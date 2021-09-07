// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::{AgentError, Context, Payload};
use crate::event;
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::service;
use crate::tests::scaffold;
use crate::EnvironmentBuilder;
use fuchsia_async as fasync;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use matches::assert_matches;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_event_test_environment";

// Exercises the event publishing path from agents.
#[fuchsia_async::run_until_stalled(test)]
async fn test_agent_event_propagation() {
    let agent_publisher: Arc<Mutex<Option<event::Publisher>>> = Arc::new(Mutex::new(None));
    let delegate: Arc<Mutex<Option<service::message::Delegate>>> = Arc::new(Mutex::new(None));

    // Capturing the context allows retrieving the publisher meant for the
    // agent.
    let publisher_capture = agent_publisher.clone();

    // Capturing the delegate allows registering a listener to published events.
    let cloned_delegate = delegate.clone();

    // Upon instantiation, the subscriber will capture the event message
    // delegate.
    let create_subscriber =
        Arc::new(move |captured_delegate: service::message::Delegate| -> BoxFuture<'static, ()> {
            let delegate = cloned_delegate.clone();
            Box::pin(async move {
                *delegate.lock().await = Some(captured_delegate);
            })
        });

    // This agent simply captures the context and returns unhandled for all
    // subsequent invocations (allowing the authority to progress).
    let create_agent = Arc::new(move |mut context: Context| -> BoxFuture<'static, ()> {
        let publisher_capture = publisher_capture.clone();

        Box::pin(async move {
            *publisher_capture.lock().await = Some(context.get_publisher());

            fasync::Task::spawn(async move {
                while let Ok((Payload::Invocation(_), client)) =
                    context.receptor.next_of::<Payload>().await
                {
                    client
                        .reply(Payload::Complete(Err(AgentError::UnhandledLifespan)).into())
                        .send()
                        .ack();
                }
            })
            .detach();
        })
    });

    let _ = EnvironmentBuilder::new(Arc::new(InMemoryStorageFactory::new()))
        .event_subscribers(&[scaffold::event::subscriber::Blueprint::create(create_subscriber)])
        .agents(&[Arc::new(scaffold::agent::Blueprint::new(scaffold::agent::Generate::Async(
            create_agent,
        )))])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let service_delegate =
        delegate.clone().lock().await.take().expect("Should have captured event factory");
    let mut receptor = service::build_event_listener(&service_delegate).await;

    let sent_event = event::Event::Custom("test");

    let publisher = agent_publisher.lock().await.take().expect("Should have captured publisher");
    publisher.send_event(sent_event.clone());

    assert_matches!(
        receptor.next_of::<event::Payload>().await.expect("Should have received broadcast").0,
        event::Payload::Event(broadcasted_event) if broadcasted_event == sent_event);
}
