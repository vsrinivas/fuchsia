// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The inspect mod defines the [InspectAgent], which is responsible for logging
//! relevant service activity to Inspect. Since this activity might happen
//! before agent lifecycle states are communicated (due to agent priority
//! ordering), the [InspectAgent] begins listening to requests immediately
//! after creation.
//!
//! [InspectAgent]: inspect::InspectAgent

use crate::agent::Context;
use crate::agent::Payload;
use crate::base::SettingType;
use crate::blueprint_definition;
use crate::clock;
use crate::handler::base::{Payload as HandlerPayload, Request};
use crate::handler::device_storage::DeviceStorageAccess;
use crate::message::base::{filter, MessageEvent, MessengerType};
use crate::service::TryFromWithClient;
use crate::{service, trace};

use fuchsia_async as fasync;
use fuchsia_inspect::{self as inspect, component, Property};
use fuchsia_inspect_derive::{Inspect, WithInspect};
use futures::StreamExt;

use std::collections::{HashMap, VecDeque};
use std::sync::Arc;

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

/// Information about a request to be written to inspect.
#[derive(Inspect)]
struct RequestInfo {
    /// Debug string representation of this Request.
    request: inspect::StringProperty,

    /// Milliseconds since creation that this request arrived.
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
pub(crate) struct InspectAgent {
    inspect_node: inspect::Node,
    /// Last requests for inspect to save.
    last_requests: HashMap<SettingType, SettingTypeInfo>,
}

impl DeviceStorageAccess for InspectAgent {
    const STORAGE_KEYS: &'static [&'static str] = &[];
}

impl InspectAgent {
    async fn create(context: Context) {
        // TODO(fxbug.dev/71295): Rename child node as switchboard is no longer in use.
        Self::create_with_node(context, component::inspector().root().create_child("switchboard"))
            .await;
    }

    async fn create_with_node(context: Context, node: inspect::Node) {
        let (_, message_rx) = context
            .delegate
            .create(MessengerType::Broker(Some(filter::Builder::single(
                filter::Condition::Custom(Arc::new(move |message| {
                    // Only catch setting handler requests.
                    matches!(
                        message.payload(),
                        service::Payload::Setting(HandlerPayload::Request(_))
                    )
                })),
            ))))
            .await
            .expect("should receive client");

        let mut agent = InspectAgent { inspect_node: node, last_requests: HashMap::new() };

        fasync::Task::spawn(async move {
            let nonce = fuchsia_trace::generate_nonce();
            trace!(nonce, "inspect_agent");
            let event = message_rx.fuse();
            let agent_event = context.receptor.fuse();
            futures::pin_mut!(agent_event, event);

            loop {
                futures::select! {
                    message_event = event.select_next_some() => {
                        trace!(
                            nonce,

                            "message_event"
                        );
                        agent.process_message_event(message_event);
                    },
                    agent_message = agent_event.select_next_some() => {
                        trace!(
                            nonce,

                            "agent_event"
                        );
                        if let MessageEvent::Message(
                                service::Payload::Agent(Payload::Invocation(_invocation)), client)
                                = agent_message {
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

    /// Identfies [`service::message::MessageEvent`] that contains a [`Request`]
    /// for setting handlers and records the [`Request`].
    fn process_message_event(&mut self, event: service::message::MessageEvent) {
        if let Ok((HandlerPayload::Request(request), client)) =
            HandlerPayload::try_from_with_client(event)
        {
            for target in client.get_audience().flatten() {
                if let service::message::Audience::Address(service::Address::Handler(
                    setting_type,
                )) = target
                {
                    self.record_request(setting_type, &request);
                }
            }
        }
    }

    /// Write a request to inspect.
    fn record_request(&mut self, setting_type: SettingType, request: &Request) {
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
            let _ = last_requests.pop_back();
        }

        let count = setting_type_info.count;
        setting_type_info.count += 1;
        let timestamp = clock::inspect_format_now();
        // std::u64::MAX maxes out at 20 digits.
        let request_info = RequestInfo::new()
            .with_inspect(&request_type_info.inspect_node, format!("{:020}", count))
            .expect("failed to create RequestInfo inspect node");
        request_info.request.set(&format!("{:?}", request));
        request_info.timestamp.set(&timestamp);
        last_requests.push_front(request_info);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::display::types::SetDisplayInfo;
    use crate::intl::types::{IntlInfo, LocaleId, TemperatureUnit};
    use crate::message::MessageHubUtil;
    use crate::service;

    use fuchsia_inspect::assert_data_tree;
    use fuchsia_inspect::testing::{AnyProperty, TreeAssertion};
    use fuchsia_zircon::Time;
    use std::collections::HashSet;

    /// The `RequestProcessor` handles sending a request through a MessageHub
    /// From caller to recipient. This is useful when testing brokers in
    /// between.
    struct RequestProcessor {
        delegate: service::message::Delegate,
    }

    impl RequestProcessor {
        fn new(delegate: service::message::Delegate) -> Self {
            RequestProcessor { delegate }
        }

        async fn send_and_receive(&self, setting_type: SettingType, setting_request: Request) {
            let (messenger, _) =
                self.delegate.create(MessengerType::Unbound).await.expect("should be created");
            let (_, mut receptor) = self
                .delegate
                .create(MessengerType::Addressable(service::Address::Handler(setting_type)))
                .await
                .expect("should be created");

            messenger
                .message(
                    HandlerPayload::Request(setting_request).into(),
                    service::message::Audience::Address(service::Address::Handler(setting_type)),
                )
                .send()
                .ack();

            let _ = receptor.next_payload().await;
        }
    }

    async fn create_context() -> Context {
        Context::new(
            service::MessageHub::create_hub()
                .create(MessengerType::Unbound)
                .await
                .expect("should be present")
                .1,
            service::MessageHub::create_hub(),
            HashSet::new(),
            HashSet::new(),
            None,
        )
        .await
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_inspect() {
        // Set the clock so that timestamps will always be 0.
        clock::mock::set(Time::from_nanos(0));

        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child("switchboard");
        let context = create_context().await;

        let request_processor = RequestProcessor::new(context.delegate.clone());

        InspectAgent::create_with_node(context, inspect_node).await;

        // Send a few requests to make sure they get written to inspect properly.
        let turn_off_auto_brightness = Request::SetDisplayInfo(SetDisplayInfo {
            auto_brightness: Some(false),
            ..SetDisplayInfo::default()
        });
        request_processor
            .send_and_receive(SettingType::Display, turn_off_auto_brightness.clone())
            .await;

        request_processor.send_and_receive(SettingType::Display, turn_off_auto_brightness).await;

        request_processor
            .send_and_receive(
                SettingType::Intl,
                Request::SetIntlInfo(IntlInfo {
                    locales: Some(vec![LocaleId { id: "en-US".to_string() }]),
                    temperature_unit: Some(TemperatureUnit::Celsius),
                    time_zone_id: Some("UTC".to_string()),
                    hour_cycle: None,
                }),
            )
            .await;

        assert_data_tree!(inspector, root: {
            switchboard: {
                "Display": {
                    "SetDisplayInfo": {
                        "00000000000000000000": {
                            request: "SetDisplayInfo(SetDisplayInfo { \
                                manual_brightness_value: None, \
                                auto_brightness_value: None, \
                                auto_brightness: Some(false), \
                                screen_enabled: None, \
                                low_light_mode: None, \
                                theme: None \
                            })",
                            timestamp: "0.000000000",
                        },
                        "00000000000000000001": {
                            request: "SetDisplayInfo(SetDisplayInfo { \
                                manual_brightness_value: None, \
                                auto_brightness_value: None, \
                                auto_brightness: Some(false), \
                                screen_enabled: None, \
                                low_light_mode: None, \
                                theme: None \
                            })",
                            timestamp: "0.000000000",
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
                            timestamp: "0.000000000",
                        }
                    },
                }
            }
        });
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_inspect_mixed_request_types() {
        // Set the clock so that timestamps will always be 0.
        clock::mock::set(Time::from_nanos(0));

        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child("switchboard");
        let context = create_context().await;

        let request_processor = RequestProcessor::new(context.delegate.clone());

        let _agent = InspectAgent::create_with_node(context, inspect_node).await;

        // Interlace different request types to make sure the counter is correct.
        request_processor
            .send_and_receive(
                SettingType::Display,
                Request::SetDisplayInfo(SetDisplayInfo {
                    auto_brightness: Some(false),
                    ..SetDisplayInfo::default()
                }),
            )
            .await;

        request_processor.send_and_receive(SettingType::Display, Request::Get).await;

        request_processor
            .send_and_receive(
                SettingType::Display,
                Request::SetDisplayInfo(SetDisplayInfo {
                    auto_brightness: Some(true),
                    ..SetDisplayInfo::default()
                }),
            )
            .await;

        request_processor.send_and_receive(SettingType::Display, Request::Get).await;

        assert_data_tree!(inspector, root: {
            switchboard: {
                "Display": {
                    "SetDisplayInfo": {
                        "00000000000000000000": {
                            request: "SetDisplayInfo(SetDisplayInfo { \
                                manual_brightness_value: None, \
                                auto_brightness_value: None, \
                                auto_brightness: Some(false), \
                                screen_enabled: None, \
                                low_light_mode: None, \
                                theme: None \
                            })",
                            timestamp: "0.000000000",
                        },
                        "00000000000000000002": {
                            request: "SetDisplayInfo(SetDisplayInfo { \
                                manual_brightness_value: None, \
                                auto_brightness_value: None, \
                                auto_brightness: Some(true), \
                                screen_enabled: None, \
                                low_light_mode: None, \
                                theme: None \
                            })",
                            timestamp: "0.000000000",
                        },
                    },
                    "Get": {
                        "00000000000000000001": {
                            request: "Get",
                            timestamp: "0.000000000",
                        },
                        "00000000000000000003": {
                            request: "Get",
                            timestamp: "0.000000000",
                        },
                    },
                },
            }
        });
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn inspect_queue_test() {
        // Set the clock so that timestamps will always be 0.
        clock::mock::set(Time::from_nanos(0));
        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child("switchboard");
        let context = create_context().await;
        let request_processor = RequestProcessor::new(context.delegate.clone());

        let _agent = InspectAgent::create_with_node(context, inspect_node).await;

        request_processor
            .send_and_receive(
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
            request_processor
                .send_and_receive(
                    SettingType::Display,
                    Request::SetDisplayInfo(SetDisplayInfo {
                        auto_brightness: Some(false),
                        ..SetDisplayInfo::default()
                    }),
                )
                .await;
        }

        // Ensures we have INSPECT_REQUESTS_COUNT items and that the queue dropped the earliest one
        // when hitting the limit.
        fn display_subtree_assertion() -> TreeAssertion {
            let mut tree_assertion = TreeAssertion::new("Display", true);
            let mut request_assertion = TreeAssertion::new("SetDisplayInfo", true);

            for i in 1..INSPECT_REQUESTS_COUNT + 1 {
                request_assertion
                    .add_child_assertion(TreeAssertion::new(&format!("{:020}", i), false));
            }
            tree_assertion.add_child_assertion(request_assertion);
            tree_assertion
        }

        assert_data_tree!(inspector, root: {
            switchboard: {
                display_subtree_assertion(),
                "Intl": {
                    "SetIntlInfo": {
                        "00000000000000000000": {
                            request: AnyProperty,
                            timestamp: "0.000000000",
                        }
                    }
                }
            }
        });
    }
}
