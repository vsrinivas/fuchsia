// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        error::ModelError,
        hooks::{Event as ComponentEvent, EventType, Hook, HooksRegistration},
        moniker::AbsoluteMoniker,
    },
    anyhow::Error,
    async_trait::async_trait,
    fuchsia_trace as trace,
    futures::{channel::*, lock::Mutex, sink::SinkExt, StreamExt},
    std::{
        collections::HashMap,
        sync::{Arc, Weak},
    },
};

/// Created for a particular component event.
/// Contains the Event that occurred along with a means to resume/unblock the component manager.
#[must_use = "invoke resume() otherwise component manager will be halted indefinitely!"]
pub struct Event {
    pub event: ComponentEvent,

    // This Sender is used to unblock the component manager.
    responder: oneshot::Sender<()>,
}

impl Event {
    pub fn resume(self) {
        trace::duration!("component_manager", "events:resume");
        trace::flow_step!("component_manager", "event", self.event.id);
        self.responder.send(()).unwrap()
    }
}

/// EventDispatcher and EventStream are two ends of a channel.
///
/// An EventDispatcher is owned by the EventRegistry. It sends an
/// Event to the EventStream.
///
/// An EventStream is owned by the client - usually a test harness or a
/// EventSource. It receives a Event from an EventDispatcher and propagates it
/// to the client.
#[derive(Clone)]
pub struct EventDispatcher {
    /// Optionally specifies a realm that this EventDispatcher can dispatch events from.
    scope_moniker: Option<AbsoluteMoniker>,
    /// An `mpsc::Sender` used to dispatch an event. Note that this
    /// `mpsc::Sender` is wrapped in an Arc<Mutex<..>> to allow it to be cloneable
    /// and passed along to other tasks for dispatch.
    tx: Arc<Mutex<mpsc::Sender<Event>>>,
}

impl EventDispatcher {
    fn new(scope_moniker: Option<AbsoluteMoniker>, tx: mpsc::Sender<Event>) -> Self {
        Self { scope_moniker, tx: Arc::new(Mutex::new(tx)) }
    }

    /// Sends the event to an event stream, if fired in the scope of `scope_moniker`. Returns
    /// a responder which can be blocked on.
    async fn send(&self, event: ComponentEvent) -> Result<Option<oneshot::Receiver<()>>, Error> {
        if let Some(scope_moniker) = &self.scope_moniker {
            if !scope_moniker.contains_in_realm(&event.target_moniker) {
                return Ok(None);
            }
        }

        trace::duration!("component_manager", "events:send");
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
            tx.send(Event { event, responder: responder_tx }).await?;
        }
        Ok(Some(responder_rx))
    }
}

pub struct EventStream {
    rx: mpsc::Receiver<Event>,
}

impl EventStream {
    fn new(rx: mpsc::Receiver<Event>) -> Self {
        Self { rx }
    }

    /// Receives the next event from the sender.
    pub async fn next(&mut self) -> Option<Event> {
        trace::duration!("component_manager", "events:next");
        self.rx.next().await
    }

    /// Waits for an event with a particular EventType against a component with a
    /// particular moniker. Ignores all other events.
    pub async fn wait_until(
        &mut self,
        expected_event_type: EventType,
        expected_moniker: AbsoluteMoniker,
    ) -> Option<Event> {
        while let Some(event) = self.next().await {
            let actual_event_type = event.event.payload.type_();
            if expected_moniker == event.event.target_moniker
                && expected_event_type == actual_event_type
            {
                return Some(event);
            }
            event.resume();
        }
        None
    }
}

/// Subscribes to events from multiple tasks and sends events to all of them.
pub struct EventRegistry {
    dispatcher_map: Arc<Mutex<HashMap<EventType, Vec<EventDispatcher>>>>,
}

impl EventRegistry {
    pub fn new() -> Self {
        Self { dispatcher_map: Arc::new(Mutex::new(HashMap::new())) }
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![
            // This hook must be registered with all events.
            // However, a task will only receive events to which it subscribed.
            HooksRegistration::new(
                "EventRegistry",
                vec![
                    EventType::AddDynamicChild,
                    EventType::BeforeStartInstance,
                    EventType::PostDestroyInstance,
                    EventType::PreDestroyInstance,
                    EventType::ResolveInstance,
                    EventType::RouteCapability,
                    EventType::StopInstance,
                ],
                Arc::downgrade(self) as Weak<dyn Hook>,
            ),
        ]
    }

    /// Subscribes to events of a provided set of EventTypes.
    pub async fn subscribe(
        &self,
        scope_moniker: Option<AbsoluteMoniker>,
        event_types: Vec<EventType>,
    ) -> EventStream {
        let (tx, rx) = mpsc::channel(0);
        let dispatcher = EventDispatcher::new(scope_moniker, tx);
        let event_stream = EventStream::new(rx);

        let mut dispatcher_map = self.dispatcher_map.lock().await;
        for event_type in event_types {
            let dispatchers = dispatcher_map.entry(event_type).or_insert(vec![]);
            dispatchers.push(dispatcher.clone());
        }

        event_stream
    }

    /// Sends the event to all dispatchers and waits to be unblocked by all
    async fn send(&self, event: &ComponentEvent) -> Result<(), ModelError> {
        // Copy the senders so we don't hold onto the sender map lock
        // If we didn't do this, it is possible to deadlock while holding onto this lock.
        // For example,
        // Task A : call send(event1) -> lock on sender map -> send -> wait for responders
        // Task B : call send(event2) -> lock on sender map
        // If task B was required to respond to event1, then this is a deadlock.
        // Neither task can make progress.
        let dispatchers = {
            let dispatcher_map = self.dispatcher_map.lock().await;
            if let Some(dispatchers) = dispatcher_map.get(&event.payload.type_()) {
                dispatchers.clone()
            } else {
                // There were no senders for this event. Do nothing.
                return Ok(());
            }
        };

        let mut responder_channels = vec![];
        for dispatcher in dispatchers {
            let result = dispatcher.send(event.clone()).await;
            match result {
                Ok(Some(responder_channel)) => {
                    // A future can be canceled if the EventStream was dropped after
                    // a send. We don't crash the system when this happens. It is
                    // perfectly valid for a EventStream to be dropped. That simply
                    // means that the EventStream is no longer interested in future
                    // events. So we force each future to return a success. This
                    // ensures that all the futures can be driven to completion.
                    let responder_channel = async move {
                        trace::duration!("component_manager", "events:wait_for_resume");
                        let _ = responder_channel.await;
                        trace::flow_end!("component_manager", "event", event.id);
                    };
                    responder_channels.push(responder_channel);
                }
                // There's nothing to do if event is outside the scope of the given
                // `EventDispatcher`.
                Ok(None) => (),
                Err(_) => {
                    // A send can fail if the EventStream was dropped. We don't
                    // crash the system when this happens. It is perfectly
                    // valid for a EventStream to be dropped. That simply means
                    // that the EventStream is no longer interested in future
                    // events.
                }
            }
        }

        // Wait until all tasks have used the responder to unblock.
        {
            trace::duration!("component_manager", "events:wait_for_all_resume");
            futures::future::join_all(responder_channels).await;
        }

        Ok(())
    }
}

#[async_trait]
impl Hook for EventRegistry {
    async fn on(self: Arc<Self>, event: &ComponentEvent) -> Result<(), ModelError> {
        self.send(event).await?;
        Ok(())
    }
}
