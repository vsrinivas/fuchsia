// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, FrameworkCapability},
        model::{
            breakpoints::{
                registry::{BreakpointRegistry, InvocationReceiver},
                serve::serve_system,
            },
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            moniker::AbsoluteMoniker,
        },
    },
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_test_breakpoints as fbreak, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::future::BoxFuture,
    lazy_static::lazy_static,
    std::convert::TryInto,
    std::sync::{Arc, Weak},
};

lazy_static! {
    pub static ref BREAKPOINT_SYSTEM_SERVICE: cm_rust::CapabilityPath =
        "/svc/fuchsia.test.breakpoints.BreakpointSystem".try_into().unwrap();
}

pub struct BreakpointSystem {
    inner: Arc<BreakpointSystemInner>,
}

impl BreakpointSystem {
    pub fn new() -> Self {
        Self { inner: BreakpointSystemInner::new() }
    }

    pub fn hooks(&self) -> Vec<HooksRegistration> {
        vec![
            // This hook must be registered with all events.
            // However, a task will only receive events that it registered breakpoints for.
            HooksRegistration::new(
                "BreakpointRegistry",
                vec![
                    EventType::AddDynamicChild,
                    EventType::BeforeStartInstance,
                    EventType::PostDestroyInstance,
                    EventType::PreDestroyInstance,
                    EventType::ResolveInstance,
                    EventType::RouteCapability,
                    EventType::StopInstance,
                ],
                Arc::downgrade(&self.inner.registry) as Weak<dyn Hook>,
            ),
            // This hook provides the Breakpoint capability to components in the tree
            HooksRegistration::new(
                "BreakpointSystem",
                vec![EventType::RouteCapability],
                Arc::downgrade(&self.inner) as Weak<dyn Hook>,
            ),
        ]
    }

    /// Creates a `ScopedBreakpointSystem` that only receives breakpoint invocations
    /// within the realm specified by `scope_moniker`.
    pub fn create_scope(&self, scope_moniker: AbsoluteMoniker) -> ScopedBreakpointSystem {
        ScopedBreakpointSystem::new(scope_moniker, self.inner.registry.clone())
    }
}

struct BreakpointSystemInner {
    registry: Arc<BreakpointRegistry>,
}

impl BreakpointSystemInner {
    pub fn new() -> Arc<Self> {
        Arc::new(Self { registry: Arc::new(BreakpointRegistry::new()) })
    }

    /// Creates and returns a ScopedBreakpointSystem when a component uses
    /// the Breakpoint framework service. A ScopedBreakpointSystem holds an
    /// AbsoluteMoniker that corresponds to the realm in which it will receive
    /// breakpoint invocations.
    async fn on_route_scoped_framework_capability_async(
        self: Arc<Self>,
        capability_decl: &FrameworkCapability,
        scope_moniker: AbsoluteMoniker,
        capability: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        match (capability, capability_decl) {
            (None, FrameworkCapability::ServiceProtocol(source_path))
                if *source_path == *BREAKPOINT_SYSTEM_SERVICE =>
            {
                let system = ScopedBreakpointSystem::new(scope_moniker, self.registry.clone());
                return Ok(Some(Box::new(system) as Box<dyn CapabilityProvider>));
            }
            (c, _) => return Ok(c),
        };
    }
}

impl Hook for BreakpointSystemInner {
    fn on(self: Arc<Self>, event: &Event) -> BoxFuture<'_, Result<(), ModelError>> {
        Box::pin(async move {
            if let EventPayload::RouteCapability {
                source:
                    CapabilitySource::Framework { capability, scope_moniker: Some(scope_moniker) },
                capability_provider,
            } = &event.payload
            {
                let mut capability_provider = capability_provider.lock().await;
                *capability_provider = self
                    .on_route_scoped_framework_capability_async(
                        &capability,
                        scope_moniker.clone(),
                        capability_provider.take(),
                    )
                    .await?;
            }
            Ok(())
        })
    }
}

/// A system responsible for implementing basic breakpoint functionality on a scoped realm.
/// If this object is dropped, there are no guarantees about breakpoint functionality.
#[derive(Clone)]
pub struct ScopedBreakpointSystem {
    scope_moniker: AbsoluteMoniker,
    registry: Arc<BreakpointRegistry>,
}

impl ScopedBreakpointSystem {
    pub fn new(scope_moniker: AbsoluteMoniker, registry: Arc<BreakpointRegistry>) -> Self {
        Self { scope_moniker, registry }
    }

    /// Sets breakpoints corresponding to the `events` vector. The breakpoints are scoped
    /// to this `ScopedBreakpointSystem`'s realm. In other words, this breakpoint system
    /// will only receive events that have a target moniker within the realm of this breakpoint
    /// system's scope.
    pub async fn set_breakpoints(&self, events: Vec<EventType>) -> InvocationReceiver {
        // Create a receiver for the given events
        self.registry.set_breakpoints(self.scope_moniker.clone(), events.clone()).await
    }

    /// Serves a `BreakpointSystem` FIDL protocol. `wait_for_root` indicates whether or not
    /// this `ScopedBreakpointSystem` should set a `ResolveInstance` breakpoint. This is only
    /// relevant for the root `ScopedBreakpointSystem`. The `ResolveInstance` breakpoint on the
    /// root component permits integration tests to install additional breakpoints before
    /// the root component starts to avoid races.
    pub fn serve_async(self, stream: fbreak::BreakpointSystemRequestStream, wait_for_root: bool) {
        fasync::spawn(async move {
            let root_instance_resolved_receiver = if self.scope_moniker.is_root() && wait_for_root {
                Some(self.set_breakpoints(vec![EventType::ResolveInstance]).await)
            } else {
                None
            };
            serve_system(self, stream, root_instance_resolved_receiver).await;
        });
    }
}

#[async_trait]
impl CapabilityProvider for ScopedBreakpointSystem {
    async fn open(
        self: Box<Self>,
        _flags: u32,
        _open_mode: u32,
        _relative_path: String,
        server_end: zx::Channel,
    ) -> Result<(), ModelError> {
        let stream = ServerEnd::<fbreak::BreakpointSystemMarker>::new(server_end)
            .into_stream()
            .expect("could not convert channel into stream");
        self.serve_async(stream, false);
        Ok(())
    }
}
