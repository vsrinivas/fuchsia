// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The usage_counts mod defines the [SettingTypeUsageInspectAgent], which is responsible for counting
//! relevant API usages to Inspect. Since API usages might happen before agent lifecycle states are
//! communicated (due to agent priority ordering), the [SettingTypeUsageInspectAgent] begins
//! listening to requests immediately after creation.
//!

use crate::agent::Context;
use crate::agent::Payload;
use crate::base::SettingType;
use crate::blueprint_definition;
use crate::handler::base::{Payload as HandlerPayload, Request};
use crate::message::base::{filter, MessageEvent, MessengerType};
use crate::service::TryFromWithClient;
use crate::{service, trace};
use fuchsia_async as fasync;
use fuchsia_inspect::{self as inspect, component};
use fuchsia_inspect_derive::Inspect;
use futures::StreamExt;
use inspect::NumericProperty;
use settings_inspect_utils::managed_inspect_map::ManagedInspectMap;
use std::collections::HashMap;
use std::sync::Arc;

blueprint_definition!(
    "usage_counts",
    crate::agent::inspect::usage_counts::SettingTypeUsageInspectAgent::create
);

/// Information about a setting type usage count to be written to inspect.
struct SettingTypeUsageInspectInfo {
    /// Map from the name of the Request variant to its calling counts.
    requests_by_type: ManagedInspectMap<UsageInfo>,
}

impl SettingTypeUsageInspectInfo {
    fn new(parent: &inspect::Node, setting_type_str: &str) -> Self {
        Self {
            requests_by_type: ManagedInspectMap::<UsageInfo>::with_node(
                parent.create_child(setting_type_str),
            ),
        }
    }
}

#[derive(Default, Inspect)]
struct UsageInfo {
    /// Node of this info.
    inspect_node: inspect::Node,

    /// Call counts of the current API.
    count: inspect::IntProperty,
}

/// The SettingTypeUsageInspectAgent is responsible for listening to requests to the setting
/// handlers and recording the related API usage counts to Inspect.
pub(crate) struct SettingTypeUsageInspectAgent {
    /// Node of this info.
    inspect_node: inspect::Node,

    /// Mapping from SettingType key to api usage counts.
    api_call_counts: HashMap<String, SettingTypeUsageInspectInfo>,
}

impl SettingTypeUsageInspectAgent {
    async fn create(context: Context) {
        Self::create_with_node(
            context,
            component::inspector().root().create_child("api_usage_counts"),
        )
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

        let mut agent =
            SettingTypeUsageInspectAgent { inspect_node: node, api_call_counts: HashMap::new() };

        fasync::Task::spawn({
            async move {
            let _ = &context;
            let id = fuchsia_trace::Id::new();
            trace!(id, "usage_counts_inspect_agent");
            let event = message_rx.fuse();
            let agent_event = context.receptor.fuse();
            futures::pin_mut!(agent_event, event);

            loop {
                futures::select! {
                    message_event = event.select_next_some() => {
                        trace!(
                            id,
                            "message_event"
                        );
                        agent.process_message_event(message_event);
                    },
                    agent_message = agent_event.select_next_some() => {
                        trace!(
                            id,
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
        }})
        .detach();
    }

    /// Identifies [`service::message::MessageEvent`] that contains a [`Request`]
    /// for setting handlers and counts [`Request`] to its API usage.
    fn process_message_event(&mut self, event: service::message::MessageEvent) {
        if let Ok((HandlerPayload::Request(request), client)) =
            HandlerPayload::try_from_with_client(event)
        {
            for target in client.get_audience().flatten() {
                if let service::message::Audience::Address(service::Address::Handler(
                    setting_type,
                )) = target
                {
                    self.record_usage(setting_type, &request);
                }
            }
        }
    }

    /// Write a usage count to inspect.
    fn record_usage(&mut self, setting_type: SettingType, request: &Request) {
        let inspect_node = &self.inspect_node;
        let setting_type_str = format!("{:?}", setting_type);
        let setting_type_info = self
            .api_call_counts
            .entry(setting_type_str.clone())
            .or_insert_with(|| SettingTypeUsageInspectInfo::new(inspect_node, &setting_type_str));

        let mut key = request.for_inspect();
        if key.starts_with("Set") {
            // Match all Set* requests to Set
            key = "Set";
        }
        let usage = setting_type_info
            .requests_by_type
            .get_or_insert_with(key.to_string(), UsageInfo::default);
        usage.count.add(1);
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

            let _ = messenger
                .message(
                    HandlerPayload::Request(setting_request).into(),
                    service::message::Audience::Address(service::Address::Handler(setting_type)),
                )
                .send();

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
        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child("api_usage_counts");
        let context = create_context().await;

        let request_processor = RequestProcessor::new(context.delegate.clone());

        SettingTypeUsageInspectAgent::create_with_node(context, inspect_node).await;

        // Send a few requests to make sure they get written to inspect properly.
        let turn_off_auto_brightness = Request::SetDisplayInfo(SetDisplayInfo {
            auto_brightness: Some(false),
            ..SetDisplayInfo::default()
        });
        request_processor
            .send_and_receive(SettingType::Display, turn_off_auto_brightness.clone())
            .await;

        request_processor.send_and_receive(SettingType::Display, turn_off_auto_brightness).await;

        for _ in 0..100 {
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
        }

        assert_data_tree!(inspector, root: {
            api_usage_counts: {
                "Display": {
                    "Set": {
                        count: 2i64,
                    },
                },
                "Intl": {
                    "Set": {
                       count: 100i64
                    },
                }
            },
        });
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_inspect_mixed_request_types() {
        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child("api_usage_counts");
        let context = create_context().await;

        let request_processor = RequestProcessor::new(context.delegate.clone());

        let _agent = SettingTypeUsageInspectAgent::create_with_node(context, inspect_node).await;

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
            api_usage_counts: {
                "Display": {
                    "Set": {
                        count: 2i64,
                    },
                    "Get": {
                        count: 2i64,
                    },
                },
            }
        });
    }
}
