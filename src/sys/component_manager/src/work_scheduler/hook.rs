// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, InternalCapability},
        channel,
        model::{
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            moniker::AbsoluteMoniker,
            routing::RoutingError,
        },
        work_scheduler::work_scheduler::{
            WorkScheduler, WORK_SCHEDULER_CAPABILITY_NAME, WORK_SCHEDULER_CONTROL_CAPABILITY_NAME,
        },
    },
    anyhow::Error,
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::TryStreamExt,
    log::warn,
    std::{
        path::PathBuf,
        sync::{Arc, Weak},
    },
};

// TODO(markdittmer): Establish
// WorkSchedulerSystem -> (WorkScheduler, WorkSchedulerHook -> WorkScheduler).
impl WorkScheduler {
    /// Helper to specify hooks associated with `WorkScheduler`. Accepts `&Arc<WorkScheduler>` to
    /// produce references needed by `HooksRegistration` without consuming `Arc`.
    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "WorkScheduler",
            vec![EventType::Resolved, EventType::CapabilityRouted],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    /// Route capability to access `fuchsia.sys2.WorkSchedulerControl` protocol as a framework
    /// capability.
    async fn on_framework_capability_routed_async<'a>(
        self: Arc<Self>,
        capability: &'a InternalCapability,
        capability_provider: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        if capability_provider.is_none()
            && capability.matches_protocol(&WORK_SCHEDULER_CONTROL_CAPABILITY_NAME)
        {
            Ok(Some(Box::new(WorkSchedulerControlCapabilityProvider::new(self.clone()))
                as Box<dyn CapabilityProvider>))
        } else {
            Ok(capability_provider)
        }
    }

    /// Route capability to access `fuchsia.sys2.WorkScheduler` protocol as a scoped framework
    /// capability.
    async fn on_scoped_framework_capability_routed_async<'a>(
        self: Arc<Self>,
        scope_moniker: AbsoluteMoniker,
        capability: &'a InternalCapability,
        capability_provider: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        if capability_provider.is_none()
            && capability.matches_protocol(&WORK_SCHEDULER_CAPABILITY_NAME)
        {
            // Only clients that expose the Worker protocol to the framework can
            // use WorkScheduler.
            if !self.verify_worker_exposed_to_framework(&scope_moniker).await {
                return Err(RoutingError::used_expose_not_found(
                    &scope_moniker,
                    WORK_SCHEDULER_CAPABILITY_NAME.to_string(),
                )
                .into());
            }
            Ok(Some(Box::new(WorkSchedulerCapabilityProvider::new(scope_moniker, self.clone()))
                as Box<dyn CapabilityProvider>))
        } else {
            Ok(capability_provider)
        }
    }
}

#[async_trait]
impl Hook for WorkScheduler {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        match &event.result {
            Ok(EventPayload::CapabilityRouted {
                source: CapabilitySource::Builtin { capability },
                capability_provider,
            }) => {
                let mut capability_provider = capability_provider.lock().await;
                *capability_provider = self
                    .on_framework_capability_routed_async(&capability, capability_provider.take())
                    .await?;
            }
            Ok(EventPayload::CapabilityRouted {
                source: CapabilitySource::Framework { capability, scope_moniker },
                capability_provider,
            }) => {
                let mut capability_provider = capability_provider.lock().await;
                *capability_provider = self
                    .on_scoped_framework_capability_routed_async(
                        scope_moniker.clone(),
                        &capability,
                        capability_provider.take(),
                    )
                    .await?;
            }
            Ok(EventPayload::Resolved { decl }) => {
                self.try_add_realm_as_worker(&event.target_moniker, &decl).await;
            }
            _ => {}
        };
        Ok(())
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
        self,
        mut stream: fsys::WorkSchedulerControlRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            let work_scheduler = self.work_scheduler.clone();
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

#[async_trait]
impl CapabilityProvider for WorkSchedulerControlCapabilityProvider {
    /// Spawn an event loop to service `WorkScheduler` FIDL operations.
    async fn open(
        self: Box<Self>,
        _flags: u32,
        _open_mode: u32,
        _relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let server_end = channel::take_channel(server_end);
        let server_end = ServerEnd::<fsys::WorkSchedulerControlMarker>::new(server_end);
        let stream: fsys::WorkSchedulerControlRequestStream =
            server_end.into_stream().map_err(ModelError::stream_creation_error)?;
        fasync::Task::spawn(async move {
            let result = self.open_async(stream).await;
            if let Err(e) = result {
                // TODO(markdittmer): Set an epitaph to indicate this was an unexpected error.
                warn!("WorkSchedulerCapabilityProvider.open failed: {}", e);
            }
        })
        .detach();
        Ok(())
    }
}

/// `Capability` to invoke `WorkScheduler` FIDL API bound to a particular `WorkScheduler` object and
/// component instance's `AbsoluteMoniker`. All FIDL operations bound to the same object and moniker
/// observe the same collection of `WorkItem` objects.
struct WorkSchedulerCapabilityProvider {
    scope_moniker: AbsoluteMoniker,
    work_scheduler: Arc<WorkScheduler>,
}

impl WorkSchedulerCapabilityProvider {
    fn new(scope_moniker: AbsoluteMoniker, work_scheduler: Arc<WorkScheduler>) -> Self {
        WorkSchedulerCapabilityProvider { scope_moniker, work_scheduler }
    }

    /// Service `open` invocation via an event loop that dispatches FIDL operations to
    /// `work_scheduler`.
    async fn open_async(
        work_scheduler: Arc<WorkScheduler>,
        scope_moniker: AbsoluteMoniker,
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
                        work_scheduler.schedule_work(&scope_moniker, &work_id, &work_request).await;
                    responder.send(&mut result)?;
                }
                fsys::WorkSchedulerRequest::CancelWork { responder, work_id, .. } => {
                    let mut result = work_scheduler.cancel_work(&scope_moniker, &work_id).await;
                    responder.send(&mut result)?;
                }
            }
        }
        Ok(())
    }
}

#[async_trait]
impl CapabilityProvider for WorkSchedulerCapabilityProvider {
    /// Spawn an event loop to service `WorkScheduler` FIDL operations.
    async fn open(
        self: Box<Self>,
        _flags: u32,
        _open_mode: u32,
        _relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let server_end = channel::take_channel(server_end);
        let server_end = ServerEnd::<fsys::WorkSchedulerMarker>::new(server_end);
        let stream: fsys::WorkSchedulerRequestStream =
            server_end.into_stream().map_err(ModelError::stream_creation_error)?;
        let work_scheduler = self.work_scheduler.clone();
        let scope_moniker = self.scope_moniker.clone();
        fasync::Task::spawn(async move {
            let result = Self::open_async(work_scheduler, scope_moniker, stream).await;
            if let Err(e) = result {
                // TODO(markdittmer): Set an epitaph to indicate this was an unexpected error.
                warn!("WorkSchedulerCapabilityProvider.open failed: {}", e);
            }
        })
        .detach();
        Ok(())
    }
}
