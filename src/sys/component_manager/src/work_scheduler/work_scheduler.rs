// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        framework::FrameworkCapability,
        model::{
            error::ModelError, framework_services::FrameworkServiceError,
            hooks::RouteFrameworkCapabilityHook, AbsoluteMoniker, Realm,
        },
        work_scheduler::{
            time::{RealTime, TimeSource},
            work::Works,
        },
    },
    cm_rust::{CapabilityPath, FrameworkCapabilityDecl},
    fidl::{endpoints::ServerEnd, Error},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{future::BoxFuture, lock::Mutex, TryStreamExt},
    lazy_static::lazy_static,
    log::warn,
    std::{collections::HashMap, convert::TryInto, sync::Arc},
};

lazy_static! {
    pub static ref WORK_SCHEDULER_CAPABILITY_PATH: CapabilityPath =
        "/svc/fuchsia.sys2.WorkScheduler".try_into().unwrap();
}

/// Provides a common facility for scheduling, inspecting, and canceling work. Each component
/// instance manages its work items in isolation from each other, but the `WorkScheduler` maintains
/// a collection of all items to make global scheduling decisions.
struct WorkScheduler {
    works: Mutex<HashMap<AbsoluteMoniker, Works>>,
    time_source: Arc<dyn TimeSource>,
}

impl WorkScheduler {
    pub fn new(time_source: Arc<dyn TimeSource>) -> Self {
        WorkScheduler { works: Mutex::new(HashMap::new()), time_source: time_source }
    }

    pub async fn schedule_work(
        &self,
        abs_moniker: &AbsoluteMoniker,
        work_id: &str,
        work_request: &fsys::WorkRequest,
    ) -> Result<(), fsys::Error> {
        let mut works_by_abs_moniker = self.works.lock().await;
        let works = works_by_abs_moniker
            .entry(abs_moniker.clone())
            .or_insert_with(|| Works::new(self.time_source.clone()));
        works.insert(work_id, work_request)
    }

    pub async fn cancel_work(
        &self,
        abs_moniker: &AbsoluteMoniker,
        work_id: &str,
    ) -> Result<(), fsys::Error> {
        let mut works_by_abs_moniker = self.works.lock().await;
        let works = works_by_abs_moniker
            .entry(abs_moniker.clone())
            .or_insert_with(|| Works::new(self.time_source.clone()));
        works.delete(work_id)
    }
}

/// `Capability` to invoke `WorkScheduler` FIDL API bound to a particular `WorkScheduler` object and
/// component instance's `AbsoluteMoniker`. All FIDL operations bound to the same object and moniker
/// observe the same `Works` collection.
struct WorkSchedulerCapability {
    work_scheduler: Arc<WorkScheduler>,
    abs_moniker: AbsoluteMoniker,
}

impl WorkSchedulerCapability {
    pub fn new(work_scheduler: Arc<WorkScheduler>, abs_moniker: AbsoluteMoniker) -> Self {
        WorkSchedulerCapability { work_scheduler: work_scheduler, abs_moniker: abs_moniker }
    }

    fn err(err: Error) -> ModelError {
        ModelError::from(FrameworkServiceError::service_error(
            "WorkScheduler service interrupted",
            err,
        ))
    }

    /// Service `open` invocation via an event loop that dispatches FIDL operations to
    /// `work_scheduler`.
    async fn open_async(
        work_scheduler: Arc<WorkScheduler>,
        abs_moniker: &AbsoluteMoniker,
        mut stream: fsys::WorkSchedulerRequestStream,
    ) -> Result<(), ModelError> {
        while let Some(request) = stream.try_next().await.map_err(Self::err)? {
            match request {
                fsys::WorkSchedulerRequest::ScheduleWork {
                    responder,
                    work_id,
                    work_request,
                    ..
                } => {
                    let mut result =
                        work_scheduler.schedule_work(abs_moniker, &work_id, &work_request).await;
                    responder.send(&mut result).map_err(Self::err)?;
                }
                fsys::WorkSchedulerRequest::CancelWork { responder, work_id, .. } => {
                    let mut result = work_scheduler.cancel_work(abs_moniker, &work_id).await;
                    responder.send(&mut result).map_err(Self::err)?;
                }
            }
        }
        Ok(())
    }
}

impl FrameworkCapability for WorkSchedulerCapability {
    /// Spawn an event loop to service `WorkScheduler` FIDL operations.
    fn open(
        &self,
        _flags: u32,
        _open_mode: u32,
        _relative_path: String,
        server_end: zx::Channel,
    ) -> BoxFuture<Result<(), ModelError>> {
        let server_end: ServerEnd<fsys::WorkSchedulerMarker> = ServerEnd::new(server_end);
        let stream: fsys::WorkSchedulerRequestStream = server_end.into_stream().unwrap();
        let work_scheduler = self.work_scheduler.clone();
        let abs_moniker = self.abs_moniker.clone();
        fasync::spawn(async move {
            let result = Self::open_async(work_scheduler, &abs_moniker, stream).await;
            if let Err(e) = result {
                // TODO(markdittmer): Set an epitaph to indicate this was an unexpected error.
                warn!("WorkSchedulerCapability.open failed: {}", e);
            }
        });

        Box::pin(async { Ok(()) })
    }
}

/// `Hook` for attaching `WorkScheduler` FIDL service to a component manager `Model`.
pub struct WorkSchedulerHook {
    work_scheduler: Arc<WorkScheduler>,
}

/// Passthrough all ops unconditionally, except `on_route_framework_capability`. (See docs for
/// `on_route_capability_async` for details.)
impl RouteFrameworkCapabilityHook for WorkSchedulerHook {
    fn on<'a>(
        &'a self,
        realm: Arc<Realm>,
        capability_decl: &'a FrameworkCapabilityDecl,
        capability: Option<Box<dyn FrameworkCapability>>,
    ) -> BoxFuture<Result<Option<Box<dyn FrameworkCapability>>, ModelError>> {
        Box::pin(self.on_route_capability_async(realm, capability_decl, capability))
    }
}

impl WorkSchedulerHook {
    pub fn new() -> Self {
        WorkSchedulerHook {
            work_scheduler: Arc::new(WorkScheduler::new(Arc::new(RealTime::new()))),
        }
    }

    async fn on_route_capability_async<'a>(
        &'a self,
        realm: Arc<Realm>,
        capability_decl: &'a FrameworkCapabilityDecl,
        capability: Option<Box<dyn FrameworkCapability>>,
    ) -> Result<Option<Box<dyn FrameworkCapability>>, ModelError> {
        match capability_decl {
            FrameworkCapabilityDecl::Service(capability_path)
                if *capability_path == *WORK_SCHEDULER_CAPABILITY_PATH =>
            {
                Ok(Some(Box::new(WorkSchedulerCapability::new(
                    self.work_scheduler.clone(),
                    realm.abs_moniker.clone(),
                )) as Box<dyn FrameworkCapability>))
            }
            _ => Ok(capability),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            model::{AbsoluteMoniker, ChildMoniker},
            work_scheduler::{
                time::test::{FakeTimeSource, SECOND},
                work::{test as work_test, WorkStatus},
            },
        },
    };

    async fn get_work_by_id(
        work_scheduler: &WorkScheduler,
        abs_moniker: &AbsoluteMoniker,
        work_id: &str,
    ) -> Result<WorkStatus, fsys::Error> {
        let mut works_by_abs_moniker = work_scheduler.works.lock().await;
        let works = works_by_abs_moniker
            .entry(abs_moniker.clone())
            .or_insert_with(|| Works::new(work_scheduler.time_source.clone()));
        work_test::get_works_status(works, work_id)
    }

    fn child(parent: &AbsoluteMoniker, name: &str) -> AbsoluteMoniker {
        parent.child(ChildMoniker::new(name.to_string(), None))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn work_scheduler_basic() {
        let time_source = Arc::new(FakeTimeSource::new());
        let work_scheduler = WorkScheduler::new(time_source.clone());
        let now_monotonic = time_source.get_monotonic();
        let root = AbsoluteMoniker::root();
        let a = child(&root, "a");
        let b = child(&a, "b");
        let c = child(&b, "c");

        let now_once = fsys::WorkRequest {
            start: Some(fsys::Start::MonotonicTime(now_monotonic)),
            period: None,
        };
        let each_second = fsys::WorkRequest {
            start: Some(fsys::Start::MonotonicTime(now_monotonic + SECOND)),
            period: Some(SECOND),
        };
        let in_an_hour = fsys::WorkRequest {
            start: Some(fsys::Start::MonotonicTime(now_monotonic + (SECOND * 60 * 60))),
            period: None,
        };

        // Schedule different 2 out of 3 requests on each component instance.

        assert_eq!(Ok(()), work_scheduler.schedule_work(&a, "NOW_ONCE", &now_once).await);
        assert_eq!(Ok(()), work_scheduler.schedule_work(&a, "EACH_SECOND", &each_second).await);

        assert_eq!(Ok(()), work_scheduler.schedule_work(&b, "EACH_SECOND", &each_second).await);
        assert_eq!(Ok(()), work_scheduler.schedule_work(&b, "IN_AN_HOUR", &in_an_hour).await);

        assert_eq!(Ok(()), work_scheduler.schedule_work(&c, "IN_AN_HOUR", &in_an_hour).await);
        assert_eq!(Ok(()), work_scheduler.schedule_work(&c, "NOW_ONCE", &now_once).await);

        // TODO(markdittmer): Create macro(s) to make this more terse but still explicit.
        assert_eq!(
            Ok(WorkStatus { next_run_monotonic_time: time_source.get_monotonic(), period: None }),
            get_work_by_id(&work_scheduler, &a, "NOW_ONCE").await
        );
        assert_eq!(
            Ok(WorkStatus {
                next_run_monotonic_time: time_source.get_monotonic() + SECOND,
                period: Some(SECOND),
            }),
            get_work_by_id(&work_scheduler, &a, "EACH_SECOND").await
        );
        assert_eq!(
            Err(fsys::Error::InstanceNotFound),
            get_work_by_id(&work_scheduler, &a, "IN_AN_HOUR").await
        );

        assert_eq!(
            Err(fsys::Error::InstanceNotFound),
            get_work_by_id(&work_scheduler, &b, "NOW_ONCE").await
        );
        assert_eq!(
            Ok(WorkStatus {
                next_run_monotonic_time: time_source.get_monotonic() + SECOND,
                period: Some(SECOND),
            }),
            get_work_by_id(&work_scheduler, &b, "EACH_SECOND").await
        );
        assert_eq!(
            Ok(WorkStatus {
                next_run_monotonic_time: time_source.get_monotonic() + (SECOND * 60 * 60),
                period: None,
            }),
            get_work_by_id(&work_scheduler, &b, "IN_AN_HOUR").await
        );

        assert_eq!(
            Ok(WorkStatus { next_run_monotonic_time: time_source.get_monotonic(), period: None }),
            get_work_by_id(&work_scheduler, &c, "NOW_ONCE").await
        );
        assert_eq!(
            Err(fsys::Error::InstanceNotFound),
            get_work_by_id(&work_scheduler, &c, "EACH_SECOND").await
        );
        assert_eq!(
            Ok(WorkStatus {
                next_run_monotonic_time: time_source.get_monotonic() + (SECOND * 60 * 60),
                period: None,
            }),
            get_work_by_id(&work_scheduler, &c, "IN_AN_HOUR").await
        );

        // Cancel a's NOW_ONCE. Confirm it only affects a's scheduled work.

        assert_eq!(Ok(()), work_scheduler.cancel_work(&a, "NOW_ONCE").await);

        assert_eq!(
            Err(fsys::Error::InstanceNotFound),
            get_work_by_id(&work_scheduler, &a, "NOW_ONCE").await
        );
        assert_eq!(
            Ok(WorkStatus {
                next_run_monotonic_time: time_source.get_monotonic() + SECOND,
                period: Some(SECOND),
            }),
            get_work_by_id(&work_scheduler, &a, "EACH_SECOND").await
        );
        assert_eq!(
            Err(fsys::Error::InstanceNotFound),
            get_work_by_id(&work_scheduler, &a, "IN_AN_HOUR").await
        );

        assert_eq!(
            Err(fsys::Error::InstanceNotFound),
            get_work_by_id(&work_scheduler, &b, "NOW_ONCE").await
        );
        assert_eq!(
            Ok(WorkStatus {
                next_run_monotonic_time: time_source.get_monotonic() + SECOND,
                period: Some(SECOND),
            }),
            get_work_by_id(&work_scheduler, &b, "EACH_SECOND").await
        );
        assert_eq!(
            Ok(WorkStatus {
                next_run_monotonic_time: time_source.get_monotonic() + (SECOND * 60 * 60),
                period: None,
            }),
            get_work_by_id(&work_scheduler, &b, "IN_AN_HOUR").await
        );

        assert_eq!(
            Ok(WorkStatus { next_run_monotonic_time: time_source.get_monotonic(), period: None }),
            get_work_by_id(&work_scheduler, &c, "NOW_ONCE").await
        );
        assert_eq!(
            Err(fsys::Error::InstanceNotFound),
            get_work_by_id(&work_scheduler, &c, "EACH_SECOND").await
        );
        assert_eq!(
            Ok(WorkStatus {
                next_run_monotonic_time: time_source.get_monotonic() + (SECOND * 60 * 60),
                period: None,
            }),
            get_work_by_id(&work_scheduler, &c, "IN_AN_HOUR").await
        );
    }
}
