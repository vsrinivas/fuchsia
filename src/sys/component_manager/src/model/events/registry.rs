// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        error::ModelError,
        events::{
            dispatcher::{EventDispatcher, ScopeMetadata},
            event::SyncMode,
            stream::EventStream,
            synthesizer::EventSynthesizer,
        },
        hooks::{Event as ComponentEvent, EventType, HasEventType, Hook, HooksRegistration},
        model::Model,
    },
    async_trait::async_trait,
    cm_rust::CapabilityName,
    fuchsia_trace as trace,
    futures::lock::Mutex,
    std::{
        collections::HashMap,
        sync::{Arc, Weak},
    },
};

pub struct RoutedEvent {
    pub source_name: CapabilityName,
    pub scopes: Vec<ScopeMetadata>,
}

/// Subscribes to events from multiple tasks and sends events to all of them.
pub struct EventRegistry {
    dispatcher_map: Arc<Mutex<HashMap<CapabilityName, Vec<Weak<EventDispatcher>>>>>,
    event_synthesizer: EventSynthesizer,
}

impl EventRegistry {
    pub fn new(model: Weak<Model>) -> Self {
        let event_synthesizer = EventSynthesizer::new(model);
        Self { dispatcher_map: Arc::new(Mutex::new(HashMap::new())), event_synthesizer }
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![
            // This hook must be registered with all events.
            // However, a task will only receive events to which it subscribed.
            HooksRegistration::new(
                "EventRegistry",
                EventType::values(),
                Arc::downgrade(self) as Weak<dyn Hook>,
            ),
        ]
    }

    /// Subscribes to events of a provided set of EventTypes.
    pub async fn subscribe(
        &self,
        sync_mode: &SyncMode,
        events: Vec<RoutedEvent>,
    ) -> Result<EventStream, ModelError> {
        // TODO(fxb/48510): get rid of this channel and use FIDL directly.
        let mut event_stream = EventStream::new();

        let mut dispatcher_map = self.dispatcher_map.lock().await;
        let running_name: CapabilityName = EventType::Running.to_string().into();
        for event in events.iter() {
            if event.source_name != running_name {
                let dispatchers = dispatcher_map.entry(event.source_name.clone()).or_insert(vec![]);
                let dispatcher =
                    event_stream.create_dispatcher(sync_mode.clone(), event.scopes.clone());
                dispatchers.push(dispatcher);
            }
        }

        self.event_synthesizer.spawn_synthesis(event_stream.sender(), events);

        Ok(event_stream)
    }

    /// Sends the event to all dispatchers and waits to be unblocked by all
    async fn dispatch(&self, event: &ComponentEvent) -> Result<(), ModelError> {
        // Copy the senders so we don't hold onto the sender map lock
        // If we didn't do this, it is possible to deadlock while holding onto this lock.
        // For example,
        // Task A : call dispatch(event1) -> lock on sender map -> send -> wait for responders
        // Task B : call dispatch(event2) -> lock on sender map
        // If task B was required to respond to event1, then this is a deadlock.
        // Neither task can make progress.
        let dispatchers = {
            let mut dispatcher_map = self.dispatcher_map.lock().await;
            if let Some(dispatchers) = dispatcher_map.get_mut(&event.event_type().into()) {
                let mut strong_dispatchers = vec![];
                dispatchers.retain(|dispatcher| {
                    if let Some(dispatcher) = dispatcher.upgrade() {
                        strong_dispatchers.push(dispatcher);
                        true
                    } else {
                        false
                    }
                });
                strong_dispatchers
            } else {
                // There were no senders for this event. Do nothing.
                return Ok(());
            }
        };

        let mut responder_channels = vec![];
        for dispatcher in dispatchers {
            let result = dispatcher.dispatch(event.clone()).await;
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

    #[cfg(test)]
    async fn dispatchers_per_event_type(&self, event_type: EventType) -> usize {
        let dispatcher_map = self.dispatcher_map.lock().await;
        dispatcher_map
            .get(&event_type.into())
            .map(|dispatchers| dispatchers.len())
            .unwrap_or_default()
    }
}

#[async_trait]
impl Hook for EventRegistry {
    async fn on(self: Arc<Self>, event: &ComponentEvent) -> Result<(), ModelError> {
        self.dispatch(event).await?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{
            hooks::{Event as ComponentEvent, EventError, EventErrorPayload, EventPayload},
            moniker::AbsoluteMoniker,
            testing::test_helpers::*,
        },
        matches::assert_matches,
    };

    async fn dispatch_fake_event(registry: &EventRegistry) -> Result<(), ModelError> {
        let root_component_url = "test:///root".to_string();
        let event = ComponentEvent::new(
            AbsoluteMoniker::root(),
            Ok(EventPayload::Discovered { component_url: root_component_url }),
        );
        registry.dispatch(&event).await
    }

    async fn dispatch_error_event(registry: &EventRegistry) -> Result<(), ModelError> {
        let root = AbsoluteMoniker::root();
        let event = ComponentEvent::new(
            root.clone(),
            Err(EventError::new(
                &ModelError::instance_not_found(root.clone()),
                EventErrorPayload::Resolved,
            )),
        );
        registry.dispatch(&event).await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn drop_dispatcher_when_event_stream_dropped() {
        let model = Arc::new(new_test_model("root", Vec::new()));
        let event_registry = EventRegistry::new(Arc::downgrade(&model));

        assert_eq!(0, event_registry.dispatchers_per_event_type(EventType::Discovered).await);

        let mut event_stream_a = event_registry
            .subscribe(
                &SyncMode::Async,
                vec![RoutedEvent {
                    source_name: EventType::Discovered.into(),
                    scopes: vec![ScopeMetadata::new(AbsoluteMoniker::root())],
                }],
            )
            .await
            .expect("subscribe succeeds");

        assert_eq!(1, event_registry.dispatchers_per_event_type(EventType::Discovered).await);

        let mut event_stream_b = event_registry
            .subscribe(
                &SyncMode::Async,
                vec![RoutedEvent {
                    source_name: EventType::Discovered.into(),
                    scopes: vec![ScopeMetadata::new(AbsoluteMoniker::root())],
                }],
            )
            .await
            .expect("subscribe succeeds");

        assert_eq!(2, event_registry.dispatchers_per_event_type(EventType::Discovered).await);

        dispatch_fake_event(&event_registry).await.unwrap();

        // Verify that both EventStreams receive the event.
        assert!(event_stream_a.next().await.is_some());
        assert!(event_stream_b.next().await.is_some());
        assert_eq!(2, event_registry.dispatchers_per_event_type(EventType::Discovered).await);

        drop(event_stream_a);

        // EventRegistry won't drop EventDispatchers until an event is dispatched.
        assert_eq!(2, event_registry.dispatchers_per_event_type(EventType::Discovered).await);

        dispatch_fake_event(&event_registry).await.unwrap();

        assert!(event_stream_b.next().await.is_some());
        assert_eq!(1, event_registry.dispatchers_per_event_type(EventType::Discovered).await);

        drop(event_stream_b);

        dispatch_fake_event(&event_registry).await.unwrap();
        assert_eq!(0, event_registry.dispatchers_per_event_type(EventType::Discovered).await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn event_error_dispatch() {
        let model = Arc::new(new_test_model("root", Vec::new()));
        let event_registry = EventRegistry::new(Arc::downgrade(&model));

        assert_eq!(0, event_registry.dispatchers_per_event_type(EventType::Resolved).await);

        let mut event_stream = event_registry
            .subscribe(
                &SyncMode::Async,
                vec![RoutedEvent {
                    source_name: EventType::Resolved.into(),
                    scopes: vec![ScopeMetadata::new(AbsoluteMoniker::root())],
                }],
            )
            .await
            .expect("subscribed to event stream");

        assert_eq!(1, event_registry.dispatchers_per_event_type(EventType::Resolved).await);

        dispatch_error_event(&event_registry).await.unwrap();

        let event = event_stream.next().await.map(|e| e.event).unwrap();

        // Verify that we received the event error.
        assert_matches!(
            event.result,
            Err(EventError {
                source: ModelError::InstanceNotFound { .. },
                event_error_payload: EventErrorPayload::Resolved,
            })
        );
    }
}
