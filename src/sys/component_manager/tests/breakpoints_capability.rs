// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cm_rust::FrameworkCapabilityDecl,
    component_manager_lib::{
        framework::FrameworkCapability, model::testing::breakpoints::*, model::*,
    },
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_test_breakpoints as fbreak, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{future::BoxFuture, StreamExt},
    lazy_static::lazy_static,
    std::{convert::TryInto, sync::Arc},
};

lazy_static! {
    pub static ref BREAKPOINTS_SERVICE: cm_rust::CapabilityPath =
        "/svc/fuchsia.test.breakpoints.Breakpoints".try_into().unwrap();
}

pub struct BreakpointCapabilityHook {
    breakpoint_capability_hook_inner: Arc<BreakpointCapabilityHookInner>,
}

impl BreakpointCapabilityHook {
    pub fn new(breakpoint_registry: Arc<BreakpointRegistry>) -> Self {
        Self {
            breakpoint_capability_hook_inner: Arc::new(BreakpointCapabilityHookInner::new(
                breakpoint_registry,
            )),
        }
    }

    pub fn hooks(&self) -> Vec<HookRegistration> {
        vec![HookRegistration {
            event_type: EventType::RouteFrameworkCapability,
            callback: self.breakpoint_capability_hook_inner.clone(),
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
        capability_decl: &FrameworkCapabilityDecl,
        capability: Option<Box<dyn FrameworkCapability>>,
    ) -> Result<Option<Box<dyn FrameworkCapability>>, ModelError> {
        match (capability, capability_decl) {
            (None, FrameworkCapabilityDecl::LegacyService(source_path))
                if *source_path == *BREAKPOINTS_SERVICE =>
            {
                return Ok(Some(
                    Box::new(BreakpointCapability::new(self.breakpoint_registry.clone()))
                        as Box<dyn FrameworkCapability>,
                ))
            }
            (c, _) => return Ok(c),
        };
    }
}

impl Hook for BreakpointCapabilityHookInner {
    fn on(self: Arc<Self>, event: &Event) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(async move {
            if let Event::RouteFrameworkCapability { realm: _, capability_decl, capability } = event
            {
                let mut capability = capability.lock().await;
                *capability = self
                    .on_route_framework_capability_async(capability_decl, capability.take())
                    .await?;
            }
            Ok(())
        })
    }
}

/// Server end of Breakpoints framework service.
/// A component can use FIDL calls to register breakpoints and expect/resume invocations.
pub struct BreakpointCapability {
    breakpoint_registry: Arc<BreakpointRegistry>,
}

impl BreakpointCapability {
    fn new(breakpoint_registry: Arc<BreakpointRegistry>) -> Self {
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
                fbreak::BreakpointsRequest::Expect { event_type, components, responder } => {
                    // Wait for the next breakpoint to occur
                    let invocation = receiver.receive().await;

                    // Ensure that the breakpoint is as expected
                    verify_invocation(&invocation, event_type, components);

                    // Unblock the component
                    responder.send().unwrap();

                    Some(invocation)
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
    async fn looper(server_end: zx::Channel, breakpoint_registry: Arc<BreakpointRegistry>) {
        let mut stream = ServerEnd::<fbreak::BreakpointsMarker>::new(server_end)
            .into_stream()
            .expect("could not convert channel into stream");

        let receiver = Self::register_breakpoints(&mut stream, &breakpoint_registry).await;

        while let Some(invocation) = Self::expect_invocation(&mut stream, &receiver).await {
            Self::resume_invocation(&mut stream, invocation).await;
        }
    }

    async fn open_async(&self, server_end: zx::Channel) -> Result<(), ModelError> {
        let breakpoint_registry = self.breakpoint_registry.clone();
        fasync::spawn(async move {
            Self::looper(server_end, breakpoint_registry).await;
        });
        Ok(())
    }
}

impl FrameworkCapability for BreakpointCapability {
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
        fbreak::EventType::StopInstance => EventType::StopInstance,
        fbreak::EventType::PreDestroyInstance => EventType::PreDestroyInstance,
        fbreak::EventType::PostDestroyInstance => EventType::PostDestroyInstance,
    }
}

fn verify_invocation(
    invocation: &BreakpointInvocation,
    expected_event_type: fbreak::EventType,
    expected_components: Vec<String>,
) {
    let expected_event_type = convert_event_type(expected_event_type);
    let expected_components: Vec<&str> =
        expected_components.iter().map(|component| component.as_ref()).collect();
    let expected_moniker = AbsoluteMoniker::from(expected_components);
    let actual_moniker = invocation.event.target_realm().abs_moniker.clone();
    let actual_event_type = invocation.event.type_();
    assert_eq!(actual_moniker, expected_moniker);
    assert_eq!(actual_event_type, expected_event_type);
}
