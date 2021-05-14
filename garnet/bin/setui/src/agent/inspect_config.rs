// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::Payload;
use crate::agent::{Context as AgentContext, Lifespan};
use crate::blueprint_definition;
use crate::clock;
use crate::config;
use crate::event;
use crate::handler::device_storage::DeviceStorageAccess;
use crate::message::base::{role, MessageEvent, MessengerType};
use crate::message::receptor::Receptor;
use crate::service;
use crate::Role;

use fuchsia_async as fasync;
use fuchsia_inspect::{self as inspect, component, NumericProperty, Property};
use futures::stream::{FuturesUnordered, StreamFuture};
use futures::StreamExt;
use std::collections::HashMap;

const CONFIG_INSPECT_NODE_NAME: &str = "config_loads";

blueprint_definition!("inspect_config_agent", InspectConfigAgent::create);

pub struct InspectConfigAgent {
    /// The factory for creating a messenger to receive messages.
    delegate: service::message::Delegate,

    /// The saved inspect node for the config loads.
    inspect_node: inspect::Node,

    /// The saved information about each load.
    config_load_values: HashMap<String, ConfigInspectInfo>,
}

/// Information about a config file load to be written to inspect.
///
/// Inspect nodes are not used, but need to be held as they're deleted from inspect once they go
/// out of scope.
struct ConfigInspectInfo {
    /// Node of this info.
    _node: inspect::Node,

    /// Nanoseconds since boot that this config was loaded.
    timestamp: inspect::StringProperty,

    /// Debug string representation of the value of this config load info.
    value: inspect::StringProperty,

    /// Number of times the config was loaded.
    count: inspect::IntProperty,
}

impl DeviceStorageAccess for InspectConfigAgent {
    const STORAGE_KEYS: &'static [&'static str] = &[];
}

impl InspectConfigAgent {
    pub async fn create(context: AgentContext) {
        let inspect_node = component::inspector().root().create_child(CONFIG_INSPECT_NODE_NAME);
        InspectConfigAgent::create_with_node(context, inspect_node).await;
    }

    pub async fn create_with_node(context: AgentContext, inspect_node: inspect::Node) {
        let mut agent = InspectConfigAgent {
            delegate: context.delegate,
            inspect_node,
            config_load_values: HashMap::new(),
        };

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
                    service::Payload::Event(event::Payload::Event(event)),
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
        payload: event::Event,
        mut message_client: service::message::MessageClient,
    ) {
        if let event::Event::ConfigLoad(config::base::Event::Load(config::base::ConfigLoadInfo {
            path,
            status,
        })) = payload
        {
            let timestamp = clock::inspect_format_now();
            match self.config_load_values.get_mut(&path) {
                Some(config_inspect_info) => {
                    config_inspect_info.timestamp.set(&timestamp);
                    config_inspect_info
                        .value
                        .set(&format!("{:#?}", config::base::ConfigLoadInfo { path, status }));
                    config_inspect_info.count.set(config_inspect_info.count.get().unwrap_or(0) + 1);
                }
                None => {
                    // Config file not loaded before, add new entry in table.
                    let node = self.inspect_node.create_child(path.clone());
                    let value_prop = node.create_string(
                        "value",
                        format!(
                            "{:#?}",
                            config::base::ConfigLoadInfo { path: path.clone(), status }
                        ),
                    );
                    let timestamp_prop = node.create_string("timestamp", timestamp.clone());
                    let count_prop = node.create_int("count", 1);
                    self.config_load_values.insert(
                        path,
                        ConfigInspectInfo {
                            _node: node,
                            value: value_prop,
                            timestamp: timestamp_prop,
                            count: count_prop,
                        },
                    );
                }
            }
            message_client.acknowledge().await;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::agent::Context;
    use crate::event;
    use crate::message::base::{Audience, MessageEvent, MessengerType, Status};

    use fuchsia_inspect::assert_inspect_tree;
    use fuchsia_zircon::Time;
    use std::collections::HashSet;

    // Tests that the agent can receive a load event.
    #[fuchsia_async::run_until_stalled(test)]
    async fn agent_receives_event() {
        // Set the clock to a fixed timestamp.
        const NANOS: i64 = 444_444_444;
        clock::mock::set(Time::from_nanos(NANOS));

        // Create the messenger and agent context.
        let messenger_factory = service::message::create_hub();
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

        // Create the inspect node.
        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child(CONFIG_INSPECT_NODE_NAME);

        // Create the inspect agent.
        InspectConfigAgent::create_with_node(context, inspect_node).await;

        let message = "unable to open file, using defaults: \
            Os { code: 2, kind: NotFound, message: \"No such file or directory\""
            .to_string();
        let expected_inspect_info = config::base::ConfigLoadInfo {
            path: "/config/data/input_device_config.json".to_string(),
            status: config::base::ConfigLoadStatus::UsingDefaults(message),
        };

        // Send the config load message.
        let mut receptor = messenger
            .message(
                service::Payload::Event(event::Payload::Event(event::Event::ConfigLoad(
                    config::base::Event::Load(expected_inspect_info.clone()),
                )))
                .into(),
                Audience::Role(role::Signature::role(Role::Event(event::Role::Sink))),
            )
            .send();

        let reply = receptor.next().await;
        assert_eq!(reply, Some(MessageEvent::Status(Status::Acknowledged)));

        // Ensure the load is logged to the inspect node.
        assert_inspect_tree!(inspector, root: {
            config_loads: {
                "/config/data/input_device_config.json": {
                    "count": 1 as i64,
                    "timestamp": "0.444444444",
                    "value": format!("{:#?}", expected_inspect_info),
                }
            }
        });
    }
}
