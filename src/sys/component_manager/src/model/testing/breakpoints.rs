// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    futures::{channel::*, future::BoxFuture, lock::Mutex, sink::SinkExt, StreamExt},
    lazy_static::lazy_static,
    std::{
        collections::HashMap,
        convert::TryInto,
        sync::{Arc, Weak},
    },
};

lazy_static! {
    pub static ref BREAKPOINTS_SERVICE: cm_rust::CapabilityPath =
        "/svc/fuchsia.test.breakpoints.Breakpoints".try_into().unwrap();
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

    /// Waits for an invocation with a particular EventType against a component with a
    /// particular moniker. Ignores all other invocations.
    pub async fn wait_until(
        &self,
        expected_event_type: EventType,
        expected_moniker: AbsoluteMoniker,
    ) -> BreakpointInvocation {
        loop {
            let invocation = self.receive().await;
            let actual_event_type = invocation.event.type_();
            let actual_moniker = invocation.event.target_realm().abs_moniker.clone();
            if expected_moniker == actual_moniker && expected_event_type == actual_event_type {
                return invocation;
            }
            invocation.resume();
        }
    }
}

/// Registers breakpoints from multiple tasks and sends events to all of them.
pub struct BreakpointRegistry {
    sender_map: Arc<Mutex<HashMap<EventType, Vec<BreakpointInvocationSender>>>>,
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
        // Copy the senders so we don't hold onto the sender map lock
        // If we didn't do this, it is possible to deadlock while holding onto this lock.
        // For example,
        // Task A : call send(event1) -> lock on sender map -> send -> wait for responders
        // Task B : call send(event2) -> lock on sender map
        // If task B was required to respond to event1, then this is a deadlock.
        // Neither task can make progress.
        let senders = {
            let sender_map = self.sender_map.lock().await;
            if let Some(senders) = sender_map.get(&event.type_()) {
                senders.clone()
            } else {
                // There were no senders for this event. Do nothing.
                return Ok(());
            }
        };

        let mut responder_channels = vec![];
        for sender in senders {
            let responder_channel = sender.send(event.clone()).await?;
            responder_channels.push(responder_channel);
        }

        // Wait until all tasks have used the responder to unblock.
        futures::future::join_all(responder_channels).await;
        Ok(())
    }
}

/// A hook registered with all lifecycle events. When any event is received from
/// component manager, a list of BreakpointInvocationSenders are obtained from the
/// BreakpointRegistry and a BreakpointInvocation is sent via each.
pub struct BreakpointHook {
    inner: Arc<BreakpointHookInner>,
}

impl BreakpointHook {
    pub fn new(breakpoint_registry: Arc<BreakpointRegistry>) -> Self {
        Self { inner: Arc::new(BreakpointHookInner::new(breakpoint_registry)) }
    }

    /// This hook must be registered with all events.
    /// However, a task will only receive events that it registered breakpoints for.
    pub fn hooks(&self) -> Vec<HooksRegistration> {
        vec![HooksRegistration {
            events: vec![
                EventType::AddDynamicChild,
                EventType::BindInstance,
                EventType::CapabilityUse,
                EventType::PostDestroyInstance,
                EventType::PreDestroyInstance,
                EventType::RouteFrameworkCapability,
                EventType::StopInstance,
            ],
            callback: Arc::downgrade(&self.inner) as Weak<dyn Hook>,
        }]
    }
}

struct BreakpointHookInner {
    breakpoint_registry: Arc<BreakpointRegistry>,
}

impl BreakpointHookInner {
    fn new(breakpoint_registry: Arc<BreakpointRegistry>) -> Self {
        Self { breakpoint_registry }
    }
}

impl Hook for BreakpointHookInner {
    fn on(self: Arc<Self>, event: &Event) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(async move {
            self.breakpoint_registry.send(event).await?;
            Ok(())
        })
    }
}
