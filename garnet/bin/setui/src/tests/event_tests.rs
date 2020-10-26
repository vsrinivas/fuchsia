// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use crate::agent::base::{AgentError, Context};
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::internal::agent;
use crate::internal::event;
use crate::message::base::{MessageEvent, MessengerType};
use crate::tests::scaffold;
use crate::EnvironmentBuilder;
use fuchsia_async as fasync;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use futures::StreamExt;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_event_test_environment";

/// Exercises the event publishing path from agents.
#[fuchsia_async::run_until_stalled(test)]
async fn test_agent_event_propagation() {
    let agent_publisher: Arc<Mutex<Option<event::Publisher>>> = Arc::new(Mutex::new(None));
    let event_factory: Arc<Mutex<Option<event::message::Factory>>> = Arc::new(Mutex::new(None));

    // Capturing the context allows retrieving the publisher meant for the
    // agent.
    let publisher_capture = agent_publisher.clone();

    // Capturing the factory allows registering a listener to published events.
    let event_factory_capture = event_factory.clone();

    // Upon instantiation, the subscriber will capture the event message
    // factory.
    let create_subscriber =
        Arc::new(move |factory: event::message::Factory| -> BoxFuture<'static, ()> {
            let event_factory = event_factory_capture.clone();
            Box::pin(async move {
                *event_factory.lock().await = Some(factory);
            })
        });

    // This agent simply captures the context and returns unhandled for all
    // subsequent invocations (allowing the authority to progress).
    let create_agent = Arc::new(move |mut context: Context| -> BoxFuture<'static, ()> {
        let publisher_capture = publisher_capture.clone();

        Box::pin(async move {
            *publisher_capture.lock().await = Some(context.get_publisher());

            fasync::Task::spawn(async move {
                while let Ok((payload, client)) = context.receptor.next_payload().await {
                    if let agent::Payload::Invocation(_) = payload {
                        client
                            .reply(agent::Payload::Complete(Err(AgentError::UnhandledLifespan)))
                            .send()
                            .ack();
                    }
                }
            })
            .detach();
        })
    });

    let _ = EnvironmentBuilder::new(InMemoryStorageFactory::create())
        .settings(&[])
        .event_subscribers(&[scaffold::event::subscriber::Blueprint::create(create_subscriber)])
        .agents(&[Arc::new(scaffold::agent::Blueprint::new(
            scaffold::agent::Generate::Async(create_agent),
            "test",
        ))])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let factory =
        event_factory.clone().lock().await.take().expect("Should have captured event factory");
    let (_, mut receptor) = factory
        .create(MessengerType::Unbound)
        .await
        .expect("Should be able to retrieve messenger for publisher");

    let sent_event = event::Event::Custom("test");

    let publisher = agent_publisher.lock().await.take().expect("Should have captured publisher");
    publisher.send_event(sent_event.clone());

    let received_event =
        receptor.next().await.expect("First message should have been the broadcast");
    match received_event {
        MessageEvent::Message(event::Payload::Event(broadcasted_event), _) => {
            assert_eq!(broadcasted_event, sent_event);
        }
        _ => {
            panic!("Should have received an event payload");
        }
    }
}
