// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        error::ModelError,
        hooks::{Event, EventType, Hook},
        moniker::AbsoluteMoniker,
    },
    anyhow::Error,
    fuchsia_trace as trace,
    futures::{channel::*, future::BoxFuture, lock::Mutex, sink::SinkExt, StreamExt},
    log::*,
    std::{collections::HashMap, sync::Arc},
};

/// Created for a particular invocation of a breakpoint in component manager.
/// Contains the Event that occurred along with a means to resume/unblock the component manager.
#[must_use = "invoke resume() otherwise component manager will be halted indefinitely!"]
pub struct Invocation {
    pub event: Event,

    // This Sender is used to unblock the component manager.
    responder: oneshot::Sender<()>,
}

impl Invocation {
    pub fn resume(self) {
        trace::duration!("component_manager", "breakpoints:resume");
        trace::flow_step!("component_manager", "event", self.event.id);
        self.responder.send(()).unwrap()
    }
}

/// InvocationSender and InvocationReceiver are two ends of a channel
/// used in the implementation of a breakpoint.
///
/// A InvocationSender is owned by the BreakpointRegistry. It sends a
/// BreakpointInvocation to the InvocationReceiver.
///
/// A InvocationReceiver is owned by the client - usually a test harness or a
/// ScopedBreakpointSystem. It receives a BreakpointInvocation from a InvocationSender
/// and propagates it to the client.
#[derive(Clone)]
pub struct InvocationSender {
    /// Specifies a realm that this InvocationSender can dispatch breakpoints from.
    scope_moniker: AbsoluteMoniker,
    /// An `mpsc::Sender` used to dispatch a breakpoint invocation. Note that this
    /// `mpsc::Sender` is wrapped in an Arc<Mutex<..>> to allow it to be cloneable
    /// and passed along to other tasks for dispatch.
    tx: Arc<Mutex<mpsc::Sender<Invocation>>>,
}

impl InvocationSender {
    fn new(scope_moniker: AbsoluteMoniker, tx: mpsc::Sender<Invocation>) -> Self {
        Self { scope_moniker, tx: Arc::new(Mutex::new(tx)) }
    }

    /// Sends the event to a receiver, if fired in the scope of `scope_moniker`. Returns
    /// a responder which can be blocked on.
    async fn send(&self, event: Event) -> Result<Option<oneshot::Receiver<()>>, Error> {
        if !self.scope_moniker.contains_in_realm(&event.target_moniker) {
            return Ok(None);
        }

        trace::duration!("component_manager", "breakpoints:send");
        let event_type = format!("{:?}", event.payload.type_());
        let target_moniker = event.target_moniker.to_string();
        trace::flow_begin!(
            "component_manager",
            "event",
            event.id,
            "event_type" => event_type.as_str(),
            "target_moniker" => target_moniker.as_str()
        );
        let (responder_tx, responder_rx) = oneshot::channel();
        {
            let mut tx = self.tx.lock().await;
            tx.send(Invocation { event, responder: responder_tx }).await?;
        }
        Ok(Some(responder_rx))
    }
}

pub struct InvocationReceiver {
    rx: mpsc::Receiver<Invocation>,
}

impl InvocationReceiver {
    fn new(rx: mpsc::Receiver<Invocation>) -> Self {
        Self { rx }
    }

    /// Receives the next invocation from the sender.
    pub async fn next(&mut self) -> Invocation {
        trace::duration!("component_manager", "breakpoints:next");
        let invocation = self.rx.next().await.expect("InvocationSender has closed the channel");
        trace::flow_step!("component_manager", "event", invocation.event.id);
        invocation
    }

    /// Waits for an invocation with a particular EventType against a component with a
    /// particular moniker. Ignores all other invocations.
    pub async fn wait_until(
        &mut self,
        expected_event_type: EventType,
        expected_moniker: AbsoluteMoniker,
    ) -> Invocation {
        loop {
            let invocation = self.next().await;
            let actual_event_type = invocation.event.payload.type_();
            if expected_moniker == invocation.event.target_moniker
                && expected_event_type == actual_event_type
            {
                return invocation;
            }
            invocation.resume();
        }
    }
}

/// Registers breakpoints from multiple tasks and sends events to all of them.
pub struct BreakpointRegistry {
    sender_map: Arc<Mutex<HashMap<EventType, Vec<InvocationSender>>>>,
}

impl BreakpointRegistry {
    pub fn new() -> Self {
        Self { sender_map: Arc::new(Mutex::new(HashMap::new())) }
    }

    /// Registers breakpoints against a set of EventTypes.
    pub async fn set_breakpoints(
        &self,
        scope_moniker: AbsoluteMoniker,
        event_types: Vec<EventType>,
    ) -> InvocationReceiver {
        let (tx, rx) = mpsc::channel(0);
        let sender = InvocationSender::new(scope_moniker, tx);
        let receiver = InvocationReceiver::new(rx);

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
            if let Some(senders) = sender_map.get(&event.payload.type_()) {
                senders.clone()
            } else {
                // There were no senders for this event. Do nothing.
                return Ok(());
            }
        };

        let mut responder_channels = vec![];
        for sender in senders {
            let result = sender.send(event.clone()).await;
            match result {
                Ok(Some(responder_channel)) => {
                    // A future can be canceled if the receiver was dropped after
                    // a send. We don't crash the system when this happens. It is
                    // perfectly valid for a receiver to be dropped. That simply
                    // means that the receiver is no longer interested in future
                    // events. So we force each future to return a success. This
                    // ensures that all the futures can be driven to completion.
                    let responder_channel = async move {
                        trace::duration!("component_manager", "breakpoints:wait_for_resume");
                        let result = responder_channel.await;
                        trace::flow_end!("component_manager", "event", event.id);
                        if let Err(error) = result {
                            warn!(
                                "A responder channel was canceled. This may \
                                 mean that an InvocationReceiver was dropped \
                                 unintentionally. Error -> {}",
                                error
                            );
                        }
                    };
                    responder_channels.push(responder_channel);
                }
                // There's nothing to do if event is outside the scope of the given
                // `InvocationSender`.
                Ok(None) => (),
                Err(error) => {
                    // A send can fail if the receiver was dropped. We don't
                    // crash the system when this happens. It is perfectly
                    // valid for a receiver to be dropped. That simply means
                    // that the receiver is no longer interested in future
                    // events.
                    warn!(
                        "Failed to send event via InvocationSender. \
                         This may mean that an InvocationReceiver was dropped \
                         unintentionally. Error -> {}",
                        error
                    );
                }
            }
        }

        // Wait until all tasks have used the responder to unblock.
        {
            trace::duration!("component_manager", "breakpoints:wait_for_all_resume");
            futures::future::join_all(responder_channels).await;
        }

        Ok(())
    }
}

impl Hook for BreakpointRegistry {
    fn on(self: Arc<Self>, event: &Event) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(async move {
            self.send(event).await?;
            Ok(())
        })
    }
}
