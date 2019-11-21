// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{capability::*, model::testing::breakpoints::*, model::*},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_test_breakpoints as fbreak, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{future::BoxFuture, StreamExt},
    lazy_static::lazy_static,
    std::{
        convert::TryInto,
        sync::{Arc, Weak},
    },
};

lazy_static! {
    pub static ref BREAKPOINTS_SERVICE: cm_rust::CapabilityPath =
        "/svc/fuchsia.test.breakpoints.Breakpoints".try_into().unwrap();
}

pub struct BreakpointCapabilityHook {
    inner: Arc<BreakpointCapabilityHookInner>,
}

impl BreakpointCapabilityHook {
    pub fn new(breakpoint_registry: Arc<BreakpointRegistry>) -> Self {
        Self { inner: Arc::new(BreakpointCapabilityHookInner::new(breakpoint_registry)) }
    }

    pub fn hooks(&self) -> Vec<HooksRegistration> {
        vec![HooksRegistration {
            events: vec![EventType::RouteFrameworkCapability],
            callback: Arc::downgrade(&self.inner) as Weak<dyn Hook>,
        }]
    }
}

/// A hook that provides the Breakpoints framework service to components.
struct BreakpointCapabilityHookInner {
    breakpoint_registry: Arc<BreakpointRegistry>,
}

impl BreakpointCapabilityHookInner {
    fn new(breakpoint_registry: Arc<BreakpointRegistry>) -> Self {
        Self { breakpoint_registry }
    }

    /// Creates and returns a BreakpointCapability when a component uses
    /// the Breakpoint framework service
    async fn on_route_framework_capability_async(
        self: Arc<Self>,
        capability_decl: &ComponentManagerCapability,
        capability: Option<Box<dyn ComponentManagerCapabilityProvider>>,
    ) -> Result<Option<Box<dyn ComponentManagerCapabilityProvider>>, ModelError> {
        match (capability, capability_decl) {
            (None, ComponentManagerCapability::LegacyService(source_path))
                if *source_path == *BREAKPOINTS_SERVICE =>
            {
                return Ok(Some(Box::new(BreakpointCapabilityProvider::new(
                    self.breakpoint_registry.clone(),
                )) as Box<dyn ComponentManagerCapabilityProvider>))
            }
            (c, _) => return Ok(c),
        };
    }
}

impl Hook for BreakpointCapabilityHookInner {
    fn on(self: Arc<Self>, event: &Event) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(async move {
            if let Event::RouteFrameworkCapability { realm: _, capability, capability_provider } =
                event
            {
                let mut capability_provider = capability_provider.lock().await;
                *capability_provider = self
                    .on_route_framework_capability_async(capability, capability_provider.take())
                    .await?;
            }
            Ok(())
        })
    }
}

/// Server end of Breakpoints framework service.
/// A component can use FIDL calls to register breakpoints and expect/resume invocations.
pub struct BreakpointCapabilityProvider {
    breakpoint_registry: Arc<BreakpointRegistry>,
}

impl BreakpointCapabilityProvider {
    pub fn new(breakpoint_registry: Arc<BreakpointRegistry>) -> Self {
        Self { breakpoint_registry }
    }

    /// Wait for a Register request and register breakpoints against the EventTypes in the request.
    async fn register_breakpoints(
        stream: &mut fbreak::BreakpointsRequestStream,
        breakpoint_registry: &Arc<BreakpointRegistry>,
    ) -> BreakpointInvocationReceiver {
        let request = stream.next().await.unwrap().unwrap();
        match request {
            fbreak::BreakpointsRequest::Register { event_types, responder } => {
                let event_types = event_types
                    .into_iter()
                    .map(|event_type| convert_event_type(event_type))
                    .collect();
                let receiver = breakpoint_registry.register(event_types).await;

                // Unblock the component
                responder.send().unwrap();

                receiver
            }
            _ => panic!("Did not receive FIDL call to Install"),
        }
    }

    /// Wait for an Expect request from the stream, wait for an invocation on the given receiver
    /// and verify that the invocation matches what was expected.
    async fn expect_invocation(
        stream: &mut fbreak::BreakpointsRequestStream,
        receiver: &BreakpointInvocationReceiver,
    ) -> Option<BreakpointInvocation> {
        if let Some(Ok(request)) = stream.next().await {
            match request {
                fbreak::BreakpointsRequest::Expect { event_type, component, responder } => {
                    // Wait for the next breakpoint to occur
                    let invocation = receiver.receive().await;

                    // Ensure that the breakpoint is as expected
                    verify_invocation(&invocation, event_type, component);

                    // Unblock the component
                    responder.send().unwrap();

                    Some(invocation)
                }
                fbreak::BreakpointsRequest::WaitUntilUseCapability {
                    component,
                    requested_capability_path,
                    responder,
                } => {
                    // Vec<String> to AbsoluteMoniker
                    let component: Vec<&str> = component.iter().map(|x| x.as_ref()).collect();
                    let component: AbsoluteMoniker = component.into();
                    loop {
                        // Keep looping until we get an UseCapability event with the correct
                        // capability path and component.

                        // Wait for the next invocation
                        let invocation = receiver.receive().await;

                        // Correct EventType?
                        if let Event::UseCapability { realm, use_ } = &invocation.event {
                            // Correct component?
                            if realm.abs_moniker == component {
                                // TODO(xbhatnag): Currently only service uses are sent as events
                                // Hence, the UseDecl must always have a CapabilityPath.
                                let actual_capability_path = use_
                                    .path()
                                    .expect("WaitUntilUseCapability: UseDecl must have path")
                                    .to_string();
                                // Correct capability path?
                                if requested_capability_path == actual_capability_path {
                                    // Unblock the component
                                    responder.send().unwrap();
                                    return Some(invocation);
                                }
                            }
                        }

                        // Wrong invocation. Resume this invocation.
                        invocation.resume();
                    }
                }
                _ => panic!("Did not receive FIDL call to Expect"),
            }
        } else {
            // There was an error getting the next request from the component.
            None
        }
    }

    /// Wait for a Resume request from the stream and resume from the given invocation
    async fn resume_invocation(
        stream: &mut fbreak::BreakpointsRequestStream,
        invocation: BreakpointInvocation,
    ) {
        // The next request must be a ResumeBreakpoint
        let request = stream.next().await.unwrap().unwrap();
        match request {
            fbreak::BreakpointsRequest::Resume { responder } => {
                invocation.resume();

                // Unblock the component
                responder.send().unwrap();
            }
            _ => panic!("Did not receive FIDL call to Resume"),
        }
    }

    /// Loops indefinitely, processing Breakpoint FIDL requests received over a channel
    ///
    /// root_realm_receiver is provided when the "--debug" flag is passed into component manager.
    /// It is a receiver for RootRealmCreated events. When this receiver is provided,
    /// component manager will halt until the Breakpoint FIDL service connects, sets up breakpoints
    /// and calls resume().
    async fn serve(
        mut stream: fbreak::BreakpointsRequestStream,
        breakpoint_registry: Arc<BreakpointRegistry>,
        root_realm_created_receiver: Option<BreakpointInvocationReceiver>,
    ) {
        let receiver = Self::register_breakpoints(&mut stream, &breakpoint_registry).await;

        // If the receiver is specified, wait for a resume to unpause the component manager.
        if let Some(receiver) = root_realm_created_receiver {
            let invocation = receiver.receive().await;
            Self::resume_invocation(&mut stream, invocation).await;
        }

        while let Some(invocation) = Self::expect_invocation(&mut stream, &receiver).await {
            Self::resume_invocation(&mut stream, invocation).await;
        }
    }

    pub fn serve_async(
        &self,
        stream: fbreak::BreakpointsRequestStream,
        root_realm_created_receiver: Option<BreakpointInvocationReceiver>,
    ) -> Result<(), ModelError> {
        let breakpoint_registry = self.breakpoint_registry.clone();
        fasync::spawn(async move {
            Self::serve(stream, breakpoint_registry, root_realm_created_receiver).await;
        });
        Ok(())
    }

    /// This function is usually called when components inside component manager have been
    /// routed to the BreakpointCapability service. There is no root_realm_created_receiver
    /// here because the root realm must already have been created by this point.
    async fn open_async(&self, server_end: zx::Channel) -> Result<(), ModelError> {
        let stream = ServerEnd::<fbreak::BreakpointsMarker>::new(server_end)
            .into_stream()
            .expect("could not convert channel into stream");
        self.serve_async(stream, None)
    }
}

impl ComponentManagerCapabilityProvider for BreakpointCapabilityProvider {
    fn open(
        &self,
        _flags: u32,
        _open_mode: u32,
        _relative_path: String,
        server_chan: zx::Channel,
    ) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(self.open_async(server_chan))
    }
}

fn convert_event_type(event_type: fbreak::EventType) -> EventType {
    match event_type {
        fbreak::EventType::StartInstance => EventType::StartInstance,
        fbreak::EventType::StopInstance => EventType::StopInstance,
        fbreak::EventType::PreDestroyInstance => EventType::PreDestroyInstance,
        fbreak::EventType::PostDestroyInstance => EventType::PostDestroyInstance,
        fbreak::EventType::UseCapability => EventType::UseCapability,
    }
}

fn verify_invocation(
    invocation: &BreakpointInvocation,
    expected_event_type: fbreak::EventType,
    expected_component: Vec<String>,
) {
    let expected_event_type = convert_event_type(expected_event_type);
    let expected_component: Vec<&str> =
        expected_component.iter().map(|component| component.as_ref()).collect();
    let expected_moniker = AbsoluteMoniker::from(expected_component);
    let actual_moniker = invocation.event.target_realm().abs_moniker.clone();
    let actual_event_type = invocation.event.type_();
    assert_eq!(actual_moniker, expected_moniker);
    assert_eq!(actual_event_type, expected_event_type);
}
