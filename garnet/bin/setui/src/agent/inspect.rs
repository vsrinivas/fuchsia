// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The inspect mod defines the [`InspectAgent`], which is responsible for logging
//! relevant service activity to Inspect. Since this activity might happen
//! before agent lifecycle states are communicated (due to agent priority
//! ordering), the [`InspectAgent`] begins listening to requests immediately
//! after creation.

use crate::agent::base::Context;
use crate::base::SettingType;
use crate::blueprint_definition;
use crate::clock;
use crate::handler::base::Request;
use crate::internal::agent::Payload;
use crate::internal::switchboard::{Action, Payload as SwitchboardPayload};
use crate::message::base::{MessageEvent, MessengerType};

use fuchsia_async as fasync;
use fuchsia_inspect::{self as inspect, component, Property};
use fuchsia_inspect_derive::{Inspect, WithInspect};
use futures::StreamExt;

use std::collections::{HashMap, VecDeque};
use std::time::{Duration, SystemTime};

blueprint_definition!("inspect", crate::agent::inspect::InspectAgent::create);

const INSPECT_REQUESTS_COUNT: usize = 25;

/// Information about a setting to be written to inspect.
#[derive(Inspect)]
struct SettingTypeInfo {
    /// Map from the name of the Request variant to a RequestTypeInfo that holds a list of
    /// recent requests.
    #[inspect(skip)]
    requests_by_type: HashMap<String, RequestTypeInfo>,

    /// Incrementing count for all requests of this setting type.
    ///
    /// Count is used across all request types to easily see the order that requests occurred in.
    #[inspect(skip)]
    count: u64,

    /// Node of this info.
    inspect_node: inspect::Node,
}

impl SettingTypeInfo {
    fn new() -> Self {
        Self { count: 0, requests_by_type: HashMap::new(), inspect_node: inspect::Node::default() }
    }
}

/// Information for all requests of a particular SettingType variant for a given setting type.
#[derive(Inspect)]
struct RequestTypeInfo {
    /// Last requests for inspect to save. Number of requests is defined by INSPECT_REQUESTS_COUNT.
    #[inspect(skip)]
    last_requests: VecDeque<RequestInfo>,

    /// Node of this info.
    inspect_node: inspect::Node,
}

impl RequestTypeInfo {
    fn new() -> Self {
        Self {
            last_requests: VecDeque::with_capacity(INSPECT_REQUESTS_COUNT),
            inspect_node: inspect::Node::default(),
        }
    }
}

/// Information about a switchboard request to be written to inspect.
///
/// Inspect nodes and properties are not used, but need to be held as they're deleted from inspect
/// once they go out of scope.
#[derive(Inspect)]
struct RequestInfo {
    /// Debug string representation of this Request.
    request: inspect::StringProperty,

    /// Milliseconds since switchboard creation that this request arrived.
    timestamp: inspect::StringProperty,

    /// Node of this info.
    inspect_node: inspect::Node,
}

impl RequestInfo {
    fn new() -> Self {
        Self {
            request: inspect::StringProperty::default(),
            timestamp: inspect::StringProperty::default(),
            inspect_node: inspect::Node::default(),
        }
    }
}

/// The InspectAgent is responsible for listening to requests to the setting
/// handlers and recording the requests to Inspect.
pub struct InspectAgent {
    inspect_node: inspect::Node,
    /// Last requests for inspect to save.
    last_requests: HashMap<SettingType, SettingTypeInfo>,
}

impl InspectAgent {
    async fn create(context: Context) {
        Self::create_with_node(context, component::inspector().root().create_child("switchboard"))
            .await;
    }

    pub async fn create_with_node(context: Context, node: inspect::Node) {
        let (_, message_rx) = context
            .switchboard_messenger_factory
            .create(MessengerType::Broker(None))
            .await
            .expect("should receive client");

        let mut agent = InspectAgent { inspect_node: node, last_requests: HashMap::new() };

        fasync::Task::spawn(async move {
            let switchboard_event = message_rx.fuse();
            let agent_event = context.receptor.fuse();
            futures::pin_mut!(switchboard_event, agent_event);

            loop {
                futures::select! {
                    switchboard_message = switchboard_event.select_next_some() => {
                        if let MessageEvent::Message(SwitchboardPayload::Action(
                                Action::Request(setting_type, request)), client)
                                    = switchboard_message {
                            agent.record_request(setting_type, request);
                        }
                    },
                    agent_message = agent_event.select_next_some() => {
                        if let MessageEvent::Message(Payload::Invocation(invocation), client)
                                = agent_message {
                            // Since the agent runs at creation, there is no
                            // need to handle state here.
                            client.reply(Payload::Complete(Ok(()))).send().ack();
                        }
                    },
                }
            }
        })
        .detach();
    }

    /// Write a request to inspect.
    fn record_request(&mut self, setting_type: SettingType, request: Request) {
        let inspect_node = &self.inspect_node;
        let setting_type_info = self.last_requests.entry(setting_type).or_insert_with(|| {
            SettingTypeInfo::new()
                .with_inspect(&inspect_node, format!("{:?}", setting_type))
                // `with_inspect` will only return an error on types with
                // interior mutability. Since none are used here, this should be
                // fine.
                .expect("failed to create SettingTypeInfo inspect node")
        });

        let key = request.for_inspect().to_string();
        let setting_type_inspect_node = &setting_type_info.inspect_node;
        let request_type_info =
            setting_type_info.requests_by_type.entry(key.clone()).or_insert_with(|| {
                // `with_inspect` will only return an error on types with
                // interior mutability. Since none are used here, this
                // should be fine.
                RequestTypeInfo::new()
                    .with_inspect(setting_type_inspect_node, key)
                    .expect("failed to create RequestTypeInfo inspect node")
            });

        let last_requests = &mut request_type_info.last_requests;
        if last_requests.len() >= INSPECT_REQUESTS_COUNT {
            last_requests.pop_back();
        }

        let count = setting_type_info.count;
        setting_type_info.count += 1;
        let timestamp = clock::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .as_ref()
            .map(Duration::as_millis)
            .unwrap_or(0);
        // std::u64::MAX maxes out at 20 digits.
        let request_info = RequestInfo::new()
            .with_inspect(&request_type_info.inspect_node, format!("{:020}", count))
            .expect("failed to create RequestInfo inspect node");
        request_info.request.set(&format!("{:?}", request));
        request_info.timestamp.set(&timestamp.to_string());
        last_requests.push_front(request_info);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::agent::base::Descriptor;
    use crate::internal::agent;
    use crate::internal::event;
    use crate::internal::switchboard;
    use crate::intl::types::{IntlInfo, LocaleId, TemperatureUnit};
    use crate::message::base::Audience;

    use fuchsia_inspect::assert_inspect_tree;
    use fuchsia_inspect::testing::{AnyProperty, TreeAssertion};

    use std::collections::HashSet;

    async fn send_request_and_wait(
        messenger: &switchboard::message::Messenger,
        switchboard_receptor: &mut switchboard::message::Receptor,
        setting_type: SettingType,
        setting_request: Request,
    ) {
        messenger
            .message(
                switchboard::Payload::Action(switchboard::Action::Request(
                    setting_type,
                    setting_request,
                )),
                Audience::Address(switchboard::Address::Switchboard),
            )
            .send()
            .ack();

        switchboard_receptor.next_payload().await.ok();
    }

    async fn create_context() -> Context {
        Context::new(
            agent::message::create_hub()
                .create(MessengerType::Unbound)
                .await
                .expect("should be present")
                .1,
            Descriptor::new("test_component"),
            switchboard::message::create_hub(),
            event::message::create_hub(),
            HashSet::new(),
            None,
        )
        .await
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_inspect() {
        clock::mock::set(SystemTime::UNIX_EPOCH);

        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child("switchboard");
        let context = create_context().await;

        let (messenger, _) =
            context.switchboard_messenger_factory.create(MessengerType::Unbound).await.unwrap();
        let (_, mut switchboard_receptor) = context
            .switchboard_messenger_factory
            .create(MessengerType::Addressable(switchboard::Address::Switchboard))
            .await
            .expect("should be created");

        let _agent = InspectAgent::create_with_node(context, inspect_node).await;

        // Send a few requests to make sure they get written to inspect properly.
        send_request_and_wait(
            &messenger,
            &mut switchboard_receptor,
            SettingType::Display,
            Request::SetAutoBrightness(false),
        )
        .await;

        send_request_and_wait(
            &messenger,
            &mut switchboard_receptor,
            SettingType::Display,
            Request::SetAutoBrightness(false),
        )
        .await;

        send_request_and_wait(
            &messenger,
            &mut switchboard_receptor,
            SettingType::Intl,
            Request::SetIntlInfo(IntlInfo {
                locales: Some(vec![LocaleId { id: "en-US".to_string() }]),
                temperature_unit: Some(TemperatureUnit::Celsius),
                time_zone_id: Some("UTC".to_string()),
                hour_cycle: None,
            }),
        )
        .await;

        assert_inspect_tree!(inspector, root: {
            switchboard: {
                "Display": {
                    "SetAutoBrightness": {
                        "00000000000000000000": {
                            request: "SetAutoBrightness(false)",
                            timestamp: "0",
                        },
                        "00000000000000000001": {
                            request: "SetAutoBrightness(false)",
                            timestamp: "0",
                        },
                    },
                },
                "Intl": {
                    "SetIntlInfo": {
                        "00000000000000000000": {
                            request: "SetIntlInfo(IntlInfo { \
                                locales: Some([LocaleId { id: \"en-US\" }]), \
                                temperature_unit: Some(Celsius), \
                                time_zone_id: Some(\"UTC\"), \
                                hour_cycle: None })",
                            timestamp: "0",
                        }
                    },
                }
            }
        });
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_inspect_mixed_request_types() {
        clock::mock::set(SystemTime::UNIX_EPOCH);

        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child("switchboard");
        let context = create_context().await;

        let (messenger, _) =
            context.switchboard_messenger_factory.create(MessengerType::Unbound).await.unwrap();
        let (_, mut switchboard_receptor) = context
            .switchboard_messenger_factory
            .create(MessengerType::Addressable(switchboard::Address::Switchboard))
            .await
            .expect("should be created");

        let _agent = InspectAgent::create_with_node(context, inspect_node).await;

        // Interlace different request types to make sure the counter is correct.
        send_request_and_wait(
            &messenger,
            &mut switchboard_receptor,
            SettingType::Display,
            Request::SetAutoBrightness(false),
        )
        .await;

        send_request_and_wait(
            &messenger,
            &mut switchboard_receptor,
            SettingType::Display,
            Request::Get,
        )
        .await;

        send_request_and_wait(
            &messenger,
            &mut switchboard_receptor,
            SettingType::Display,
            Request::SetAutoBrightness(true),
        )
        .await;

        send_request_and_wait(
            &messenger,
            &mut switchboard_receptor,
            SettingType::Display,
            Request::Get,
        )
        .await;

        assert_inspect_tree!(inspector, root: {
            switchboard: {
                "Display": {
                    "SetAutoBrightness": {
                        "00000000000000000000": {
                            request: "SetAutoBrightness(false)",
                            timestamp: "0",
                        },
                        "00000000000000000002": {
                            request: "SetAutoBrightness(true)",
                            timestamp: "0",
                        },
                    },
                    "Get": {
                        "00000000000000000001": {
                            request: "Get",
                            timestamp: "0",
                        },
                        "00000000000000000003": {
                            request: "Get",
                            timestamp: "0",
                        },
                    },
                },
            }
        });
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn inspect_queue_test() {
        clock::mock::set(SystemTime::UNIX_EPOCH);

        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child("switchboard");
        let context = create_context().await;

        let (messenger, _) =
            context.switchboard_messenger_factory.create(MessengerType::Unbound).await.unwrap();
        let (_, mut switchboard_receptor) = context
            .switchboard_messenger_factory
            .create(MessengerType::Addressable(switchboard::Address::Switchboard))
            .await
            .expect("should be created");

        let _agent = InspectAgent::create_with_node(context, inspect_node).await;

        send_request_and_wait(
            &messenger,
            &mut switchboard_receptor,
            SettingType::Intl,
            Request::SetIntlInfo(IntlInfo {
                locales: Some(vec![LocaleId { id: "en-US".to_string() }]),
                temperature_unit: Some(TemperatureUnit::Celsius),
                time_zone_id: Some("UTC".to_string()),
                hour_cycle: None,
            }),
        )
        .await;

        // Send one more than the max requests to make sure they get pushed off the end of the queue
        for _ in 0..INSPECT_REQUESTS_COUNT + 1 {
            send_request_and_wait(
                &messenger,
                &mut switchboard_receptor,
                SettingType::Display,
                Request::SetAutoBrightness(false),
            )
            .await;
        }

        // Ensures we have INSPECT_REQUESTS_COUNT items and that the queue dropped the earliest one
        // when hitting the limit.
        fn display_subtree_assertion() -> TreeAssertion {
            let mut tree_assertion = TreeAssertion::new("Display", true);
            let mut request_assertion = TreeAssertion::new("SetAutoBrightness", true);

            for i in 1..INSPECT_REQUESTS_COUNT + 1 {
                request_assertion
                    .add_child_assertion(TreeAssertion::new(&format!("{:020}", i), false));
            }
            tree_assertion.add_child_assertion(request_assertion);
            tree_assertion
        }

        assert_inspect_tree!(inspector, root: {
            switchboard: {
                display_subtree_assertion(),
                "Intl": {
                    "SetIntlInfo": {
                        "00000000000000000000": {
                            request: AnyProperty,
                            timestamp: "0",
                        }
                    }
                }
            }
        });
    }
}
