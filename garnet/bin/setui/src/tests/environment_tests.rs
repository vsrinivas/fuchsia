// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::agent::{
        AgentError, Context, Invocation, InvocationResult, Lifespan, Payload as AgentPayload,
    },
    crate::base::SettingType,
    crate::blueprint_definition,
    crate::event::{Event, Payload as EventPayload},
    crate::handler::device_storage::testing::InMemoryStorageFactory,
    crate::message::base::{filter, Audience, MessengerType},
    crate::service::Payload,
    crate::service_context::ServiceContext,
    crate::tests::fakes::input_device_registry_service::InputDeviceRegistryService,
    crate::tests::fakes::service_registry::ServiceRegistry,
    crate::tests::message_utils::verify_payload,
    crate::{service, Environment, EnvironmentBuilder},
    fuchsia_async as fasync,
    futures::future::BoxFuture,
    futures::lock::Mutex,
    std::sync::Arc,
};

const ENV_NAME: &str = "settings_service_environment_test";
const TEST_PAYLOAD: &str = "test_payload";
const TEST_REPLY: &str = "test_reply";

blueprint_definition!("test_agent", TestAgent::create);
// A test agent to send an event to the message hub. Required so that we can test that
// a message sent on the message hub returned from environment creation is received by
// other components attached to the message hub.
pub struct TestAgent {
    delegate: service::message::Delegate,
}

impl TestAgent {
    async fn create(mut context: Context) {
        let mut agent = TestAgent { delegate: context.delegate.clone() };

        fasync::Task::spawn(async move {
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
                        ()
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
    let input_device_registry_service_handle =
        Arc::new(Mutex::new(InputDeviceRegistryService::new()));
    service_registry.lock().await.register_service(input_device_registry_service_handle.clone());

    let Environment { nested_environment: _, delegate, .. } =
        EnvironmentBuilder::new(Arc::new(InMemoryStorageFactory::new()))
            .service(ServiceRegistry::serve(service_registry))
            .agents(&[blueprint::create()])
            .settings(&[SettingType::Unknown])
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
