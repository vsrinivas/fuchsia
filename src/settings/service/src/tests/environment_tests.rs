// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::{
    AgentError, Context, Invocation, InvocationResult, Lifespan, Payload as AgentPayload,
};
use crate::base::{Dependency, Entity, SettingType};
use crate::blueprint_definition;
use crate::event::{Event, Payload as EventPayload};
use crate::ingress::fidl;
use crate::ingress::registration;
use crate::job::source::Error;
use crate::job::{self, Job};
use crate::message::base::{filter, Audience, MessengerType};
use crate::service::Payload;
use crate::service_context::ServiceContext;
use crate::storage::testing::InMemoryStorageFactory;
use crate::tests::fakes::service_registry::ServiceRegistry;
use crate::tests::message_utils::verify_payload;
use crate::tests::scaffold::workload::channel;
use crate::{service, Environment, EnvironmentBuilder};
use assert_matches::assert_matches;
use fuchsia_async as fasync;
use futures::future::BoxFuture;
use futures::FutureExt;
use futures::StreamExt;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_environment_test";
const TEST_PAYLOAD: &str = "test_payload";
const TEST_REPLY: &str = "test_reply";

blueprint_definition!("test_agent", TestAgent::create);
// A test agent to send an event to the message hub. Required so that we can test that
// a message sent on the message hub returned from environment creation is received by
// other components attached to the message hub.
struct TestAgent {
    delegate: service::message::Delegate,
}

impl TestAgent {
    async fn create(mut context: Context) {
        let mut agent = TestAgent { delegate: context.delegate.clone() };

        fasync::Task::spawn(async move {
            let _ = &context;
            while let Ok((AgentPayload::Invocation(invocation), client)) =
                context.receptor.next_of::<AgentPayload>().await
            {
                client
                    .reply(AgentPayload::Complete(agent.handle(invocation).await).into())
                    .send()
                    .ack();
            }
        })
        .detach();
    }

    async fn handle(&mut self, invocation: Invocation) -> InvocationResult {
        match invocation.lifespan {
            Lifespan::Initialization => Err(AgentError::UnhandledLifespan),
            Lifespan::Service => self.handle_service_lifespan(invocation.service_context).await,
        }
    }

    async fn handle_service_lifespan(
        &mut self,
        _service_context: Arc<ServiceContext>,
    ) -> InvocationResult {
        let (_, mut receptor) = self
            .delegate
            .create(MessengerType::Broker(Some(filter::Builder::single(
                filter::Condition::Custom(Arc::new(move |message| {
                    matches!(
                        message.payload(),
                        Payload::Event(EventPayload::Event(Event::Custom(TEST_PAYLOAD)))
                    )
                })),
            ))))
            .await
            .expect("Failed to create broker");

        fasync::Task::spawn(async move {
            verify_payload(
                Payload::Event(EventPayload::Event(Event::Custom(TEST_PAYLOAD))),
                &mut receptor,
                Some(Box::new(|client| -> BoxFuture<'_, ()> {
                    Box::pin(async move {
                        client
                            .reply(Payload::Event(EventPayload::Event(Event::Custom(TEST_REPLY))))
                            .send()
                            .ack();
                    })
                })),
            )
            .await;
        })
        .detach();
        Ok(())
    }
}

// Ensure that the messenger factory returned from environment creation is able
// to send events to the test agent.
#[fuchsia_async::run_until_stalled(test)]
async fn test_message_hub() {
    let service_registry = ServiceRegistry::create();

    let Environment { connector: _, delegate, .. } =
        EnvironmentBuilder::new(Arc::new(InMemoryStorageFactory::new()))
            .service(ServiceRegistry::serve(service_registry))
            .agents(&[blueprint::create()])
            .spawn_nested(ENV_NAME)
            .await
            .unwrap();

    // Send message for TestAgent to receive.
    let (messenger, _) = delegate
        .messenger_builder(MessengerType::Unbound)
        .build()
        .await
        .expect("should be able to create messenger");

    let mut client_receptor = messenger
        .message(
            Payload::Event(EventPayload::Event(Event::Custom(TEST_PAYLOAD))),
            Audience::Broadcast,
        )
        .send();

    // Wait for reply from TestAgent.
    verify_payload(
        Payload::Event(EventPayload::Event(Event::Custom(TEST_REPLY))),
        &mut client_receptor,
        None,
    )
    .await;
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_dependency_generation() {
    let entity = Entity::Handler(SettingType::Unknown);
    let registrant =
        registration::Builder::new(registration::Registrar::Test(Box::new(move || {})))
            .add_dependency(Dependency::Entity(entity))
            .build();

    let Environment { entities, .. } =
        EnvironmentBuilder::new(Arc::new(InMemoryStorageFactory::new()))
            .registrants(vec![registrant])
            .spawn_nested(ENV_NAME)
            .await
            .expect("environment should be built");

    assert!(entities.contains(&entity));
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_display_interface_consolidation() {
    let Environment { entities, .. } =
        EnvironmentBuilder::new(Arc::new(InMemoryStorageFactory::new()))
            .fidl_interfaces(&[
                fidl::Interface::Display(fidl::display::InterfaceFlags::BASE),
                fidl::Interface::Display(fidl::display::InterfaceFlags::LIGHT_SENSOR),
            ])
            .spawn_nested(ENV_NAME)
            .await
            .expect("environment should be built");

    assert!(entities.contains(&Entity::Handler(SettingType::Display)));
    assert!(entities.contains(&Entity::Handler(SettingType::LightSensor)));
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_job_sourcing() {
    // Create channel to send the current job state.
    let (job_state_tx, mut job_state_rx) = futures::channel::mpsc::unbounded::<channel::State>();

    // Create a new job stream with an Job that will signal when it is executed.
    let job_stream = async move {
        Ok(Job::new(job::work::Load::Independent(Box::new(channel::Workload::new(job_state_tx)))))
            as Result<Job, Error>
    }
    .into_stream();

    // Build a registrant with the stream.
    let registrant = registration::Builder::new(registration::Registrar::TestWithSeeder(Box::new(
        move |seeder| {
            seeder.seed(job_stream);
        },
    )))
    .build();

    // Build environment with the registrant.
    let _ = EnvironmentBuilder::new(Arc::new(InMemoryStorageFactory::new()))
        .registrants(vec![registrant])
        .spawn_nested(ENV_NAME)
        .await
        .expect("environment should be built");

    // Ensure job is executed.
    assert_matches!(job_state_rx.next().await, Some(channel::State::Execute));
}
