// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::Payload;
use crate::agent::{Context as AgentContext, Lifespan};
use crate::blueprint_definition;
use crate::config;
use crate::config::inspect_logger::{InspectConfigLogger, InspectConfigLoggerHandle};
use crate::event;
use crate::handler::device_storage::DeviceStorageAccess;
use crate::message::base::{role, MessageEvent, MessengerType};
use crate::message::receptor::Receptor;
use crate::service;
use crate::Role;

use fuchsia_async as fasync;
use futures::lock::Mutex;
use futures::stream::{FuturesUnordered, StreamFuture};
use futures::StreamExt;
use std::sync::Arc;

blueprint_definition!("inspect_config_agent", InspectConfigAgent::create);

// TODO(fxbug.dev/84416): Migrate usages of the InspectConfigAgent and remove in favor
// of the new InspectLogger.
pub(crate) struct InspectConfigAgent {
    /// The factory for creating a messenger to receive messages.
    delegate: service::message::Delegate,

    /// The inspect logger with which to write to inspect.
    inspect_logger: Arc<Mutex<InspectConfigLogger>>,
}

impl DeviceStorageAccess for InspectConfigAgent {
    const STORAGE_KEYS: &'static [&'static str] = &[];
}

impl InspectConfigAgent {
    async fn create(context: AgentContext) {
        InspectConfigAgent::create_with_logger(context, InspectConfigLoggerHandle::new().logger)
            .await;
    }

    async fn create_with_logger(
        context: AgentContext,
        inspect_logger: Arc<Mutex<InspectConfigLogger>>,
    ) {
        let mut agent = InspectConfigAgent { delegate: context.delegate, inspect_logger };

        // Using FuturesUnordered to manage events helps avoid lifetime and ownership issues.
        let unordered = FuturesUnordered::new();
        unordered.push(context.receptor.into_future());

        fasync::Task::spawn(async move { agent.handle(unordered).await }).detach();
    }

    async fn handle(
        &mut self,
        mut unordered: FuturesUnordered<
            StreamFuture<Receptor<service::Payload, service::Address, Role>>,
        >,
    ) {
        while let Some((event, stream)) = unordered.next().await {
            let event = if let Some(event) = event {
                event
            } else {
                continue;
            };

            match event {
                MessageEvent::Message(
                    service::Payload::Agent(Payload::Invocation(invocation)),
                    client,
                ) => {
                    // Only initialize the message receptor once during Initialization.
                    if let Lifespan::Initialization = invocation.lifespan {
                        // Build a receptor on which load events will be received.
                        let (_, receptor) = self
                            .delegate
                            .messenger_builder(MessengerType::Unbound)
                            .add_role(role::Signature::role(service::Role::Event(
                                event::Role::Sink,
                            )))
                            .build()
                            .await
                            .expect("config agent receptor should have been created");

                        unordered.push(receptor.into_future());
                    }
                    // Since the agent runs at creation, there is no need to handle state here.
                    client.reply(service::Payload::Agent(Payload::Complete(Ok(())))).send();
                }
                MessageEvent::Message(
                    service::Payload::Event(event::Payload::Event(event::Event::ConfigLoad(event))),
                    message_client,
                ) => {
                    self.handle_event(event, message_client).await;
                }
                _ => {} // Other messages are ignored.
            }

            // When an event is received, the rest of the events must be added
            // back onto the unordered list.
            unordered.push(stream.into_future());
        }
    }

    async fn handle_event(
        &mut self,
        payload: config::base::Event,
        mut message_client: service::message::MessageClient,
    ) {
        let config::base::Event::Load(config_load_info) = payload;
        self.inspect_logger.lock().await.write_config_load_to_inspect(config_load_info);
        message_client.acknowledge().await;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::agent::Context;
    use crate::clock;
    use crate::event;
    use crate::message::base::{Audience, MessageEvent, MessengerType, Status};
    use crate::message::MessageHubUtil;

    use fuchsia_inspect::assert_data_tree;
    use fuchsia_zircon::Time;
    use std::collections::HashSet;

    // Tests that the agent can receive a load event.
    #[fuchsia_async::run_until_stalled(test)]
    async fn agent_receives_event() {
        // Set the clock to a fixed timestamp.
        const NANOS: i64 = 444_444_444;
        clock::mock::set(Time::from_nanos(NANOS));

        // Create the messenger and agent context.
        let messenger_factory = service::MessageHub::create_hub();
        let context = Context::new(
            messenger_factory
                .clone()
                .messenger_builder(MessengerType::Unbound)
                .add_role(role::Signature::role(Role::Event(event::Role::Sink)))
                .build()
                .await
                .expect("should be present")
                .1,
            messenger_factory.clone(),
            HashSet::new(),
            HashSet::new(),
            None,
        )
        .await;

        // Create a sender to send out the config load event.
        let (messenger, _) = messenger_factory.create(MessengerType::Unbound).await.unwrap();

        // Create the inspect config logger.
        let inspect_config_logger = InspectConfigLoggerHandle::new().logger;

        // Create the inspect agent.
        InspectConfigAgent::create_with_logger(context, inspect_config_logger.clone()).await;

        let message = "unable to open file, using defaults: \
            Os { code: 2, kind: NotFound, message: \"No such file or directory\""
            .to_string();
        let expected_inspect_info = config::base::ConfigLoadInfo {
            path: "/config/data/input_device_config.json".to_string(),
            status: config::base::ConfigLoadStatus::UsingDefaults(message),
            contents: Some("{}".to_string()),
        };

        // Send the config load message.
        let mut receptor = messenger
            .message(
                service::Payload::Event(event::Payload::Event(event::Event::ConfigLoad(
                    config::base::Event::Load(expected_inspect_info.clone()),
                ))),
                Audience::Role(role::Signature::role(Role::Event(event::Role::Sink))),
            )
            .send();

        let reply = receptor.next().await;
        assert_eq!(reply, Some(MessageEvent::Status(Status::Acknowledged)));

        // Ensure the load is logged to the inspect node.
        assert_data_tree!(inspect_config_logger.lock().await.inspector, root: {
            config_loads: {
                "/config/data/input_device_config.json": {
                    "count": 1i64,
                    "timestamp": "0.444444444",
                    "value": format!("{:#?}", expected_inspect_info),
                }
            }
        });
    }
}
