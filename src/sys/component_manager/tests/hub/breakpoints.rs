// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cm_rust::FrameworkCapabilityDecl,
    component_manager_lib::{framework::FrameworkCapability, model::*},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_test_hub as fhub, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{channel::*, future::BoxFuture, lock::Mutex, sink::SinkExt, StreamExt},
    lazy_static::lazy_static,
    std::{collections::HashMap, convert::TryInto, sync::Arc},
};

lazy_static! {
    pub static ref BREAKPOINTS_SERVICE: cm_rust::CapabilityPath =
        "/svc/fuchsia.test.hub.Breakpoints".try_into().unwrap();
}

/// Created for a particular invocation of a breakpoint in component manager.
/// Contains the Event that occurred along with a means to resume/unblock the component manager.
pub struct BreakpointInvocation {
    pub event: Event,

    // This Sender is used to unblock the component manager.
    responder: oneshot::Sender<()>,
}

impl BreakpointInvocation {
    pub fn resume(self) {
        self.responder.send(()).unwrap()
    }
}

/// BreakpointInvocationSender and BreakpointInvocationReceiver are two ends of a channel
/// used in the implementation of a breakpoint.
///
/// A BreakpointInvocationSender is owned by the BreakpointRegistry. It sends a
/// BreakpointInvocation to the BreakpointInvocationReceiver.
///
/// A BreakpointInvocationReceiver is owned by the client - usually a test harness or a
/// BreakpointCapability. It receives a BreakpointInvocation from a BreakpointInvocationSender
/// and propagates it to the client.
#[derive(Clone)]
pub struct BreakpointInvocationSender {
    tx: Arc<Mutex<mpsc::Sender<BreakpointInvocation>>>,
}

impl BreakpointInvocationSender {
    fn new(tx: mpsc::Sender<BreakpointInvocation>) -> Self {
        Self { tx: Arc::new(Mutex::new(tx)) }
    }

    /// Sends the event to a receiver. Returns a responder which can be blocked on.
    async fn send(&self, event: Event) -> Result<oneshot::Receiver<()>, ModelError> {
        let (responder_tx, responder_rx) = oneshot::channel();
        {
            let mut tx = self.tx.lock().await;
            tx.send(BreakpointInvocation { event, responder: responder_tx }).await.unwrap();
        }
        Ok(responder_rx)
    }
}

pub struct BreakpointInvocationReceiver {
    rx: Mutex<mpsc::Receiver<BreakpointInvocation>>,
}

impl BreakpointInvocationReceiver {
    fn new(rx: mpsc::Receiver<BreakpointInvocation>) -> Self {
        Self { rx: Mutex::new(rx) }
    }

    /// Receives an invocation from the sender.
    pub async fn receive(&self) -> BreakpointInvocation {
        let mut rx = self.rx.lock().await;
        rx.next().await.expect("Breakpoint did not occur")
    }
}

/// Registers breakpoints from multiple tasks and sends events to all of them.
pub struct BreakpointRegistry {
    sender_map: Arc<Mutex<HashMap<EventType, Vec<BreakpointInvocationSender>>>>
}

impl BreakpointRegistry {
    pub fn new() -> Self {
        Self { sender_map: Arc::new(Mutex::new(HashMap::new())) }
    }

    /// Registers breakpoints against a set of EventTypes.
    pub async fn register(&self, event_types: Vec<EventType>) -> BreakpointInvocationReceiver {
        let (tx, rx) = mpsc::channel(0);
        let sender = BreakpointInvocationSender::new(tx);
        let receiver = BreakpointInvocationReceiver::new(rx);

        let mut sender_map = self.sender_map.lock().await;
        for event_type in event_types {
            let senders = sender_map.entry(event_type).or_insert(vec![]);
            senders.push(sender.clone());
        }

        receiver
    }

    /// Sends the event to all registered breakpoints and waits to be unblocked by all
    async fn send(&self, event: &Event) -> Result<(), ModelError> {
        let sender_map = self.sender_map.lock().await;

        // If this EventType has senders installed against it, then
        // send this event via those senders.
        if let Some(senders) = sender_map.get(&event.type_()) {
            let mut responder_channels = vec![];
            for sender in senders {
                let responder_channel = sender.send(event.clone()).await?;
                responder_channels.push(responder_channel);
            }

            // Wait until all tasks have used the responder to unblock.
            futures::future::join_all(responder_channels).await;
        }
        Ok(())
    }
}

/// A hook registered with all lifecycle events. When any event is received from
/// component manager, a list of BreakpointInvocationSenders are obtained from the
/// BreakpointRegistry and a BreakpointInvocation is sent via each.
pub struct BreakpointHook {
    breakpoint_registry: Arc<BreakpointRegistry>
}

impl BreakpointHook {
    pub fn new(breakpoint_registry: Arc<BreakpointRegistry>) -> Self {
        Self { breakpoint_registry }
    }

    /// This hook must be registered with all events.
    /// However, a task will only receive events that it registered breakpoints for.
    pub fn hooks(self: Arc<Self>) -> Vec<HookRegistration> {
        vec![
            HookRegistration {
                event_type: EventType::AddDynamicChild,
                callback: self.clone(),
            },
            HookRegistration {
                event_type: EventType::BindInstance,
                callback: self.clone(),
            },
            HookRegistration {
                event_type: EventType::PostDestroyInstance,
                callback: self.clone(),
            },
            HookRegistration {
                event_type: EventType::PreDestroyInstance,
                callback: self.clone(),
            },
            HookRegistration {
                event_type: EventType::RouteFrameworkCapability,
                callback: self.clone(),
            },
            HookRegistration {
                event_type: EventType::StopInstance,
                callback: self.clone(),
            },
        ]
    }
}

impl Hook for BreakpointHook {
    fn on(self: Arc<Self>, event: &Event) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(async move {
            self.breakpoint_registry.send(event).await?;
            Ok(())
        })
    }
}

/// A hook that provides the Breakpoints framework service to components.
pub struct BreakpointCapabilityHook {
    breakpoint_registry: Arc<BreakpointRegistry>
}

impl BreakpointCapabilityHook {
    pub fn new(breakpoint_registry: Arc<BreakpointRegistry>) -> Self {
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
                return Ok(Some(Box::new(BreakpointCapability::new(self.breakpoint_registry.clone()))
                    as Box<dyn FrameworkCapability>))
            }
            (c, _) => return Ok(c),
        };
    }

    pub fn hooks(self: Arc<Self>) -> Vec<HookRegistration> {
        vec![
            HookRegistration {
                event_type: EventType::RouteFrameworkCapability,
                callback: self.clone(),
            }
        ]
    }
}

impl Hook for BreakpointCapabilityHook {
    fn on(self: Arc<Self>, event: &Event) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(async move {
            if let Event::RouteFrameworkCapability { realm: _, capability_decl, capability } = event {
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
        stream: &mut fhub::BreakpointsRequestStream,
        breakpoint_registry: &Arc<BreakpointRegistry>,
    ) -> BreakpointInvocationReceiver {
        let request = stream.next().await.unwrap().unwrap();
        match request {
            fhub::BreakpointsRequest::Register { event_types, responder } => {
                let event_types = event_types.into_iter().map(|event_type| convert_event_type(event_type)).collect();
                let receiver = breakpoint_registry.register(event_types).await;

                // Unblock the component
                responder.send().unwrap();

                receiver
            }
            _ => panic!("Did not receive FIDL call to Install")
        }
    }

    /// Wait for an Expect request from the stream, wait for an invocation on the given receiver
    /// and verify that the invocation matches what was expected.
    async fn expect_invocation(
        stream: &mut fhub::BreakpointsRequestStream,
        receiver: &BreakpointInvocationReceiver
    ) -> Option<BreakpointInvocation> {
        if let Some(Ok(request)) = stream.next().await {
            match request {
                fhub::BreakpointsRequest::Expect { event_type, components, responder } => {
                    // Wait for the next breakpoint to occur
                    let invocation = receiver.receive().await;

                    // Ensure that the breakpoint is as expected
                    verify_invocation(&invocation, event_type, components);

                    // Unblock the component
                    responder.send().unwrap();

                    Some(invocation)
                }
                _ => panic!("Did not receive FIDL call to Expect")
            }
        } else {
            // There was an error getting the next request from the component.
            None
        }
    }

    /// Wait for a Resume request from the stream and resume from the given invocation
    async fn resume_invocation(
        stream: &mut fhub::BreakpointsRequestStream,
        invocation: BreakpointInvocation
    ) {
        // The next request must be a ResumeBreakpoint
        let request = stream.next().await.unwrap().unwrap();
        match request {
            fhub::BreakpointsRequest::Resume { responder } => {
                invocation.resume();

                // Unblock the component
                responder.send().unwrap();
            }
            _ => panic!("Did not receive FIDL call to Resume")
        }
    }

    /// Loops indefinitely, processing Breakpoint FIDL requests received over a channel
    async fn looper(server_end: zx::Channel, breakpoint_registry: Arc<BreakpointRegistry>) {
        let mut stream = ServerEnd::<fhub::BreakpointsMarker>::new(server_end)
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

fn convert_event_type(event_type: fhub::EventType) -> EventType {
    match event_type {
        fhub::EventType::StopInstance => EventType::StopInstance,
        fhub::EventType::PreDestroyInstance => EventType::PreDestroyInstance,
        fhub::EventType::PostDestroyInstance => EventType::PostDestroyInstance,
    }
}

fn verify_invocation(
    invocation: &BreakpointInvocation,
    expected_event_type: fhub::EventType,
    expected_components: Vec<String>
) {
    let expected_event_type = convert_event_type(expected_event_type);
    let expected_components : Vec<&str> = expected_components.iter().map(|component| component.as_ref()).collect();
    let expected_moniker = AbsoluteMoniker::from(expected_components);
    let actual_moniker = invocation.event.target_realm().abs_moniker.clone();
    let actual_event_type = invocation.event.type_();
    assert_eq!(actual_moniker, expected_moniker);
    assert_eq!(actual_event_type, expected_event_type);
}