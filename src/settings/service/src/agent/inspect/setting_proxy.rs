// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The inspect mod defines the [SettingProxyInspectAgent], which is responsible for logging
//! the contents of requests and responses, as well as timestamps and counts to Inspect. Since this
//! activity might happen before agent lifecycle states are communicated (due to agent priority
//! ordering), the [SettingProxyInspectAgent] begins listening to requests immediately after
//! creation.
//!
//! [SettingProxyInspectAgent]: inspect::SettingProxyInspectAgent

use crate::agent::Context;
use crate::agent::Payload;
use crate::base::{SettingInfo, SettingType};
use crate::blueprint_definition;
use crate::clock;
use crate::handler::base::{Error, Payload as HandlerPayload, Request};
use crate::inspect::utils::enums::ResponseType;
use crate::message::base::{filter, MessageEvent, MessengerType};
use crate::message::receptor::Receptor;
use crate::service::TryFromWithClient;
use crate::{service, trace};
use settings_inspect_utils::joinable_inspect_vecdeque::JoinableInspectVecDeque;
use settings_inspect_utils::managed_inspect_map::ManagedInspectMap;
use settings_inspect_utils::managed_inspect_queue::ManagedInspectQueue;

use fuchsia_async as fasync;
use fuchsia_inspect::{self as inspect, component, NumericProperty};
use fuchsia_inspect_derive::{IValue, Inspect};
use futures::stream::FuturesUnordered;
use futures::StreamExt;
use std::sync::Arc;

blueprint_definition!(
    "setting_proxy",
    crate::agent::inspect::setting_proxy::SettingProxyInspectAgent::create
);

/// The maximum number of pending requests to store in inspect per setting. There should generally
/// be fairly few of these unless a setting is changing rapidly, so a slightly larger size allows us
/// to avoid dropping requests.
const MAX_PENDING_REQUESTS: usize = 20;

/// The maximum number of unique request + response pairs to store per request type in each setting.
const MAX_REQUEST_RESPONSE_PAIRS: usize = 10;

/// The maximum number of request or response timestamps to store per request + response pair.
const MAX_REQUEST_RESPONSE_TIMESTAMPS: usize = 10;

/// Name of the top-level node under root used to store the contents of requests and responses.
const REQUEST_RESPONSE_NODE_NAME: &str = "requests_and_responses";

/// Name of the top-level node under root used to store request counts.
const RESPONSE_COUNTS_NODE_NAME: &str = "response_counts";

#[derive(Default, Inspect)]
/// Information about response counts to be written to inspect.
struct SettingTypeResponseCountInfo {
    /// Map from the name of the ResponseType variant to a ResponseCountInfo that holds the number
    /// of occurrences of that response.
    #[inspect(forward)]
    response_counts_by_type: ManagedInspectMap<ResponseTypeCount>,
}

#[derive(Default, Inspect)]
/// Information about the number of responses of a given response type.
struct ResponseTypeCount {
    count: inspect::UintProperty,
    inspect_node: inspect::Node,
}

/// Inspect information on the requests and responses of one setting.
#[derive(Inspect)]
struct SettingTypeRequestResponseInfo {
    /// Map from request type to a map containing [RequestResponsePairInfo].
    ///
    /// The first-level map's keys are the enum variant names from [Request::to_inspect], to allow
    /// different request types to be recorded separately. The second-level map's keys are the
    /// concatenation of the debug representation of the request and response and holds up to
    /// [REQUEST_RESPONSE_COUNT] of the most recently seen unique request + response pairs.
    #[inspect(rename = "requests_and_responses")]
    requests_and_responses_by_type: ManagedInspectMap<ManagedInspectMap<RequestResponsePairInfo>>,

    /// Queue of pending requests that have been sent but have not received a response yet.
    pending_requests: ManagedInspectQueue<PendingRequestInspectInfo>,

    /// Inspect node to which this setting's data is written.
    inspect_node: inspect::Node,

    /// Incrementing count for all requests of this setting type.
    ///
    /// The same counter is used across all request types to easily see the order that requests
    /// occurred in.
    #[inspect(skip)]
    count: u64,
}

impl SettingTypeRequestResponseInfo {
    fn new() -> Self {
        Self {
            requests_and_responses_by_type: Default::default(),
            pending_requests: ManagedInspectQueue::<PendingRequestInspectInfo>::new(
                MAX_PENDING_REQUESTS,
            ),
            inspect_node: Default::default(),
            count: 0,
        }
    }
}

/// Information to be written to inspect about a request that has not yet received a response.
#[derive(Debug, Default, Inspect)]
struct PendingRequestInspectInfo {
    /// Debug string representation of the request.
    request: IValue<String>,

    /// The request type of the request, from [Request::to_inspect]. Used to bucket by request type
    /// when recording responses.
    #[inspect(skip)]
    request_type: String,

    /// Time this request was sent, in milliseconds of uptime. Uses the system monotonic clock
    /// (zx_clock_get_monotonic).
    timestamp: IValue<String>,

    /// Request count within the setting for this request.
    #[inspect(skip)]
    count: u64,

    /// Inspect node this request will be recorded at.
    inspect_node: inspect::Node,
}

/// Information about a request and response pair to be written to inspect.
///
/// Timestamps are recorded upon receiving a response, so [request_timestamp] and
/// [response_timestamp] will always be the same length and the timestamps at index N of each array
/// belong to the same request + response round trip.
#[derive(Default, Inspect)]
struct RequestResponsePairInfo {
    /// Debug string representation of the request.
    request: IValue<String>,

    /// Debug string representation of the response.
    response: IValue<String>,

    /// Request count of the most recently received request + response.
    #[inspect(skip)]
    request_count: u64,

    /// List of timestamps at which this request was seen.
    ///
    /// Timestamps are recorded as milliseconds of system uptime. Uses the system monotonic clock
    /// (zx_clock_get_monotonic).
    request_timestamps: IValue<JoinableInspectVecDeque>,

    /// List of timestamps at which this response was seen.
    ///
    /// Timestamps are recorded as milliseconds of system uptime. Uses the system monotonic clock
    /// (zx_clock_get_monotonic).
    response_timestamps: IValue<JoinableInspectVecDeque>,

    /// Inspect node at which this info is stored.
    inspect_node: inspect::Node,
}

impl RequestResponsePairInfo {
    fn new(request: String, response: String, count: u64) -> Self {
        Self {
            request: IValue::new(request),
            response: IValue::new(response),
            request_count: count,
            request_timestamps: Default::default(),
            response_timestamps: Default::default(),
            inspect_node: Default::default(),
        }
    }
}

/// The SettingProxyInspectAgent is responsible for listening to requests to the setting
/// handlers and recording the requests and responses to Inspect.
pub(crate) struct SettingProxyInspectAgent {
    /// Response type accumulation info.
    response_counts: ManagedInspectMap<SettingTypeResponseCountInfo>,

    /// Information for each setting on requests and responses.
    setting_request_response_info: ManagedInspectMap<SettingTypeRequestResponseInfo>,
}

impl SettingProxyInspectAgent {
    async fn create(context: Context) {
        Self::create_with_node(
            context,
            component::inspector().root().create_child(REQUEST_RESPONSE_NODE_NAME),
            component::inspector().root().create_child(RESPONSE_COUNTS_NODE_NAME),
        )
        .await;
    }

    async fn create_with_node(
        context: Context,
        request_response_inspect_node: inspect::Node,
        response_counts_node: inspect::Node,
    ) {
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

        let mut agent = SettingProxyInspectAgent {
            response_counts: ManagedInspectMap::<SettingTypeResponseCountInfo>::with_node(
                response_counts_node,
            ),
            setting_request_response_info:
                ManagedInspectMap::<SettingTypeRequestResponseInfo>::with_node(
                    request_response_inspect_node,
                ),
        };

        fasync::Task::spawn({
            async move {
            let _ = &context;
            let id = fuchsia_trace::Id::new();
            trace!(id, "setting_proxy_inspect_agent");
            let event = message_rx.fuse();
            let agent_event = context.receptor.fuse();
            futures::pin_mut!(agent_event, event);

            // Push reply_receptor to the FutureUnordered to avoid blocking codes when there are no
            // responses replied back.
            let mut unordered = FuturesUnordered::new();
            loop {
                futures::select! {
                    message_event = event.select_next_some() => {
                        trace!(
                            id,
                            "message_event"
                        );
                        if let Some((setting_type, count, mut reply_receptor)) =
                            agent.process_message_event(message_event) {
                                unordered.push(async move {
                                    let payload = reply_receptor.next_payload().await;
                                    (setting_type, count, payload)
                                });
                        };
                    },
                    reply = unordered.select_next_some() => {
                        let (setting_type, count, payload) = reply;
                        if let Ok((
                            service::Payload::Setting(
                                HandlerPayload::Response(response)),
                            _,
                        )) = payload
                        {
                            agent.record_response(setting_type, count, response);
                        }
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

    /// Identfies [`service::message::MessageEvent`] that contains a [`Request`]
    /// for setting handlers and records the [`Request`].
    fn process_message_event(
        &mut self,
        event: service::message::MessageEvent,
    ) -> Option<(SettingType, u64, Receptor<service::Payload, service::Address, service::Role>)>
    {
        if let Ok((HandlerPayload::Request(request), mut client)) =
            HandlerPayload::try_from_with_client(event)
        {
            for target in client.get_audience().flatten() {
                if let service::message::Audience::Address(service::Address::Handler(
                    setting_type,
                )) = target
                {
                    // A Listen request will always send a Get request. We can always get the Get's
                    // response. However, Listen will return the Get's response only when it is
                    // considered updated. Therefore, we ignore Listen response.
                    if request != Request::Listen {
                        let count = self.record_request(setting_type, request);
                        return Some((setting_type, count, client.spawn_observer()));
                    }
                }
            }
        }
        None
    }

    /// Writes a pending request to inspect.
    ///
    /// Returns the request count of this request.
    fn record_request(&mut self, setting_type: SettingType, request: Request) -> u64 {
        let setting_type_str = format!("{:?}", setting_type);
        let timestamp = clock::inspect_format_now();

        // Get or create the info for this setting type.
        let request_response_info = self
            .setting_request_response_info
            .get_or_insert_with(setting_type_str, SettingTypeRequestResponseInfo::new);

        let request_count = request_response_info.count;
        request_response_info.count += 1;

        let pending_request_info = PendingRequestInspectInfo {
            request: format!("{:?}", request).into(),
            request_type: request.for_inspect().to_string(),
            timestamp: timestamp.into(),
            count: request_count,
            inspect_node: inspect::Node::default(),
        };

        let count_key = format!("{:020}", request_count);
        request_response_info.pending_requests.push(&count_key, pending_request_info);

        request_count
    }

    /// Writes a response to inspect, matching it up with an already recorded request + response
    /// pair if possible.
    fn record_response(
        &mut self,
        setting_type: SettingType,
        count: u64,
        response: Result<Option<SettingInfo>, Error>,
    ) {
        let setting_type_str = format!("{:?}", setting_type);
        let timestamp = clock::inspect_format_now();

        // Update the response counter.
        self.increment_response_count(setting_type_str.clone(), &response);

        // Find the inspect data for this setting. This should always be present as it's created
        // upon receiving a request, which should happen before the response is recorded.
        let condensed_setting_type_info = self
            .setting_request_response_info
            .map_mut()
            .get_mut(&setting_type_str)
            .expect("Missing info for request");

        let pending_requests = &mut condensed_setting_type_info.pending_requests;

        // Find the position of the pending request with the same request count and remove it. This
        // should generally be the first pending request in the queue if requests are being answered
        // in order.
        let position = match pending_requests.iter_mut().position(|info| info.count == count) {
            Some(position) => position,
            None => {
                // We may be unable to find a matching request if requests are piling up faster than
                // responses, as the number of pending requests is limited.
                return;
            }
        };
        let pending =
            pending_requests.items_mut().remove(position).expect("Failed to find pending item");

        // Find the info for this particular request type.
        let request_type_info_map = condensed_setting_type_info
            .requests_and_responses_by_type
            .get_or_insert_with(pending.request_type, || {
                ManagedInspectMap::<RequestResponsePairInfo>::default()
            });

        // Request and response pairs are keyed by the concatenation of the request and response,
        // which uniquely identifies them within a setting.
        let map_key = format!("{:?}{:?}", pending.request, response);

        // Find this request + response pair in the map and remove it, if it's present. While the
        // map key is the request + response concatenated, the key displayed in inspect is the
        // newest request count for that pair. We remove the map entry if it exists so that we can
        // re-insert to update the key displayed in inspect.
        let removed_info = request_type_info_map.map_mut().remove(&map_key);

        let response_str = format!("{:?}", response);
        let mut info = removed_info.unwrap_or_else(|| {
            RequestResponsePairInfo::new(pending.request.into_inner(), response_str, pending.count)
        });
        {
            // Update the request and response timestamps. We have borrow from the IValues with
            // as_mut and drop the variables after this scope ends so that the IValues will know to
            // update the values in inspect.
            let mut_requests = &mut info.request_timestamps.as_mut().0;
            let mut_responses = &mut info.response_timestamps.as_mut().0;

            mut_requests.push_back(pending.timestamp.into_inner());
            mut_responses.push_back(timestamp);

            // If there are too many timestamps, remove earlier ones.
            if mut_requests.len() > MAX_REQUEST_RESPONSE_TIMESTAMPS {
                let _ = mut_requests.pop_front();
            }
            if mut_responses.len() > MAX_REQUEST_RESPONSE_TIMESTAMPS {
                let _ = mut_responses.pop_front();
            }
        }

        // Insert into the map, but display the key in inspect as the request count.
        let count_key = format!("{:020}", pending.count);
        let _ = request_type_info_map.insert_with_property_name(map_key, count_key, info);

        // If there are too many entries, find and remove the oldest.
        let num_request_response_pairs = request_type_info_map.map().len();
        if num_request_response_pairs > MAX_REQUEST_RESPONSE_PAIRS {
            // Find the item with the lowest request count, which means it was the oldest request
            // received.
            let mut lowest_count: u64 = u64::MAX;
            let mut lowest_key: Option<String> = None;
            for (key, inspect_info) in request_type_info_map.map() {
                if inspect_info.request_count < lowest_count {
                    lowest_count = inspect_info.request_count;
                    lowest_key = Some(key.clone());
                }
            }

            if let Some(key_to_remove) = lowest_key {
                let _ = request_type_info_map.map_mut().remove(&key_to_remove);
            }
        }
    }

    fn increment_response_count(
        &mut self,
        setting_type_str: String,
        response: &Result<Option<SettingInfo>, Error>,
    ) {
        // Get the response count info for the setting type, creating a new info object
        // if it doesn't exist in the map yet.
        let response_count_info = self
            .response_counts
            .get_or_insert_with(setting_type_str, SettingTypeResponseCountInfo::default);

        // Get the count for the response type, creating a new count if it doesn't exist
        // in the map yet, then increment the response count
        let response_type: ResponseType = response.clone().into();
        let response_count = response_count_info
            .response_counts_by_type
            .get_or_insert_with(format!("{:?}", response_type), ResponseTypeCount::default);
        response_count.count.add(1u64);
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
    use fuchsia_inspect::testing::TreeAssertion;
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

        async fn send_request(
            &self,
            setting_type: SettingType,
            setting_request: Request,
            should_reply: bool,
        ) {
            let (messenger, _) =
                self.delegate.create(MessengerType::Unbound).await.expect("should be created");

            let (_, mut receptor) = self
                .delegate
                .create(MessengerType::Addressable(service::Address::Handler(setting_type)))
                .await
                .expect("should be created");

            let mut reply_receptor = messenger
                .message(
                    HandlerPayload::Request(setting_request).into(),
                    service::message::Audience::Address(service::Address::Handler(setting_type)),
                )
                .send();

            if let Some(message_event) = futures::StreamExt::next(&mut receptor).await {
                if let Ok((_, reply_client)) = HandlerPayload::try_from_with_client(message_event) {
                    if should_reply {
                        reply_client.reply(HandlerPayload::Response(Ok(None)).into()).send().ack();
                    }
                }
            }
            let _ = reply_receptor.next_payload().await;
        }

        async fn send_and_receive(&self, setting_type: SettingType, setting_request: Request) {
            self.send_request(setting_type, setting_request, true).await;
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

    // Verifies that request + response pairs with the same value and request type are grouped
    // together.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_inspect_grouped_responses() {
        // Set the clock so that timestamps can be controlled.
        clock::mock::set(Time::from_nanos(0));

        let inspector = inspect::Inspector::new();
        let condense_node = inspector.root().create_child(REQUEST_RESPONSE_NODE_NAME);
        let response_counts_node = inspector.root().create_child(RESPONSE_COUNTS_NODE_NAME);
        let context = create_context().await;

        let request_processor = RequestProcessor::new(context.delegate.clone());

        SettingProxyInspectAgent::create_with_node(context, condense_node, response_counts_node)
            .await;

        // Send a request to turn off auto brightness.
        let turn_off_auto_brightness = Request::SetDisplayInfo(SetDisplayInfo {
            auto_brightness: Some(false),
            ..SetDisplayInfo::default()
        });
        request_processor
            .send_and_receive(SettingType::Display, turn_off_auto_brightness.clone())
            .await;

        // Increment clock and send a request to turn on auto brightness.
        clock::mock::set(Time::from_nanos(100));
        request_processor
            .send_and_receive(
                SettingType::Display,
                Request::SetDisplayInfo(SetDisplayInfo {
                    auto_brightness: Some(true),
                    ..SetDisplayInfo::default()
                }),
            )
            .await;

        // Increment clock and send the same request as the first one. The two should be grouped
        // together.
        clock::mock::set(Time::from_nanos(200));
        request_processor.send_and_receive(SettingType::Display, turn_off_auto_brightness).await;

        assert_data_tree!(inspector, root: contains {
            requests_and_responses: {
                "Display": {
                    "pending_requests": {},
                    "requests_and_responses": {
                        "SetDisplayInfo": {
                            "00000000000000000001": {
                                "request": "SetDisplayInfo(SetDisplayInfo { \
                                    manual_brightness_value: None, \
                                    auto_brightness_value: None, \
                                    auto_brightness: Some(true), \
                                    screen_enabled: None, \
                                    low_light_mode: None, \
                                    theme: None \
                                })",
                                "request_timestamps": "0.000000100",
                                "response": "Ok(None)",
                                "response_timestamps": "0.000000100"
                            },
                            "00000000000000000002": {
                                "request": "SetDisplayInfo(SetDisplayInfo { \
                                    manual_brightness_value: None, \
                                    auto_brightness_value: None, \
                                    auto_brightness: Some(false), \
                                    screen_enabled: None, \
                                    low_light_mode: None, \
                                    theme: None \
                                })",
                                "request_timestamps": "0.000000000,0.000000200",
                                "response": "Ok(None)",
                                "response_timestamps": "0.000000000,0.000000200"
                            }
                        }
                    }
                }
            },
        });
    }

    // Test that multiple requests of different request types for the same setting records the
    // correct inspect data.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_inspect_mixed_request_types() {
        // Set the clock so that timestamps can be controlled.
        clock::mock::set(Time::from_nanos(0));

        let inspector = inspect::Inspector::new();
        let condense_node = inspector.root().create_child(REQUEST_RESPONSE_NODE_NAME);
        let response_counts_node = inspector.root().create_child(RESPONSE_COUNTS_NODE_NAME);
        let context = create_context().await;

        let request_processor = RequestProcessor::new(context.delegate.clone());

        SettingProxyInspectAgent::create_with_node(context, condense_node, response_counts_node)
            .await;

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

        // Set to a different time so that a response can correctly link to its request.
        clock::mock::set(Time::from_nanos(100));
        request_processor.send_and_receive(SettingType::Display, Request::Get).await;

        // Set to a different time so that a response can correctly link to its request.
        clock::mock::set(Time::from_nanos(200));
        request_processor
            .send_and_receive(
                SettingType::Display,
                Request::SetDisplayInfo(SetDisplayInfo {
                    auto_brightness: Some(true),
                    ..SetDisplayInfo::default()
                }),
            )
            .await;

        clock::mock::set(Time::from_nanos(300));
        request_processor.send_and_receive(SettingType::Display, Request::Get).await;

        assert_data_tree!(inspector, root: contains {
            requests_and_responses: {
                "Display": {
                    "pending_requests": {},
                    "requests_and_responses": {
                        "Get": {
                            "00000000000000000003": {
                                "request": "Get",
                                "request_timestamps": "0.000000100,0.000000300",
                                "response": "Ok(None)",
                                "response_timestamps": "0.000000100,0.000000300"
                            }
                        },
                        "SetDisplayInfo": {
                            "00000000000000000000": {
                                "request": "SetDisplayInfo(SetDisplayInfo { \
                                    manual_brightness_value: None, \
                                    auto_brightness_value: None, \
                                    auto_brightness: Some(false), \
                                    screen_enabled: None, \
                                    low_light_mode: None, \
                                    theme: None \
                                })",
                                  "request_timestamps": "0.000000000",
                                  "response": "Ok(None)",
                                  "response_timestamps": "0.000000000"
                            },
                            "00000000000000000002": {
                                "request": "SetDisplayInfo(SetDisplayInfo { \
                                    manual_brightness_value: None, \
                                    auto_brightness_value: None, \
                                    auto_brightness: Some(true), \
                                    screen_enabled: None, \
                                    low_light_mode: None, \
                                    theme: None \
                                })",
                                "request_timestamps": "0.000000200",
                                "response": "Ok(None)",
                                "response_timestamps": "0.000000200"
                            }
                        }
                    }
                }
            },
            response_counts: {
                "Display": {
                    "OkNone": {
                        count: 4u64,
                    }
                },
            },
        });
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_pending_request() {
        // Set the clock so that timestamps can be controlled.
        clock::mock::set(Time::from_nanos(0));

        let inspector = inspect::Inspector::new();
        let condense_node = inspector.root().create_child(REQUEST_RESPONSE_NODE_NAME);
        let request_counts_node = inspector.root().create_child(RESPONSE_COUNTS_NODE_NAME);
        let context = create_context().await;

        let request_processor = RequestProcessor::new(context.delegate.clone());

        SettingProxyInspectAgent::create_with_node(context, condense_node, request_counts_node)
            .await;

        request_processor
            .send_request(
                SettingType::Display,
                Request::SetDisplayInfo(SetDisplayInfo {
                    auto_brightness: Some(false),
                    ..SetDisplayInfo::default()
                }),
                false,
            )
            .await;

        assert_data_tree!(inspector, root: contains {
            requests_and_responses: {
                "Display": {
                    "pending_requests": {
                        "00000000000000000000": {
                            "request": "SetDisplayInfo(SetDisplayInfo { \
                                manual_brightness_value: None, \
                                auto_brightness_value: None, \
                                auto_brightness: Some(false), \
                                screen_enabled: None, \
                                low_light_mode: None, \
                                theme: None \
                            })",
                            "timestamp": "0.000000000",
                        }
                    },
                    "requests_and_responses": {}
                }
            },
        });
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_response_counts_inspect() {
        // Set the clock so that timestamps can be controlled.
        clock::mock::set(Time::from_nanos(0));

        let inspector = inspect::Inspector::new();
        let condense_node = inspector.root().create_child(REQUEST_RESPONSE_NODE_NAME);
        let request_counts_node = inspector.root().create_child(RESPONSE_COUNTS_NODE_NAME);
        let context = create_context().await;

        let request_processor = RequestProcessor::new(context.delegate.clone());

        SettingProxyInspectAgent::create_with_node(context, condense_node, request_counts_node)
            .await;

        request_processor
            .send_and_receive(
                SettingType::Display,
                Request::SetDisplayInfo(SetDisplayInfo {
                    auto_brightness: Some(false),
                    ..SetDisplayInfo::default()
                }),
            )
            .await;

        clock::mock::set(Time::from_nanos(100));
        request_processor.send_and_receive(SettingType::Display, Request::Get).await;

        clock::mock::set(Time::from_nanos(200));
        request_processor
            .send_and_receive(
                SettingType::Display,
                Request::SetDisplayInfo(SetDisplayInfo {
                    auto_brightness: None,
                    ..SetDisplayInfo::default()
                }),
            )
            .await;

        clock::mock::set(Time::from_nanos(300));
        request_processor.send_and_receive(SettingType::Display, Request::Get).await;

        assert_data_tree!(inspector, root: contains {
            response_counts: {
                "Display": {
                    "OkNone": {
                        count: 4u64,
                    },
                },
            },
        });
    }

    // Verifies that old requests are dropped after MAX_REQUEST_RESPONSE_PAIRS are received for a
    // given request + response pair.
    #[fuchsia_async::run_until_stalled(test)]
    async fn inspect_queue_test() {
        // Set the clock so that timestamps will always be 0.
        clock::mock::set(Time::from_nanos(0));
        let inspector = inspect::Inspector::new();
        let condense_node = inspector.root().create_child(REQUEST_RESPONSE_NODE_NAME);
        let response_counts_node = inspector.root().create_child(RESPONSE_COUNTS_NODE_NAME);
        let context = create_context().await;
        let request_processor = RequestProcessor::new(context.delegate.clone());

        SettingProxyInspectAgent::create_with_node(context, condense_node, response_counts_node)
            .await;

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

        // Send one more than the max requests to make sure they get pushed off the end of the
        // queue. The requests must have different values to avoid getting grouped together.
        for i in 0..MAX_REQUEST_RESPONSE_PAIRS + 1 {
            request_processor
                .send_and_receive(
                    SettingType::Display,
                    Request::SetDisplayInfo(SetDisplayInfo {
                        manual_brightness_value: Some((i as f32) / 100f32),
                        ..SetDisplayInfo::default()
                    }),
                )
                .await;
        }

        // Ensures we have INSPECT_REQUESTS_COUNT items and that the queue dropped the earliest one
        // when hitting the limit.
        fn display_subtree_assertion() -> TreeAssertion {
            let mut tree_assertion = TreeAssertion::new("Display", false);
            let mut request_response_assertion = TreeAssertion::new("requests_and_responses", true);
            let mut request_assertion = TreeAssertion::new("SetDisplayInfo", true);

            for i in 1..MAX_REQUEST_RESPONSE_PAIRS + 1 {
                // We don't need to set clock here since we don't do exact match.
                request_assertion
                    .add_child_assertion(TreeAssertion::new(&format!("{:020}", i), false));
            }
            request_response_assertion.add_child_assertion(request_assertion);
            tree_assertion.add_child_assertion(request_response_assertion);
            tree_assertion
        }

        assert_data_tree!(inspector, root: contains {
            requests_and_responses: {
                display_subtree_assertion(),
                "Intl": {
                    "pending_requests": {},
                    "requests_and_responses": {
                        "SetIntlInfo": {
                            "00000000000000000000": {
                                "request": "SetIntlInfo(IntlInfo { \
                                    locales: Some([LocaleId { id: \"en-US\" }]), \
                                    temperature_unit: Some(Celsius), \
                                    time_zone_id: Some(\"UTC\"), \
                                    hour_cycle: None \
                                })",
                                "request_timestamps": "0.000000000",
                                "response": "Ok(None)",
                                "response_timestamps": "0.000000000"
                            }
                        }
                    }
                }
            },
        });
    }
}
