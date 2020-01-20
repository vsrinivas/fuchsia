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
    anyhow::format_err,
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_test_breakpoints as fbreak, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{future::BoxFuture, lock::Mutex},
    lazy_static::lazy_static,
    std::{
        collections::HashMap,
        convert::TryInto,
        sync::{Arc, Weak},
    },
};

lazy_static! {
    pub static ref BREAKPOINT_SYSTEM_SERVICE: cm_rust::CapabilityPath =
        "/svc/fuchsia.test.breakpoints.BreakpointSystem".try_into().unwrap();
}

pub struct BreakpointSystemFactory {
    inner: Arc<BreakpointSystemFactoryInner>,
}

impl BreakpointSystemFactory {
    pub fn new() -> Self {
        Self { inner: BreakpointSystemFactoryInner::new() }
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
                Arc::downgrade(&self.inner.breakpoint_registry) as Weak<dyn Hook>,
            ),
            // This hook provides the Breakpoint capability to components in the tree
            HooksRegistration::new(
                "BreakpointSystemFactory",
                vec![EventType::ResolveInstance, EventType::RouteCapability],
                Arc::downgrade(&self.inner) as Weak<dyn Hook>,
            ),
        ]
    }

    pub async fn create(&self, scope_moniker: Option<AbsoluteMoniker>) -> BreakpointSystem {
        self.inner.create(scope_moniker).await
    }
}

struct BreakpointSystemFactoryInner {
    breakpoint_system_registry: Mutex<HashMap<Option<AbsoluteMoniker>, BreakpointSystem>>,
    breakpoint_registry: Arc<BreakpointRegistry>,
}

impl BreakpointSystemFactoryInner {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            breakpoint_system_registry: Mutex::new(HashMap::new()),
            breakpoint_registry: Arc::new(BreakpointRegistry::new()),
        })
    }

    /// Creates a `BreakpointSystem` that only receives breakpoint invocations
    /// within the realm specified by `scope_moniker`.
    pub async fn create(&self, scope_moniker: Option<AbsoluteMoniker>) -> BreakpointSystem {
        BreakpointSystem::new(scope_moniker, self.breakpoint_registry.clone()).await
    }

    /// Creates and returns a BreakpointSystem when a component uses
    /// the Breakpoint framework service. A BreakpointSystem holds an
    /// AbsoluteMoniker that corresponds to the realm in which it will receive
    /// breakpoint invocations.
    async fn on_route_scoped_framework_capability_async(
        self: Arc<Self>,
        capability_decl: &FrameworkCapability,
        scope_moniker: AbsoluteMoniker,
        capability: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        match (capability, capability_decl) {
            (None, FrameworkCapability::Protocol(source_path))
                if *source_path == *BREAKPOINT_SYSTEM_SERVICE =>
            {
                let key = Some(scope_moniker.clone());
                let breakpoint_system_registry = self.breakpoint_system_registry.lock().await;
                if let Some(system) = breakpoint_system_registry.get(&key) {
                    return Ok(Some(Box::new(system.clone()) as Box<dyn CapabilityProvider>));
                } else {
                    return Err(ModelError::capability_discovery_error(format_err!(
                        "Unable to find BreakpointSystem in registry for {}",
                        scope_moniker
                    )));
                }
            }
            (c, _) => return Ok(c),
        };
    }
}

impl Hook for BreakpointSystemFactoryInner {
    fn on(self: Arc<Self>, event: &Event) -> BoxFuture<'_, Result<(), ModelError>> {
        Box::pin(async move {
            match &event.payload {
                EventPayload::RouteCapability {
                    source:
                        CapabilitySource::Framework { capability, scope_moniker: Some(scope_moniker) },
                    capability_provider,
                } => {
                    let mut capability_provider = capability_provider.lock().await;
                    *capability_provider = self
                        .on_route_scoped_framework_capability_async(
                            &capability,
                            scope_moniker.clone(),
                            capability_provider.take(),
                        )
                        .await?;
                }
                EventPayload::ResolveInstance { decl } => {
                    if decl.uses_protocol_from_framework(&BREAKPOINT_SYSTEM_SERVICE) {
                        let key = Some(event.target_moniker.clone());
                        let mut breakpoint_system_registry =
                            self.breakpoint_system_registry.lock().await;
                        assert!(!breakpoint_system_registry.contains_key(&key));
                        let breakpoint_system = self.create(key.clone()).await;
                        breakpoint_system_registry.insert(key, breakpoint_system);
                    }
                }
                _ => {}
            }
            Ok(())
        })
    }
}

/// A system responsible for implementing basic breakpoint functionality on a scoped realm.
/// If this object is dropped, there are no guarantees about breakpoint functionality.
#[derive(Clone)]
pub struct BreakpointSystem {
    scope_moniker: Option<AbsoluteMoniker>,
    registry: Arc<BreakpointRegistry>,
    resolve_instance_receiver: Arc<Mutex<Option<InvocationReceiver>>>,
}

impl BreakpointSystem {
    pub async fn new(
        scope_moniker: Option<AbsoluteMoniker>,
        registry: Arc<BreakpointRegistry>,
    ) -> Self {
        let resolve_instance_receiver = Arc::new(Mutex::new(Some(
            registry.set_breakpoints(scope_moniker.clone(), vec![EventType::ResolveInstance]).await,
        )));
        Self { scope_moniker, registry, resolve_instance_receiver }
    }

    /// Drops the `ResolveInstance` receiver, thereby permitting components within the
    /// realm to be started.
    pub async fn start_component_tree(&mut self) {
        let mut resolve_instance_receiver = self.resolve_instance_receiver.lock().await;
        *resolve_instance_receiver = None;
    }

    /// Sets breakpoints corresponding to the `events` vector. The breakpoints are scoped
    /// to this `BreakpointSystem`'s realm. In other words, this breakpoint system
    /// will only receive events that have a target moniker within the realm of this breakpoint
    /// system's scope.
    pub async fn set_breakpoints(&self, events: Vec<EventType>) -> InvocationReceiver {
        // Create a receiver for the given events
        self.registry.set_breakpoints(self.scope_moniker.clone(), events.clone()).await
    }

    /// Serves a `BreakpointSystem` FIDL protocol.
    pub fn serve_async(self, stream: fbreak::BreakpointSystemRequestStream) {
        fasync::spawn(async move {
            serve_system(self, stream).await;
        });
    }

    // Returns an AbsoluteMoniker corresponding to the scope of this breakpoint system.
    pub fn scope(&self) -> Option<AbsoluteMoniker> {
        self.scope_moniker.clone()
    }
}

#[async_trait]
impl CapabilityProvider for BreakpointSystem {
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
        self.serve_async(stream);
        Ok(())
    }
}
