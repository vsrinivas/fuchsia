// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains the core algorithm for `WorkScheduler`, a component manager subsytem for
//! dispatching batches of work.
//!
//! The subsystem's interface consists of three FIDL prototocols::
//!
//! * `fuchsia.sys2.WorkScheduler`: A framework service for scheduling and canceling work.
//! * `fuchsia.sys2.Worker`: A service that `WorkScheduler` clients expose to the framework to be
//!   notified when work units are dispatched.
//! * `fuchsia.sys2.WorkSchedulerControl`: A built-in service for controlling the period between
//!   wakeup, batch, and dispatch cycles.

use {
    crate::{
        capability::*,
        model::{error::ModelError, hooks::*, Realm},
        work_scheduler::{dispatcher::Dispatcher, work_item::WorkItem},
    },
    cm_rust::{CapabilityPath, ExposeDecl, ExposeTarget},
    failure::{format_err, Error},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_sys2 as fsys,
    fuchsia_async::{self as fasync, Time, Timer},
    fuchsia_zircon as zx,
    futures::{
        future::{AbortHandle, Abortable, BoxFuture},
        lock::Mutex,
        TryStreamExt,
    },
    lazy_static::lazy_static,
    log::warn,
    std::{
        convert::TryInto,
        sync::{Arc, Weak},
    },
};

// If you change this block, please update test `work_scheduler_capability_paths`.
lazy_static! {
    pub static ref WORKER_CAPABILITY_PATH: CapabilityPath =
        "/svc/fuchsia.sys2.Worker".try_into().unwrap();
    pub static ref WORK_SCHEDULER_CAPABILITY_PATH: CapabilityPath =
        "/svc/fuchsia.sys2.WorkScheduler".try_into().unwrap();
    pub static ref WORK_SCHEDULER_CONTROL_CAPABILITY_PATH: CapabilityPath =
        "/svc/fuchsia.sys2.WorkSchedulerControl".try_into().unwrap();
}

/// A self-managed timer instantiated by `WorkScheduler` to implement the "wakeup" part of its
/// wakeup, batch, and dispatch cycles.
struct WorkSchedulerTimer {
    /// Next absolute monotonic time when a timeout should be triggered to wakeup, batch, and
    /// dispatch work.
    next_timeout_monotonic: i64,
    /// The handle used to abort the next wakeup, batch, and dispatch cycle if it needs to be
    /// replaced by a different timer to be woken up at a different time.
    abort_handle: AbortHandle,
}

impl WorkSchedulerTimer {
    /// Construct a new timer that will fire at monotonic time `next_timeout_monotonic`. When the
    /// the timer fires, if it was not aborted, it will invoke `work_scheduler.dispatch_work()`.
    fn new(next_timeout_monotonic: i64, work_scheduler: WorkScheduler) -> Self {
        let (abort_handle, abort_registration) = AbortHandle::new_pair();

        let future = Abortable::new(
            Timer::new(Time::from_nanos(next_timeout_monotonic)),
            abort_registration,
        );
        fasync::spawn(async move {
            // Dispatch work only when abortable was not aborted.
            if future.await.is_ok() {
                work_scheduler.dispatch_work().await;
            }
        });

        WorkSchedulerTimer { next_timeout_monotonic, abort_handle }
    }
}

/// Automatically cancel a timer that is dropped by `WorkScheduler`. This allows `WorkScheduler` to
/// use patterns like:
///
///   WorkScheduler.timer = Some(WorkSchedulerTimer::new(deadline, self.clone()))
///
/// and expect any timer previously stored in `WorkScheduler.timer` to be aborted as a part of the
/// operation.
impl Drop for WorkSchedulerTimer {
    fn drop(&mut self) {
        self.abort_handle.abort();
    }
}

/// State maintained by a `WorkScheduler`, kept consistent via a single `Mutex`.
struct WorkSchedulerState {
    /// Scheduled work items that have not been dispatched.
    work_items: Vec<WorkItem>,
    /// Period between wakeup, batch, dispatch cycles. Set to `None` when dispatching work is
    /// disabled.
    batch_period: Option<i64>,
    /// Current timer for next wakeup, batch, dispatch cycle, if any.
    timer: Option<WorkSchedulerTimer>,
}

impl WorkSchedulerState {
    pub fn new() -> Self {
        WorkSchedulerState { work_items: Vec::new(), batch_period: None, timer: None }
    }

    fn set_timer(&mut self, next_monotonic_deadline: i64, work_scheduler: WorkScheduler) {
        self.timer = Some(WorkSchedulerTimer::new(next_monotonic_deadline, work_scheduler));
    }
}

/// Provides a common facility for scheduling canceling work. Each component instance manages its
/// work items in isolation from each other, but the `WorkScheduler` maintains a collection of all
/// items to make global scheduling decisions.
#[derive(Clone)]
pub struct WorkScheduler {
    inner: Arc<WorkSchedulerInner>,
}

impl WorkScheduler {
    pub fn new() -> Self {
        Self { inner: Arc::new(WorkSchedulerInner::new()) }
    }

    pub fn hooks(&self) -> Vec<HooksRegistration> {
        vec![HooksRegistration {
            events: vec![EventType::RouteBuiltinCapability, EventType::RouteFrameworkCapability],
            callback: Arc::downgrade(&self.inner) as Weak<dyn Hook>,
        }]
    }

    pub async fn schedule_work(
        &self,
        realm: Arc<Realm>,
        work_id: &str,
        work_request: &fsys::WorkRequest,
    ) -> Result<(), fsys::Error> {
        let mut state = self.inner.state.lock().await;
        self.schedule_work_request(&mut *state, realm, work_id, work_request)
    }

    fn schedule_work_request(
        &self,
        state: &mut WorkSchedulerState,
        dispatcher: Arc<dyn Dispatcher>,
        work_id: &str,
        work_request: &fsys::WorkRequest,
    ) -> Result<(), fsys::Error> {
        let work_items = &mut state.work_items;
        let work_item = WorkItem::try_new(dispatcher, work_id, work_request)?;

        if work_items.contains(&work_item) {
            return Err(fsys::Error::InstanceAlreadyExists);
        }

        work_items.push(work_item);
        work_items.sort_by(WorkItem::deadline_order);

        self.update_timeout(&mut *state);

        Ok(())
    }

    pub async fn cancel_work(&self, realm: Arc<Realm>, work_id: &str) -> Result<(), fsys::Error> {
        let mut state = self.inner.state.lock().await;
        self.cancel_work_item(&mut *state, realm, work_id)
    }

    fn cancel_work_item(
        &self,
        state: &mut WorkSchedulerState,
        dispatcher: Arc<dyn Dispatcher>,
        work_id: &str,
    ) -> Result<(), fsys::Error> {
        let work_items = &mut state.work_items;
        let work_item = WorkItem::new_by_identity(dispatcher, work_id);

        // TODO(markdittmer): Use `work_items.remove_item(work_item)` if/when it becomes stable.
        let mut found = false;
        work_items.retain(|item| {
            let matches = &work_item == item;
            found = found || matches;
            !matches
        });

        if !found {
            return Err(fsys::Error::InstanceNotFound);
        }

        self.update_timeout(&mut *state);

        Ok(())
    }

    pub async fn get_batch_period(&self) -> Result<i64, fsys::Error> {
        let state = self.inner.state.lock().await;
        match state.batch_period {
            Some(batch_period) => Ok(batch_period),
            // TODO(markdittmer): GetBatchPeriod Ok case should probably return Option<i64> to
            // more directly reflect "dispatching work disabled".
            None => Ok(std::i64::MAX),
        }
    }

    pub async fn set_batch_period(&self, batch_period: i64) -> Result<(), fsys::Error> {
        if batch_period <= 0 {
            return Err(fsys::Error::InvalidArguments);
        }

        let mut state = self.inner.state.lock().await;
        if batch_period != std::i64::MAX {
            state.batch_period = Some(batch_period);
        } else {
            // TODO(markdittmer): SetBatchPeriod should probably accept Option<i64> to more directly
            // reflect "dispatching work disabled".
            state.batch_period = None;
        }

        self.update_timeout(&mut *state);

        Ok(())
    }

    /// Dispatch expired `work_items`. In the one-shot case expired items are dispatched and dropped
    /// from `work_items`. In the periodic case expired items are retained and given a new deadline.
    /// New deadlines must meet all of the following criteria:
    ///
    ///   now < new_deadline
    ///   and
    ///   now + period <= new_deadline
    ///   and
    ///   new_deadline = first_deadline + n * period
    ///
    /// Example:
    ///
    /// F = First expected dispatch time for work item
    /// C = Current expected dispatch time for work item
    /// N = Now
    /// * = New expected dispatch time for work item
    /// | = Period marker for work item (that isn't otherwise labeled)
    ///
    /// Period markers only:      ...------|----|----|----|----|----|----|----|----|...
    /// Fully annotated timeline: ...------F----|----C----|----|----|-N--*----|----|...
    ///
    /// Example of edge case:
    ///
    /// Now lands exactly on a period marker.
    ///
    /// Period markers only:      ...------|----|----|----|----|----|----|----|----|...
    /// Fully annotated timeline: ...------F----|----C----|----|----N----*----|----|...
    ///
    /// Example of edge case:
    ///
    /// Period markers only:      ...------||||||||||||||||||||...
    /// Fully annotated timeline: ...------F||C||||||||N*||||||...
    ///
    /// Example of edge case:
    ///
    /// N=C. Denote M = N=C.
    ///
    /// Period markers only:      ...------|----|----|----|----|----|...
    /// Fully annotated timeline: ...------F----|----M----*----|----|...
    ///
    /// Note that updating `WorkItem` deadlines is _independent_ of updating `WorkScheduler` batch
    /// period. When either `work_items` (and their deadlines) change or `batch_period` changes, the
    /// next wakeup timeout is re-evaluated, but this involves updating _only_ the wakeup timeout,
    /// not any `WorkItem` deadlines.
    async fn dispatch_work(&self) {
        let mut state = self.inner.state.lock().await;
        let now = Time::now().into_nanos();
        let work_items = &mut state.work_items;
        let mut to_dispatch = Vec::new();

        work_items.retain(|item| {
            // Retain future work items.
            if item.next_deadline_monotonic > now {
                return true;
            }

            to_dispatch.push(item.clone());

            // Only dispatched/past items to retain: periodic items that will recur.
            item.period.is_some()
        });

        // Dispatch work items that are due.
        fasync::spawn(async move {
            for item in to_dispatch.into_iter() {
                let dispatcher = item.dispatcher.clone();
                let _ = dispatcher.dispatch(item).await;
            }
        });

        // Update deadlines on dispatched periodic items.
        for mut item in work_items.iter_mut() {
            // Stop processing items once we reach future items.
            if item.next_deadline_monotonic > now {
                break;
            }

            // All retained dispatched/past items have a period (hence, safe to unwrap()).
            let period = item.period.unwrap();
            item.next_deadline_monotonic += if now < item.next_deadline_monotonic + period {
                // Normal case: next deadline after adding one period is in the future.
                period
            } else {
                // Skip deadlines in the past by advancing `next_deadline_monotonic` to the first
                // multiple of `period` after now
                period * (((now - item.next_deadline_monotonic) / period) + 1)
            };
        }

        work_items.sort_by(WorkItem::deadline_order);

        self.update_timeout(&mut *state);
    }

    /// Update the timeout for the next wakeup, batch, and dispatch cycle, if necessary. The timeout
    /// should be disabled if either there are no `work_items` or there is no `batch_period`.
    /// Otherwise, a suitable timeout may already be set. A suitable timeout is one that satisfies:
    ///
    ///   timeout > work_deadline
    ///   and
    ///   timeout - batch_period < work_deadline
    ///     where
    ///       work_deadline is the earliest expected dispatch time of all `work_items`
    ///
    /// That is, a suitable timeout will trigger after there is something to schedule, but before a
    /// full `batch_period` has elapsed since the next schedulable `WorkItem` hit its deadline.
    ///
    /// If the current timeout is not suitable, then the timeout is updated to the unique suitable
    /// timeout rounded to the nearest `batch_deadline` (in absolute monotonic time):
    ///
    ///   timeout > work_deadline
    ///   and
    ///   timeout - batch_period < work_deadline
    ///   and
    ///   (timeout % batch_period) == 0
    ///     where
    ///       work_deadline is the earliest expected dispatch time of all `work_items`
    ///
    /// This scheme avoids updating the timeout whenever possible, while maintaining that all
    /// scheduled `WorkItem` objects will be dispatched no later than
    /// `WorkItem.next_deadline_monotonic + WorkScheduler.batch_period`.
    fn update_timeout(&self, state: &mut WorkSchedulerState) {
        if state.work_items.is_empty() || state.batch_period.is_none() {
            // No work to schedule. Abort any existing timer to wakeup and dispatch work.
            state.timer = None;
            return;
        }
        let work_deadline = state.work_items[0].next_deadline_monotonic;
        let batch_period = state.batch_period.unwrap();

        if let Some(timer) = &state.timer {
            let timeout = timer.next_timeout_monotonic;
            if timeout > work_deadline && timeout - batch_period < work_deadline {
                // There is an active timeout that will fire after the next deadline but before a
                // full batch period has elapsed after the deadline. Timer needs no update.
                return;
            }
        }

        // Define a deadline, an absolute monotonic time, as the soonest time after `work_deadline`
        // that is aligned with `batch_period`.
        let new_deadline = work_deadline - (work_deadline % batch_period) + batch_period;
        state.set_timer(new_deadline, self.clone());
    }
}

struct WorkSchedulerInner {
    state: Mutex<WorkSchedulerState>,
}

impl WorkSchedulerInner {
    pub fn new() -> Self {
        Self { state: Mutex::new(WorkSchedulerState::new()) }
    }

    async fn on_route_builtin_capability_async<'a>(
        self: Arc<Self>,
        capability: &'a ComponentManagerCapability,
        capability_provider: Option<Box<dyn ComponentManagerCapabilityProvider>>,
    ) -> Result<Option<Box<dyn ComponentManagerCapabilityProvider>>, ModelError> {
        match (&capability_provider, capability) {
            (None, ComponentManagerCapability::LegacyService(capability_path))
                if *capability_path == *WORK_SCHEDULER_CONTROL_CAPABILITY_PATH =>
            {
                Ok(Some(Box::new(WorkSchedulerControlCapabilityProvider::new(WorkScheduler {
                    inner: self.clone(),
                })) as Box<dyn ComponentManagerCapabilityProvider>))
            }
            _ => Ok(capability_provider),
        }
    }

    async fn on_route_framework_capability_async<'a>(
        self: Arc<Self>,
        realm: Arc<Realm>,
        capability: &'a ComponentManagerCapability,
        capability_provider: Option<Box<dyn ComponentManagerCapabilityProvider>>,
    ) -> Result<Option<Box<dyn ComponentManagerCapabilityProvider>>, ModelError> {
        match (&capability_provider, capability) {
            (None, ComponentManagerCapability::LegacyService(capability_path))
                if *capability_path == *WORK_SCHEDULER_CAPABILITY_PATH =>
            {
                Self::check_for_worker(&*realm).await?;
                Ok(Some(Box::new(WorkSchedulerCapabilityProvider::new(
                    realm.clone(),
                    WorkScheduler { inner: self.clone() },
                )) as Box<dyn ComponentManagerCapabilityProvider>))
            }
            _ => Ok(capability_provider),
        }
    }

    async fn check_for_worker(realm: &Realm) -> Result<(), ModelError> {
        let realm_state = realm.lock_state().await;
        let realm_state = realm_state.as_ref().expect("check_for_worker: not resolved");
        let decl = realm_state.decl();
        decl.exposes
            .iter()
            .find(|&expose| match expose {
                ExposeDecl::LegacyService(ls) => ls.target_path == *WORKER_CAPABILITY_PATH,
                _ => false,
            })
            .map_or_else(
                || {
                    Err(ModelError::capability_discovery_error(format_err!(
                        "component uses WorkScheduler without exposing Worker: {}",
                        realm.abs_moniker
                    )))
                },
                |expose| match expose {
                    ExposeDecl::LegacyService(ls) => match ls.target {
                        ExposeTarget::Framework => Ok(()),
                        _ => Err(ModelError::capability_discovery_error(format_err!(
                            "component exposes Worker, but not as legacy service to framework: {}",
                            realm.abs_moniker
                        ))),
                    },
                    _ => Err(ModelError::capability_discovery_error(format_err!(
                        "component exposes Worker, but not as legacy service to framework: {}",
                        realm.abs_moniker
                    ))),
                },
            )
    }
}

impl Hook for WorkSchedulerInner {
    fn on<'a>(self: Arc<Self>, event: &'a Event) -> BoxFuture<'a, Result<(), ModelError>> {
        Box::pin(async move {
            match event {
                Event::RouteBuiltinCapability { realm: _, capability, capability_provider } => {
                    let mut capability_provider = capability_provider.lock().await;
                    *capability_provider = self
                        .on_route_builtin_capability_async(capability, capability_provider.take())
                        .await?;
                }
                Event::RouteFrameworkCapability { realm, capability, capability_provider } => {
                    let mut capability_provider = capability_provider.lock().await;
                    *capability_provider = self
                        .on_route_framework_capability_async(
                            realm.clone(),
                            capability,
                            capability_provider.take(),
                        )
                        .await?;
                }
                _ => {}
            };
            Ok(())
        })
    }
}

/// `ComponentManagerCapabilityProvider` to invoke `WorkSchedulerControl` FIDL API bound to a
/// particular `WorkScheduler` object.
struct WorkSchedulerControlCapabilityProvider {
    work_scheduler: WorkScheduler,
}

impl WorkSchedulerControlCapabilityProvider {
    pub fn new(work_scheduler: WorkScheduler) -> Self {
        WorkSchedulerControlCapabilityProvider { work_scheduler }
    }

    /// Service `open` invocation via an event loop that dispatches FIDL operations to
    /// `work_scheduler`.
    async fn open_async(
        work_scheduler: WorkScheduler,
        mut stream: fsys::WorkSchedulerControlRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            match request {
                fsys::WorkSchedulerControlRequest::GetBatchPeriod { responder, .. } => {
                    let mut result = work_scheduler.get_batch_period().await;
                    responder.send(&mut result)?;
                }
                fsys::WorkSchedulerControlRequest::SetBatchPeriod {
                    responder,
                    batch_period,
                    ..
                } => {
                    let mut result = work_scheduler.set_batch_period(batch_period).await;
                    responder.send(&mut result)?;
                }
            }
        }

        Ok(())
    }
}

impl ComponentManagerCapabilityProvider for WorkSchedulerControlCapabilityProvider {
    /// Spawn an event loop to service `WorkScheduler` FIDL operations.
    fn open(
        &self,
        _flags: u32,
        _open_mode: u32,
        _relative_path: String,
        server_end: zx::Channel,
    ) -> BoxFuture<Result<(), ModelError>> {
        let server_end = ServerEnd::<fsys::WorkSchedulerControlMarker>::new(server_end);
        let stream: fsys::WorkSchedulerControlRequestStream = server_end.into_stream().unwrap();
        let work_scheduler = self.work_scheduler.clone();
        fasync::spawn(async move {
            let result = Self::open_async(work_scheduler, stream).await;
            if let Err(e) = result {
                // TODO(markdittmer): Set an epitaph to indicate this was an unexpected error.
                warn!("WorkSchedulerCapabilityProvider.open failed: {}", e);
            }
        });

        Box::pin(async { Ok(()) })
    }
}

/// `Capability` to invoke `WorkScheduler` FIDL API bound to a particular `WorkScheduler` object and
/// component instance's `AbsoluteMoniker`. All FIDL operations bound to the same object and moniker
/// observe the same collection of `WorkItem` objects.
struct WorkSchedulerCapabilityProvider {
    realm: Arc<Realm>,
    work_scheduler: WorkScheduler,
}

impl WorkSchedulerCapabilityProvider {
    pub fn new(realm: Arc<Realm>, work_scheduler: WorkScheduler) -> Self {
        WorkSchedulerCapabilityProvider { realm, work_scheduler }
    }

    /// Service `open` invocation via an event loop that dispatches FIDL operations to
    /// `work_scheduler`.
    async fn open_async(
        work_scheduler: WorkScheduler,
        realm: Arc<Realm>,
        mut stream: fsys::WorkSchedulerRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            match request {
                fsys::WorkSchedulerRequest::ScheduleWork {
                    responder,
                    work_id,
                    work_request,
                    ..
                } => {
                    let mut result =
                        work_scheduler.schedule_work(realm.clone(), &work_id, &work_request).await;
                    responder.send(&mut result)?;
                }
                fsys::WorkSchedulerRequest::CancelWork { responder, work_id, .. } => {
                    let mut result = work_scheduler.cancel_work(realm.clone(), &work_id).await;
                    responder.send(&mut result)?;
                }
            }
        }
        Ok(())
    }
}

impl ComponentManagerCapabilityProvider for WorkSchedulerCapabilityProvider {
    /// Spawn an event loop to service `WorkScheduler` FIDL operations.
    fn open(
        &self,
        _flags: u32,
        _open_mode: u32,
        _relative_path: String,
        server_end: zx::Channel,
    ) -> BoxFuture<Result<(), ModelError>> {
        let server_end = ServerEnd::<fsys::WorkSchedulerMarker>::new(server_end);
        let stream: fsys::WorkSchedulerRequestStream = server_end.into_stream().unwrap();
        let work_scheduler = self.work_scheduler.clone();
        let realm = self.realm.clone();
        fasync::spawn(async move {
            let result = Self::open_async(work_scheduler, realm, stream).await;
            if let Err(e) = result {
                // TODO(markdittmer): Set an epitaph to indicate this was an unexpected error.
                warn!("WorkSchedulerCapabilityProvider.open failed: {}", e);
            }
        });

        Box::pin(async { Ok(()) })
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            model::{AbsoluteMoniker, ChildMoniker, ResolverRegistry},
            work_scheduler::dispatcher as dspr,
        },
        fidl::endpoints::{ClientEnd, ServiceMarker},
        fidl_fuchsia_sys2::WorkSchedulerControlMarker,
        fuchsia_async::{Executor, Time, WaitState},
        futures::{future::BoxFuture, Future},
    };

    /// Time is measured in nanoseconds. This provides a constant symbol for one second.
    const SECOND: i64 = 1000000000;

    // Use arbitrary start monolithic time. This will surface bugs that, for example, are not
    // apparent when "time starts at 0".
    const FAKE_MONOTONIC_TIME: i64 = 374789234875;

    impl Dispatcher for AbsoluteMoniker {
        fn abs_moniker(&self) -> &AbsoluteMoniker {
            &self
        }
        fn dispatch(&self, _work_item: WorkItem) -> BoxFuture<Result<(), dspr::Error>> {
            Box::pin(async move { Err(dspr::Error::ComponentNotRunning) })
        }
    }

    async fn schedule_work_request(
        work_scheduler: &WorkScheduler,
        abs_moniker: &AbsoluteMoniker,
        work_id: &str,
        work_request: &fsys::WorkRequest,
    ) -> Result<(), fsys::Error> {
        let mut state = work_scheduler.inner.state.lock().await;
        work_scheduler.schedule_work_request(
            &mut *state,
            Arc::new(abs_moniker.clone()),
            work_id,
            work_request,
        )
    }

    async fn cancel_work_item(
        work_scheduler: &WorkScheduler,
        abs_moniker: &AbsoluteMoniker,
        work_id: &str,
    ) -> Result<(), fsys::Error> {
        let mut state = work_scheduler.inner.state.lock().await;
        work_scheduler.cancel_work_item(&mut *state, Arc::new(abs_moniker.clone()), work_id)
    }

    async fn get_work_status(
        work_scheduler: &WorkScheduler,
        abs_moniker: &AbsoluteMoniker,
        work_id: &str,
    ) -> Result<(i64, Option<i64>), fsys::Error> {
        let state = work_scheduler.inner.state.lock().await;
        let work_items = &state.work_items;
        match work_items.iter().find(|work_item| {
            work_item.dispatcher.abs_moniker() == abs_moniker && work_item.id == work_id
        }) {
            Some(work_item) => Ok((work_item.next_deadline_monotonic, work_item.period)),
            None => Err(fsys::Error::InstanceNotFound),
        }
    }

    async fn get_all_by_deadline(work_scheduler: &WorkScheduler) -> Vec<WorkItem> {
        let state = work_scheduler.inner.state.lock().await;
        state.work_items.clone()
    }

    fn child(parent: &AbsoluteMoniker, name: &str) -> AbsoluteMoniker {
        parent.child(ChildMoniker::new(name.to_string(), None, 0))
    }

    #[test]
    fn work_scheduler_capability_paths() {
        assert_eq!(
            format!("/svc/{}", fsys::WorkerMarker::NAME),
            WORKER_CAPABILITY_PATH.to_string()
        );
        assert_eq!(
            format!("/svc/{}", fsys::WorkSchedulerMarker::NAME),
            WORK_SCHEDULER_CAPABILITY_PATH.to_string()
        );
        assert_eq!(
            format!("/svc/{}", fsys::WorkSchedulerControlMarker::NAME),
            WORK_SCHEDULER_CONTROL_CAPABILITY_PATH.to_string()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn work_scheduler_basic() {
        let work_scheduler = WorkScheduler::new();
        let root = AbsoluteMoniker::root();
        let a = child(&root, "a");
        let b = child(&a, "b");
        let c = child(&b, "c");

        let now_once = fsys::WorkRequest {
            start: Some(fsys::Start::MonotonicTime(FAKE_MONOTONIC_TIME)),
            period: None,
        };
        let each_second = fsys::WorkRequest {
            start: Some(fsys::Start::MonotonicTime(FAKE_MONOTONIC_TIME + SECOND)),
            period: Some(SECOND),
        };
        let in_an_hour = fsys::WorkRequest {
            start: Some(fsys::Start::MonotonicTime(FAKE_MONOTONIC_TIME + (SECOND * 60 * 60))),
            period: None,
        };

        // Schedule different 2 out of 3 requests on each component instance.

        assert_eq!(Ok(()), schedule_work_request(&work_scheduler, &a, "NOW_ONCE", &now_once).await);
        assert_eq!(
            Ok(()),
            schedule_work_request(&work_scheduler, &a, "EACH_SECOND", &each_second).await
        );

        assert_eq!(
            Ok(()),
            schedule_work_request(&work_scheduler, &b, "EACH_SECOND", &each_second).await
        );
        assert_eq!(
            Ok(()),
            schedule_work_request(&work_scheduler, &b, "IN_AN_HOUR", &in_an_hour).await
        );

        assert_eq!(
            Ok(()),
            schedule_work_request(&work_scheduler, &c, "IN_AN_HOUR", &in_an_hour).await
        );
        assert_eq!(Ok(()), schedule_work_request(&work_scheduler, &c, "NOW_ONCE", &now_once).await);

        assert_eq!(
            Ok((FAKE_MONOTONIC_TIME, None)),
            get_work_status(&work_scheduler, &a, "NOW_ONCE").await
        );
        assert_eq!(
            Ok((FAKE_MONOTONIC_TIME + SECOND, Some(SECOND))),
            get_work_status(&work_scheduler, &a, "EACH_SECOND").await
        );
        assert_eq!(
            Err(fsys::Error::InstanceNotFound),
            get_work_status(&work_scheduler, &a, "IN_AN_HOUR").await
        );

        assert_eq!(
            Err(fsys::Error::InstanceNotFound),
            get_work_status(&work_scheduler, &b, "NOW_ONCE").await
        );
        assert_eq!(
            Ok((FAKE_MONOTONIC_TIME + SECOND, Some(SECOND))),
            get_work_status(&work_scheduler, &b, "EACH_SECOND").await
        );
        assert_eq!(
            Ok((FAKE_MONOTONIC_TIME + (SECOND * 60 * 60), None)),
            get_work_status(&work_scheduler, &b, "IN_AN_HOUR").await
        );

        assert_eq!(
            Ok((FAKE_MONOTONIC_TIME, None)),
            get_work_status(&work_scheduler, &c, "NOW_ONCE").await
        );
        assert_eq!(
            Err(fsys::Error::InstanceNotFound),
            get_work_status(&work_scheduler, &c, "EACH_SECOND").await
        );
        assert_eq!(
            Ok((FAKE_MONOTONIC_TIME + (SECOND * 60 * 60), None)),
            get_work_status(&work_scheduler, &c, "IN_AN_HOUR").await
        );

        // Cancel a's NOW_ONCE. Confirm it only affects a's scheduled work.

        assert_eq!(Ok(()), cancel_work_item(&work_scheduler, &a, "NOW_ONCE").await);

        assert_eq!(
            Err(fsys::Error::InstanceNotFound),
            get_work_status(&work_scheduler, &a, "NOW_ONCE").await
        );
        assert_eq!(
            Ok((FAKE_MONOTONIC_TIME + SECOND, Some(SECOND))),
            get_work_status(&work_scheduler, &a, "EACH_SECOND").await
        );
        assert_eq!(
            Err(fsys::Error::InstanceNotFound),
            get_work_status(&work_scheduler, &a, "IN_AN_HOUR").await
        );

        assert_eq!(
            Err(fsys::Error::InstanceNotFound),
            get_work_status(&work_scheduler, &b, "NOW_ONCE").await
        );
        assert_eq!(
            Ok((FAKE_MONOTONIC_TIME + SECOND, Some(SECOND))),
            get_work_status(&work_scheduler, &b, "EACH_SECOND").await
        );
        assert_eq!(
            Ok((FAKE_MONOTONIC_TIME + (SECOND * 60 * 60), None)),
            get_work_status(&work_scheduler, &b, "IN_AN_HOUR").await
        );

        assert_eq!(
            Ok((FAKE_MONOTONIC_TIME, None)),
            get_work_status(&work_scheduler, &c, "NOW_ONCE").await
        );
        assert_eq!(
            Err(fsys::Error::InstanceNotFound),
            get_work_status(&work_scheduler, &c, "EACH_SECOND").await
        );
        assert_eq!(
            Ok((FAKE_MONOTONIC_TIME + (SECOND * 60 * 60), None)),
            get_work_status(&work_scheduler, &c, "IN_AN_HOUR").await
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn work_scheduler_deadline_order() {
        let work_scheduler = WorkScheduler::new();
        let root = AbsoluteMoniker::root();
        let a = child(&root, "a");
        let b = child(&a, "b");
        let c = child(&b, "c");

        let now_once = fsys::WorkRequest {
            start: Some(fsys::Start::MonotonicTime(FAKE_MONOTONIC_TIME)),
            period: None,
        };
        let each_second = fsys::WorkRequest {
            start: Some(fsys::Start::MonotonicTime(FAKE_MONOTONIC_TIME + SECOND)),
            period: Some(SECOND),
        };
        let in_an_hour = fsys::WorkRequest {
            start: Some(fsys::Start::MonotonicTime(FAKE_MONOTONIC_TIME + (SECOND * 60 * 60))),
            period: None,
        };

        assert_eq!(
            Ok(()),
            schedule_work_request(&work_scheduler, &a, "EACH_SECOND", &each_second).await
        );
        assert_eq!(Ok(()), schedule_work_request(&work_scheduler, &c, "NOW_ONCE", &now_once).await);
        assert_eq!(
            Ok(()),
            schedule_work_request(&work_scheduler, &b, "IN_AN_HOUR", &in_an_hour).await
        );

        // Order should match deadlines, not order of scheduling or component topology.
        assert_eq!(
            vec![
                WorkItem::new(Arc::new(c), "NOW_ONCE", FAKE_MONOTONIC_TIME, None),
                WorkItem::new(
                    Arc::new(a),
                    "EACH_SECOND",
                    FAKE_MONOTONIC_TIME + SECOND,
                    Some(SECOND),
                ),
                WorkItem::new(
                    Arc::new(b),
                    "IN_AN_HOUR",
                    FAKE_MONOTONIC_TIME + (SECOND * 60 * 60),
                    None,
                ),
            ],
            get_all_by_deadline(&work_scheduler).await
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn work_scheduler_batch_period() {
        let work_scheduler = WorkScheduler::new();
        assert_eq!(Ok(std::i64::MAX), work_scheduler.get_batch_period().await);
        assert_eq!(Ok(()), work_scheduler.set_batch_period(SECOND).await);
        assert_eq!(Ok(SECOND), work_scheduler.get_batch_period().await)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn work_scheduler_batch_period_error() {
        let work_scheduler = WorkScheduler::new();
        assert_eq!(Err(fsys::Error::InvalidArguments), work_scheduler.set_batch_period(0).await);
        assert_eq!(Err(fsys::Error::InvalidArguments), work_scheduler.set_batch_period(-1).await)
    }

    struct TestWorkUnit {
        start: i64,
        work_item: WorkItem,
    }

    impl TestWorkUnit {
        fn new(
            start: i64,
            abs_moniker: &AbsoluteMoniker,
            id: &str,
            next_deadline_monotonic: i64,
            period: Option<i64>,
        ) -> Self {
            TestWorkUnit {
                start,
                work_item: WorkItem::new(
                    Arc::new(abs_moniker.clone()),
                    id,
                    next_deadline_monotonic,
                    period,
                ),
            }
        }
    }

    struct TimeTest {
        executor: Executor,
    }

    impl TimeTest {
        fn new() -> Self {
            let executor = Executor::new_with_fake_time().unwrap();
            executor.set_fake_time(Time::from_nanos(0));
            TimeTest { executor }
        }

        fn set_time(&mut self, time: i64) {
            self.executor.set_fake_time(Time::from_nanos(time));
        }

        fn run_and_sync<F>(&mut self, fut: &mut F)
        where
            F: Future + Unpin,
        {
            assert!(self.executor.run_until_stalled(fut).is_ready());
            while self.executor.is_waiting() == WaitState::Ready {
                assert!(self.executor.run_until_stalled(&mut Box::pin(async {})).is_ready());
            }
        }

        fn set_time_and_run_timers(&mut self, time: i64) {
            self.set_time(time);
            assert!(self.executor.wake_expired_timers());
            while self.executor.is_waiting() == WaitState::Ready {
                assert!(self.executor.run_until_stalled(&mut Box::pin(async {})).is_ready());
            }
        }

        fn assert_no_timers(&mut self) {
            assert_eq!(None, self.executor.wake_next_timer());
        }

        fn assert_next_timer_at(&mut self, time: i64) {
            assert_eq!(WaitState::Waiting(Time::from_nanos(time)), self.executor.is_waiting());
        }

        fn assert_work_items(
            &mut self,
            work_scheduler: &WorkScheduler,
            test_work_units: Vec<TestWorkUnit>,
        ) {
            self.run_and_sync(&mut Box::pin(async {
                // Check collection of work items.
                let work_items: Vec<WorkItem> = test_work_units
                    .iter()
                    .map(|test_work_unit| test_work_unit.work_item.clone())
                    .collect();
                assert_eq!(work_items, get_all_by_deadline(&work_scheduler).await);

                // Check invariants on relationships between `now` and `WorkItem` state.
                let now = Time::now().into_nanos();
                for test_work_unit in test_work_units.iter() {
                    let work_item = &test_work_unit.work_item;
                    let deadline = work_item.next_deadline_monotonic;
                    // Either:
                    // 1. This is a check for initial state, in which case allow now=deadline=0, or
                    // 2. All deadlines should be in the future.
                    assert!(
                        (now == 0 && deadline == now) || now < deadline,
                        "Expected either
                            1. This is a check for initial state, so allow now=deadline=0, or
                            2. All deadlines should be in the future."
                    );
                    if let Some(period) = work_item.period {
                        // All periodic deadlines should be either:
                        // 1. Waiting to be dispatched for the first time, or
                        // 2. At most one period into the future (for otherwise, a period would be
                        //    skipped).
                        assert!(
                            now < test_work_unit.start || now + period >= deadline,
                            "Expected all periodic deadlines should be either:
                                1. Waiting to be dispatched for the first time, or
                                2. At most one period into the future (for otherwise, a period would
                                    be skipped"
                        );
                        // All periodic deadlines should be aligned to:
                        // `deadline = start + n*period` for some non-negative integer, `n`.
                        assert_eq!(
                            0,
                            (deadline - test_work_unit.start) % period,
                            "Expected all periodic deadlines should be aligned to:
                                `deadline = start + n*period` for some non-negative integer, `n`."
                        );
                    }
                }
            }));
        }

        fn assert_no_work(&mut self, work_scheduler: &WorkScheduler) {
            self.run_and_sync(&mut Box::pin(async {
                assert_eq!(vec![] as Vec<WorkItem>, get_all_by_deadline(&work_scheduler).await);
            }));
        }
    }

    #[test]
    fn work_scheduler_time_get_batch_period_queues_nothing() {
        let mut t = TimeTest::new();
        t.run_and_sync(&mut Box::pin(async {
            let work_scheduler = WorkScheduler::new();
            assert_eq!(Ok(std::i64::MAX), work_scheduler.get_batch_period().await);
        }));
        t.assert_no_timers();
    }

    #[test]
    fn work_scheduler_time_set_batch_period_no_work_queues_nothing() {
        let mut t = TimeTest::new();
        t.run_and_sync(&mut Box::pin(async {
            let work_scheduler = WorkScheduler::new();
            assert_eq!(Ok(()), work_scheduler.set_batch_period(1).await);
        }));
        t.assert_no_timers();
    }

    #[test]
    fn work_scheduler_time_schedule_inf_batch_period_queues_nothing() {
        let mut t = TimeTest::new();
        t.run_and_sync(&mut Box::pin(async {
            let work_scheduler = WorkScheduler::new();
            let root = AbsoluteMoniker::root();
            let now_once =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(0)), period: None };
            assert_eq!(
                Ok(()),
                schedule_work_request(&work_scheduler, &root, "NOW_ONCE", &now_once).await
            );
        }));
        t.assert_no_timers();
    }

    #[test]
    fn work_scheduler_time_schedule_finite_batch_period_queues_and_dispatches() {
        let mut t = TimeTest::new();
        let work_scheduler = WorkScheduler::new();
        let root = AbsoluteMoniker::root();

        // Set batch period and queue a unit of work.
        t.run_and_sync(&mut Box::pin(async {
            assert_eq!(Ok(()), work_scheduler.set_batch_period(1).await);
            let now_once =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(0)), period: None };
            assert_eq!(
                Ok(()),
                schedule_work_request(&work_scheduler, &root, "NOW_ONCE", &now_once).await
            );
        }));

        // Confirm timer and work item.
        t.assert_next_timer_at(1);
        t.assert_work_items(
            &work_scheduler,
            vec![TestWorkUnit::new(0, &root, "NOW_ONCE", 0, None)],
        );

        // Run work stemming from timer and confirm no more work items.
        t.set_time_and_run_timers(1);
        t.assert_no_work(&work_scheduler);
    }

    #[test]
    fn work_scheduler_time_periodic_stays_queued() {
        let mut t = TimeTest::new();
        let work_scheduler = WorkScheduler::new();
        let root = AbsoluteMoniker::root();

        // Set batch period and queue a unit of work.
        t.run_and_sync(&mut Box::pin(async {
            assert_eq!(Ok(()), work_scheduler.set_batch_period(1).await);
            let every_moment =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(0)), period: Some(1) };
            assert_eq!(
                Ok(()),
                schedule_work_request(&work_scheduler, &root, "EVERY_MOMENT", &every_moment).await
            );
        }));

        // Confirm timer and work item.
        t.assert_next_timer_at(1);
        t.assert_work_items(
            &work_scheduler,
            vec![TestWorkUnit::new(0, &root, "EVERY_MOMENT", 0, Some(1))],
        );

        // Dispatch work and assert next periodic work item and timer.
        t.set_time_and_run_timers(1);
        t.assert_work_items(
            &work_scheduler,
            vec![TestWorkUnit::new(0, &root, "EVERY_MOMENT", 2, Some(1))],
        );
        t.assert_next_timer_at(3);
    }

    #[test]
    fn work_scheduler_time_timeout_updates_when_earlier_work_item_added() {
        let mut t = TimeTest::new();
        let work_scheduler = WorkScheduler::new();
        let root = AbsoluteMoniker::root();

        // Set batch period and queue a unit of work.
        t.run_and_sync(&mut Box::pin(async {
            assert_eq!(Ok(()), work_scheduler.set_batch_period(5).await);
            let at_nine =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(9)), period: None };
            assert_eq!(
                Ok(()),
                schedule_work_request(&work_scheduler, &root, "AT_NINE", &at_nine).await
            );
        }));

        // Confirm timer and work item.
        t.assert_next_timer_at(10);
        t.assert_work_items(&work_scheduler, vec![TestWorkUnit::new(9, &root, "AT_NINE", 9, None)]);

        // Queue unit of work with deadline _earlier_ than first unit of work.
        t.run_and_sync(&mut Box::pin(async {
            let at_four =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(4)), period: None };
            assert_eq!(
                Ok(()),
                schedule_work_request(&work_scheduler, &root, "AT_FOUR", &at_four).await
            );
        }));

        // Confirm timer moved _back_, and work units are as expected.
        t.assert_next_timer_at(5);
        t.assert_work_items(
            &work_scheduler,
            vec![
                TestWorkUnit::new(4, &root, "AT_FOUR", 4, None),
                TestWorkUnit::new(9, &root, "AT_NINE", 9, None),
            ],
        );

        // Dispatch work and assert remaining work and timer.
        t.set_time_and_run_timers(5);
        t.assert_work_items(&work_scheduler, vec![TestWorkUnit::new(9, &root, "AT_NINE", 9, None)]);
        t.assert_next_timer_at(10);

        // Queue unit of work with deadline _later_ than existing unit of work.
        t.run_and_sync(&mut Box::pin(async {
            let at_ten =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(10)), period: None };
            assert_eq!(
                Ok(()),
                schedule_work_request(&work_scheduler, &root, "AT_TEN", &at_ten).await
            );
        }));

        // Confirm unchanged, and work units are as expected.
        t.assert_next_timer_at(10);
        t.assert_work_items(
            &work_scheduler,
            vec![
                TestWorkUnit::new(9, &root, "AT_NINE", 9, None),
                TestWorkUnit::new(10, &root, "AT_TEN", 10, None),
            ],
        );

        // Dispatch work and assert no work left.
        t.set_time_and_run_timers(10);
        t.assert_no_work(&work_scheduler);
    }

    #[test]
    fn work_scheduler_time_late_timer_fire() {
        let mut t = TimeTest::new();
        let work_scheduler = WorkScheduler::new();
        let root = AbsoluteMoniker::root();

        // Set period and schedule two work items, one of which _should_ be dispatched in a second
        // cycle.
        t.run_and_sync(&mut Box::pin(async {
            assert_eq!(Ok(()), work_scheduler.set_batch_period(5).await);
            let at_four =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(4)), period: None };
            assert_eq!(
                Ok(()),
                schedule_work_request(&work_scheduler, &root, "AT_FOUR", &at_four).await
            );
            let at_nine =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(9)), period: None };
            assert_eq!(
                Ok(()),
                schedule_work_request(&work_scheduler, &root, "AT_NINE", &at_nine).await
            );
        }));

        // Confirm timer and work items.
        t.assert_next_timer_at(5);
        t.assert_work_items(
            &work_scheduler,
            vec![
                TestWorkUnit::new(4, &root, "AT_FOUR", 4, None),
                TestWorkUnit::new(9, &root, "AT_NINE", 9, None),
            ],
        );

        // Simulate delayed dispatch: System load or some other factor caused dispatch of work to be
        // delayed beyond the deadline of _both_ units of work.
        t.set_time_and_run_timers(16);

        // Confirm timers and dispatched units.
        t.assert_no_timers();
        t.assert_no_work(&work_scheduler);
    }

    #[test]
    fn work_scheduler_time_late_timer_fire_periodic_work_item() {
        let mut t = TimeTest::new();
        let work_scheduler = WorkScheduler::new();
        let root = AbsoluteMoniker::root();

        // Set period and schedule two work items, one of which _should_ be dispatched in a second
        // cycle.
        t.run_and_sync(&mut Box::pin(async {
            assert_eq!(Ok(()), work_scheduler.set_batch_period(5).await);
            let at_four =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(4)), period: None };
            assert_eq!(
                Ok(()),
                schedule_work_request(&work_scheduler, &root, "AT_FOUR", &at_four).await
            );
            let at_nine_periodic =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(9)), period: Some(5) };
            assert_eq!(
                Ok(()),
                schedule_work_request(
                    &work_scheduler,
                    &root,
                    "AT_NINE_PERIODIC_FIVE",
                    &at_nine_periodic
                )
                .await
            );
        }));

        // Confirm timer and work items.
        t.assert_next_timer_at(5);
        t.assert_work_items(
            &work_scheduler,
            vec![
                TestWorkUnit::new(4, &root, "AT_FOUR", 4, None),
                TestWorkUnit::new(9, &root, "AT_NINE_PERIODIC_FIVE", 9, Some(5)),
            ],
        );

        // Simulate _seriously_ delayed dispatch: System load or some other
        // factor caused dispatch of work to be delayed _way_ beyond the
        // deadline of _both_ units of work.
        t.set_time_and_run_timers(116);

        // Confirm timer set to next batch period, and periodic work item still queued.
        t.assert_next_timer_at(120);
        t.assert_work_items(
            &work_scheduler,
            // Time:
            //   now=116
            // WorkItem:
            //  start=9
            //  period=5
            //
            // Updated WorkItem.period should be:
            //   WorkItem.next_deadline_monotonic = 9 + 5*n
            //     where
            //       Time.now < WorkItem.next_deadline_monotonic
            //       and
            //       Time.now + WorkItem.period > WorkItem.next_deadline_monotonic
            //
            // WorkItem.next_deadline_monotonic = 119 = 9 + (22 * 5).
            vec![TestWorkUnit::new(9, &root, "AT_NINE_PERIODIC_FIVE", 119, Some(5))],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn connect_to_work_scheduler_control_service() -> Result<(), Error> {
        let work_scheduler = WorkScheduler::new();
        let hooks = Hooks::new(None);
        hooks.install(work_scheduler.hooks()).await;

        let capability_provider = Arc::new(Mutex::new(None));
        let capability = ComponentManagerCapability::LegacyService(
            WORK_SCHEDULER_CONTROL_CAPABILITY_PATH.clone(),
        );

        let (client, server) = zx::Channel::create()?;

        let realm = {
            let resolver = ResolverRegistry::new();
            let root_component_url = "test:///root".to_string();
            Arc::new(Realm::new_root_realm(resolver, root_component_url))
        };
        let event = Event::RouteBuiltinCapability {
            realm: realm.clone(),
            capability: capability.clone(),
            capability_provider: capability_provider.clone(),
        };
        hooks.dispatch(&event).await?;

        let capability_provider = capability_provider.lock().await.take();
        if let Some(capability_provider) = capability_provider {
            capability_provider.open(0, 0, String::new(), server).await?;
        }

        let work_scheduler_control = ClientEnd::<WorkSchedulerControlMarker>::new(client)
            .into_proxy()
            .expect("failed to create launcher proxy");
        let result = work_scheduler_control.get_batch_period().await;
        result
            .expect("failed to use WorkSchedulerControl service")
            .expect("WorkSchedulerControl.GetBatchPeriod() yielded error");
        Ok(())
    }
}
