// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{capability::*, model::testing::breakpoints::*, model::*},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_test_breakpoints as fbreak, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{future::BoxFuture, StreamExt},
    lazy_static::lazy_static,
    std::{convert::TryInto, sync::Arc},
};

lazy_static! {
    pub static ref BREAKPOINT_SYSTEM_SERVICE: cm_rust::CapabilityPath =
        "/svc/fuchsia.test.breakpoints.BreakpointSystem".try_into().unwrap();
}

/// A hook that provides the Breakpoints framework service to components.
pub struct BreakpointCapabilityHook {
    breakpoint_registry: Arc<BreakpointRegistry>,
}

impl BreakpointCapabilityHook {
    pub fn new(breakpoint_registry: Arc<BreakpointRegistry>) -> Self {
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
                if *source_path == *BREAKPOINT_SYSTEM_SERVICE =>
            {
                return Ok(Some(Box::new(BreakpointCapabilityProvider::new(
                    self.breakpoint_registry.clone(),
                )) as Box<dyn ComponentManagerCapabilityProvider>))
            }
            (c, _) => return Ok(c),
        };
    }
}

impl Hook for BreakpointCapabilityHook {
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

    /// Serves the BreakpointSystem FIDL service over the provided stream.
    /// A root_realm_created_receiver is provided when component manager
    /// is in debug mode.
    pub fn serve_async(
        &self,
        stream: fbreak::BreakpointSystemRequestStream,
        root_realm_created_receiver: Option<InvocationReceiver>,
    ) -> Result<(), ModelError> {
        let breakpoint_registry = self.breakpoint_registry.clone();
        fasync::spawn(async move {
            serve_system(stream, breakpoint_registry, root_realm_created_receiver).await;
        });
        Ok(())
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
        let stream = ServerEnd::<fbreak::BreakpointSystemMarker>::new(server_chan)
            .into_stream()
            .expect("could not convert channel into stream");
        Box::pin(async move { self.serve_async(stream, None) })
    }
}

/// Serves BreakpointSystem FIDL requests received over the provided stream.
///
/// root_realm_receiver is provided when the "--debug" flag is passed into component manager.
/// When this receiver is provided, component manager has been halted on a RootRealmCreated event.
/// The first call to BreakpointSystem::Register will also unblock component manager
/// using the provided receiver.
async fn serve_system(
    mut stream: fbreak::BreakpointSystemRequestStream,
    breakpoint_registry: Arc<BreakpointRegistry>,
    root_realm_created_receiver: Option<InvocationReceiver>,
) {
    // The first call to register is special.
    if let Some(Ok(fbreak::BreakpointSystemRequest::Register {
        event_types,
        server_end,
        responder,
    })) = stream.next().await
    {
        serve_fidl_receiver_async(&breakpoint_registry, event_types, server_end).await;

        // Unblock component manager if the receiver is provided
        if let Some(receiver) = root_realm_created_receiver {
            let invocation = receiver.receive().await;
            invocation.resume();
        }

        // Unblock the component
        responder.send().unwrap();

        // Continue to accept registration requests
        while let Some(Ok(fbreak::BreakpointSystemRequest::Register {
            event_types,
            server_end,
            responder,
        })) = stream.next().await
        {
            serve_fidl_receiver_async(&breakpoint_registry, event_types, server_end).await;

            // Unblock the component
            responder.send().unwrap();
        }
    }
}

/// Registers the given event types, then serves the server end of the
/// InvocationReceiver FIDL protocol asynchronously.
async fn serve_fidl_receiver_async(
    breakpoint_registry: &Arc<BreakpointRegistry>,
    event_types: Vec<fbreak::EventType>,
    server_end: ServerEnd<fbreak::InvocationReceiverMarker>,
) {
    let event_types =
        event_types.into_iter().map(|event_type| convert_event_type(event_type)).collect();
    let receiver = breakpoint_registry.register(event_types).await;
    let stream = server_end.into_stream().unwrap();
    fasync::spawn(async move {
        serve_receiver(receiver, stream).await;
    });
}

/// Serves InvocationReceiver FIDL requests received over the provided stream.
async fn serve_receiver(
    receiver: InvocationReceiver,
    mut stream: fbreak::InvocationReceiverRequestStream,
) {
    while let Some(Ok(request)) = stream.next().await {
        match request {
            fbreak::InvocationReceiverRequest::Expect {
                event_type,
                component,
                server_end,
                responder,
            } => {
                let invocation = expect_invocation(&receiver, event_type, Some(component)).await;
                serve_fidl_invocation_async(invocation, server_end);
                responder.send().unwrap();
            }
            fbreak::InvocationReceiverRequest::ExpectType { event_type, server_end, responder } => {
                let invocation = expect_invocation(&receiver, event_type, None).await;
                serve_fidl_invocation_async(invocation, server_end);
                responder.send().unwrap();
            }
            fbreak::InvocationReceiverRequest::WaitUntilUseCapability {
                component,
                requested_capability_path,
                server_end,
                responder,
            } => {
                let invocation =
                    wait_until_use_capability(&receiver, component, requested_capability_path)
                        .await;
                serve_fidl_invocation_async(invocation, server_end);
                responder.send().unwrap();
            }
        };
    }
}

/// Serves the server end of the Invocation FIDL protocol asynchronously.
fn serve_fidl_invocation_async(
    invocation: Invocation,
    server_end: ServerEnd<fbreak::InvocationMarker>,
) {
    let mut stream = server_end.into_stream().unwrap();
    fasync::spawn(async move {
        if let Some(Ok(fbreak::InvocationRequest::Resume { responder })) = stream.next().await {
            invocation.resume();
            responder.send().unwrap();
        }
    });
}

/// Blocks until a UseCapability invocation matching the specified component
/// and capability path. All other invocations are ignored.
async fn wait_until_use_capability(
    receiver: &InvocationReceiver,
    component: Vec<String>,
    requested_capability_path: String,
) -> Invocation {
    // Vec<String> to AbsoluteMoniker
    let component = create_moniker(component);
    loop {
        // Keep looping until we get a UseCapability event with the correct
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
                    return invocation;
                }
            }
        }

        // Wrong invocation. Resume this invocation.
        invocation.resume();
    }
}

/// Wait for an Expect request from the stream, wait for an invocation on the given receiver
/// and verify that the invocation matches what was expected.
async fn expect_invocation(
    receiver: &InvocationReceiver,
    event_type: fbreak::EventType,
    component: Option<Vec<String>>,
) -> Invocation {
    // Wait for the next breakpoint to occur
    let invocation = receiver.receive().await;

    // Ensure that the breakpoint is as expected
    verify_event_type(&invocation, event_type);

    // If the component is provided, ensure it is as expected
    if let Some(component) = component {
        verify_moniker(&invocation, component);
    }

    invocation
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

fn create_moniker(component: Vec<String>) -> AbsoluteMoniker {
    let component: Vec<&str> = component.iter().map(|component| component.as_ref()).collect();
    AbsoluteMoniker::from(component)
}

fn verify_moniker(invocation: &Invocation, expected_component: Vec<String>) {
    let expected_moniker = create_moniker(expected_component);
    let actual_moniker = invocation.event.target_realm().abs_moniker.clone();
    assert_eq!(actual_moniker, expected_moniker);
}

fn verify_event_type(invocation: &Invocation, expected_event_type: fbreak::EventType) {
    let expected_event_type = convert_event_type(expected_event_type);
    let actual_event_type = invocation.event.type_();
    assert_eq!(actual_event_type, expected_event_type);
}
