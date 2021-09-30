// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::{HashMap, HashSet};
use std::sync::Arc;

use fuchsia_async as fasync;
use fuchsia_inspect::{self as inspect, component, Property};
use fuchsia_syslog::fx_log_err;
use futures::StreamExt;

use crate::agent::{Context, Lifespan, Payload};
use crate::base::{SettingInfo, SettingType};
use crate::blueprint_definition;
use crate::clock;
use crate::handler::base::{Payload as SettingPayload, Request};
use crate::handler::device_storage::DeviceStorageAccess;
use crate::handler::setting_handler::{Event, Payload as HandlerPayload};
use crate::message::base::{filter, Audience, MessageEvent, MessengerType};
use crate::service;
use crate::service::message::Messenger;
use crate::service::TryFromWithClient;

const INSPECT_NODE_NAME: &str = "setting_values";
const SETTING_TYPE_INSPECT_NODE_NAME: &str = "setting_types";

blueprint_definition!(
    "inspect_setting_data",
    crate::agent::inspect_setting_data::InspectSettingAgent::create
);

/// An agent that listens in on messages between the proxy and setting handlers to record the
/// values of all settings to inspect.
pub(crate) struct InspectSettingAgent {
    messenger_client: Messenger,
    inspect_node: inspect::Node,
    setting_values: HashMap<&'static str, InspectSettingInfo>,
    setting_types: HashSet<SettingType>,
    _setting_types_inspect_node: SettingTypesInspectInfo,
}

/// Information about the setting types available in the settings service.
///
/// Inspect nodes are not used, but need to be held as they're deleted from inspect once they go
/// out of scope.
#[derive(Debug)]
struct SettingTypesInspectInfo {
    _node: inspect::Node,
    _value: inspect::StringProperty,
}

/// Information about a setting to be written to inspect.
///
/// Inspect nodes are not used, but need to be held as they're deleted from inspect once they go
/// out of scope.
struct InspectSettingInfo {
    /// Node of this info.
    _node: inspect::Node,

    /// Debug string representation of the value of this setting.
    value: inspect::StringProperty,

    /// Milliseconds since Unix epoch that this setting's value was changed.
    timestamp: inspect::StringProperty,
}

impl DeviceStorageAccess for InspectSettingAgent {
    const STORAGE_KEYS: &'static [&'static str] = &[];
}

impl InspectSettingAgent {
    async fn create(context: Context) {
        Self::create_with_node(
            context,
            component::inspector().root().create_child(INSPECT_NODE_NAME),
            None,
        )
        .await;
    }

    /// Create an agent to listen in on all messages between Proxy and setting
    /// handlers. Agent starts immediately without calling invocation, but
    /// acknowledges the invocation payload to let the Authority know the agent
    /// starts properly.
    async fn create_with_node(
        context: Context,
        inspect_node: inspect::Node,
        custom_inspector: Option<&inspect::Inspector>,
    ) {
        let inspector = custom_inspector.unwrap_or_else(|| component::inspector());

        let (messenger_client, receptor) = match context
            .delegate
            .create(MessengerType::Broker(Some(filter::Builder::single(
                filter::Condition::Custom(Arc::new(move |message| {
                    // Only catch messages that were originally sent from the interfaces, and
                    // that contain a request for the specific setting type we're interested in.
                    matches!(
                        message.payload(),
                        service::Payload::Controller(HandlerPayload::Event(Event::Changed(_)))
                    )
                })),
            ))))
            .await
        {
            Ok(messenger) => messenger,
            Err(err) => {
                fx_log_err!("could not create inspect: {:?}", err);
                return;
            }
        };

        // Add inspect node for the setting types.
        let setting_type_node = inspector.root().create_child(SETTING_TYPE_INSPECT_NODE_NAME);
        let mut setting_types_list: Vec<String> = context
            .available_components
            .clone()
            .iter()
            .map(|component| format!("{:?}", component))
            .collect();
        setting_types_list.sort();
        let setting_types_value =
            setting_type_node.create_string("value", format!("{:?}", setting_types_list));
        let setting_types_inspect_node =
            SettingTypesInspectInfo { _node: setting_type_node, _value: setting_types_value };

        let mut agent = Self {
            messenger_client,
            inspect_node,
            setting_values: HashMap::new(),
            setting_types: context.available_components.clone(),
            _setting_types_inspect_node: setting_types_inspect_node,
        };

        fasync::Task::spawn(async move {
            let event = receptor.fuse();
            let agent_event = context.receptor.fuse();
            futures::pin_mut!(agent_event, event);

            loop {
                futures::select! {
                    message_event = event.select_next_some() => {
                        agent.process_message_event(message_event).await;
                    },
                    agent_message = agent_event.select_next_some() => {
                        if let MessageEvent::Message(
                                service::Payload::Agent(Payload::Invocation(invocation)), client)
                                = agent_message {

                            if invocation.lifespan == Lifespan::Service {
                                agent.fetch_initial_values().await;
                            }

                            // Since the agent runs at creation, there is no
                            // need to handle state here.
                            client.reply(Payload::Complete(Ok(())).into()).send().ack();
                        }
                    },
                }
            }
        })
        .detach();
    }

    /// This function iterates over the available components, requesting the current value with a
    /// Get Request. The returned values combined with updates returned through watching for changes
    /// (which should be registered for observing beforehand) provide full coverage of current
    /// setting values.
    async fn fetch_initial_values(&mut self) {
        for setting_type in self.setting_types.clone() {
            let mut receptor = self
                .messenger_client
                .message(
                    SettingPayload::Request(Request::Get).into(),
                    Audience::Address(service::Address::Handler(setting_type)),
                )
                .send();

            if let Ok((SettingPayload::Response(Ok(Some(setting_info))), _)) =
                receptor.next_of::<SettingPayload>().await
            {
                self.write_setting_to_inspect(setting_info).await;
            } else {
                fx_log_err!("Could not fetch initial value for setting type:{:?}", setting_type);
            }
        }
    }

    /// Identifies [`service::message::MessageEvent`] that contains a changing event from a setting
    /// handler to the proxy and records the setting values.
    async fn process_message_event(&mut self, event: service::message::MessageEvent) {
        if let Ok((HandlerPayload::Event(Event::Changed(setting_info)), _)) =
            HandlerPayload::try_from_with_client(event)
        {
            self.write_setting_to_inspect(setting_info).await;
        }
    }

    /// Writes a setting value to inspect.
    async fn write_setting_to_inspect(&mut self, setting: SettingInfo) {
        let (key, value) = setting.for_inspect();

        let timestamp = clock::inspect_format_now();

        match self.setting_values.get_mut(key) {
            Some(setting) => {
                // Value already known, just update its fields.
                setting.timestamp.set(&timestamp);
                setting.value.set(&value);
            }
            None => {
                // Setting value not recorded yet, create a new inspect node.
                let node = self.inspect_node.create_child(key);
                let value_prop = node.create_string("value", value);
                let timestamp_prop = node.create_string("timestamp", timestamp);
                let _ = self.setting_values.insert(
                    key,
                    InspectSettingInfo {
                        _node: node,
                        value: value_prop,
                        timestamp: timestamp_prop,
                    },
                );
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use fuchsia_inspect::assert_data_tree;
    use fuchsia_zircon::Time;

    use crate::agent::Invocation;
    use crate::base::{SettingInfo, SettingType, UnknownInfo};
    use crate::message::base::Status;
    use crate::message::MessageHubUtil;
    use crate::service::MessageHub;
    use crate::service_context::ServiceContext;

    use super::*;

    async fn create_context() -> Context {
        let hub = MessageHub::create_hub();
        Context::new(
            hub.create(MessengerType::Unbound).await.expect("should be present").1,
            hub,
            vec![SettingType::Unknown].into_iter().collect(),
            HashSet::new(),
            None,
        )
        .await
    }

    // Verifies that inspect agent intercepts a restore request and writes the setting value to
    // inspect.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_write_inspect_on_service_lifespan() {
        // Set the clock so that timestamps will always be 0.
        clock::mock::set(Time::from_nanos(0));

        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child(INSPECT_NODE_NAME);
        let context = create_context().await;
        let delegate = context.delegate.clone();
        let agent_signature = context.receptor.get_signature();

        let (_, mut setting_proxy_receptor) = context
            .delegate
            .create(MessengerType::Addressable(service::Address::Handler(SettingType::Unknown)))
            .await
            .expect("should create proxy");

        InspectSettingAgent::create_with_node(context, inspect_node, Some(&inspector)).await;

        // Inspect agent should not report any setting values.
        assert_data_tree!(inspector, root: {
            setting_types: {
                "value": "[\"Unknown\"]",
            },
            setting_values: {
            }
        });

        // Message service lifespan to agent.
        delegate
            .create(MessengerType::Unbound)
            .await
            .expect("should create messenger")
            .0
            .message(
                Payload::Invocation(Invocation {
                    lifespan: Lifespan::Service,
                    service_context: Arc::new(ServiceContext::new(None, None)),
                })
                .into(),
                Audience::Messenger(agent_signature),
            )
            .send()
            .ack();

        // Setting handler reply to get.
        let (_, reply_client) =
            setting_proxy_receptor.next_payload().await.expect("payload should be present");

        let mut reply_receptor = reply_client
            .reply(SettingPayload::Response(Ok(Some(UnknownInfo(true).into()))).into())
            .send();

        while let Some(message_event) = reply_receptor.next().await {
            if matches!(message_event, service::message::MessageEvent::Status(Status::Acknowledged))
            {
                break;
            }
        }

        // Inspect agent writes value to inspect.
        assert_data_tree!(inspector, root: {
            setting_types: {
                "value": "[\"Unknown\"]",
            },
            setting_values: {
                "Unknown": {
                    value: "UnknownInfo(true)",
                    timestamp: "0.000000000",
                }
            }
        });
    }

    // Verifies that inspect agent intercepts setting change events and writes the setting value
    // to inspect.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_write_inspect_on_changed() {
        // Set the clock so that timestamps will always be 0.
        clock::mock::set(Time::from_nanos(0));

        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child(INSPECT_NODE_NAME);
        let context = create_context().await;
        let delegate = context.delegate.clone();

        let mut proxy_receptor =
            delegate.create(MessengerType::Unbound).await.expect("should create proxy messenger").1;

        InspectSettingAgent::create_with_node(context, inspect_node, Some(&inspector)).await;

        // Inspect agent should not report any setting values.
        assert_data_tree!(inspector, root: {
            setting_types: {
                "value": "[\"Unknown\"]",
            },
            setting_values: {
            }
        });

        // Setting handler notifies proxy of setting changed.
        let _ = delegate
            .create(MessengerType::Unbound)
            .await
            .expect("seting handler should be created")
            .0
            .message(
                HandlerPayload::Event(Event::Changed(SettingInfo::Unknown(UnknownInfo(false))))
                    .into(),
                Audience::Messenger(proxy_receptor.get_signature()),
            )
            .send();

        // Await for the event changed. The agent will intercept it in-between so capturing here
        // will ensure the inspect operation has fully occurred.
        let _payload = proxy_receptor.next_payload().await;

        // Inspect agent writes value to inspect.
        assert_data_tree!(inspector, root: {
            setting_types: {
                "value": "[\"Unknown\"]",
            },
            setting_values: {
                "Unknown": {
                    value: "UnknownInfo(false)",
                    timestamp: "0.000000000",
                }
            }
        });
    }
}
