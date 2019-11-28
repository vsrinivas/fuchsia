// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{ComponentManagerCapability, ComponentManagerCapabilityProvider},
        model::{Event, EventPayload, EventType, Hook, HooksRegistration, ModelError, Realm},
        work_scheduler::work_scheduler::{
            WorkScheduler, WORKER_CAPABILITY_PATH, WORK_SCHEDULER_CAPABILITY_PATH,
            WORK_SCHEDULER_CONTROL_CAPABILITY_PATH,
        },
    },
    failure::{format_err, Error},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{future::BoxFuture, TryStreamExt},
    log::warn,
    std::sync::{Arc, Weak},
};

// TODO(markdittmer): Establish
// WorkSchedulerSystem -> (WorkScheduler, WorkSchedulerHook -> WorkScheduler).
impl WorkScheduler {
    /// Helper to specify hooks associated with `WorkScheduler`. Accepts `&Arc<WorkScheduler>` to
    /// produce references needed by `HooksRegistration` without consuming `Arc`.
    pub fn hooks(work_scheduler: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration {
            events: vec![EventType::RouteBuiltinCapability, EventType::RouteFrameworkCapability],
            callback: Arc::downgrade(work_scheduler) as Weak<dyn Hook>,
        }]
    }

    /// Route capability to access `fuchsia.sys2.WorkSchedulerControl` protocol as a builtin
    /// capability.
    async fn on_route_builtin_capability_async<'a>(
        self: Arc<Self>,
        capability: &'a ComponentManagerCapability,
        capability_provider: Option<Box<dyn ComponentManagerCapabilityProvider>>,
    ) -> Result<Option<Box<dyn ComponentManagerCapabilityProvider>>, ModelError> {
        match (&capability_provider, capability) {
            (None, ComponentManagerCapability::LegacyService(capability_path))
                if *capability_path == *WORK_SCHEDULER_CONTROL_CAPABILITY_PATH =>
            {
                Ok(Some(Box::new(WorkSchedulerControlCapabilityProvider::new(self.clone()))
                    as Box<dyn ComponentManagerCapabilityProvider>))
            }
            _ => Ok(capability_provider),
        }
    }

    /// Route capability to access `fuchsia.sys2.WorkScheduler` protocol as a framework capability.
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
                // Only clients that expose the Worker protocol to the framework can
                // use WorkScheduler.
                Self::verify_worker_exposed_to_framework(&*realm).await?;
                Ok(Some(
                    Box::new(WorkSchedulerCapabilityProvider::new(realm.clone(), self.clone()))
                        as Box<dyn ComponentManagerCapabilityProvider>,
                ))
            }
            _ => Ok(capability_provider),
        }
    }

    /// Ensure that `fuchsia.sys2.WorkScheduler` clients expose `fuchsia.sys2.Worker` to recieve
    /// work dispatch callbacks.
    async fn verify_worker_exposed_to_framework(realm: &Realm) -> Result<(), ModelError> {
        if realm.is_service_exposed_to_framework(&*WORKER_CAPABILITY_PATH).await {
            Ok(())
        } else {
            Err(ModelError::capability_discovery_error(format_err!(
                "Component {} does not expose Worker",
                realm.abs_moniker
            )))
        }
    }
}

impl Hook for WorkScheduler {
    fn on<'a>(self: Arc<Self>, event: &'a Event) -> BoxFuture<'a, Result<(), ModelError>> {
        Box::pin(async move {
            match &event.payload {
                EventPayload::RouteBuiltinCapability { capability, capability_provider } => {
                    let mut capability_provider = capability_provider.lock().await;
                    *capability_provider = self
                        .on_route_builtin_capability_async(&capability, capability_provider.take())
                        .await?;
                }
                EventPayload::RouteFrameworkCapability { capability, capability_provider } => {
                    let mut capability_provider = capability_provider.lock().await;
                    *capability_provider = self
                        .on_route_framework_capability_async(
                            event.target_realm.clone(),
                            &capability,
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
    work_scheduler: Arc<WorkScheduler>,
}

impl WorkSchedulerControlCapabilityProvider {
    fn new(work_scheduler: Arc<WorkScheduler>) -> Self {
        WorkSchedulerControlCapabilityProvider { work_scheduler }
    }

    /// Service `open` invocation via an event loop that dispatches FIDL operations to
    /// `work_scheduler`.
    async fn open_async(
        work_scheduler: Arc<WorkScheduler>,
        mut stream: fsys::WorkSchedulerControlRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            let work_scheduler = work_scheduler.clone();
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
    work_scheduler: Arc<WorkScheduler>,
}

impl WorkSchedulerCapabilityProvider {
    fn new(realm: Arc<Realm>, work_scheduler: Arc<WorkScheduler>) -> Self {
        WorkSchedulerCapabilityProvider { realm, work_scheduler }
    }

    /// Service `open` invocation via an event loop that dispatches FIDL operations to
    /// `work_scheduler`.
    async fn open_async(
        work_scheduler: Arc<WorkScheduler>,
        realm: Arc<Realm>,
        mut stream: fsys::WorkSchedulerRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            let work_scheduler = work_scheduler.clone();
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
