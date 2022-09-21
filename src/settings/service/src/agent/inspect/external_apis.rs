// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The external_apis mod defines the [ExternalApisInspectAgent], which is responsible for recording
//! external API requests and responses to Inspect. Since API usages might happen before agent
//! lifecycle states are communicated (due to agent priority ordering), the
//! [ExternalApisInspectAgent] begins listening to requests immediately after creation.
//!
//! Example Inspect structure:
//!
//! ```text
//! {
//!   "fuchsia.external.FakeAPI": {
//!     "pending_calls": {
//!       "00000000000000000005": {
//!         request: "set_manual_brightness(0.7)",
//!         response: "None",
//!         request_timestamp: "19.002716",
//!         response_timestamp: "None",
//!       },
//!     },
//!     "calls": {
//!       "00000000000000000002": {
//!         request: "set_manual_brightness(0.6)",
//!         response: "Ok(None)",
//!         request_timestamp: "18.293864",
//!         response_timestamp: "18.466811",
//!       },
//!       "00000000000000000004": {
//!         request: "set_manual_brightness(0.8)",
//!         response: "Ok(None)",
//!         request_timestamp: "18.788366",
//!         response_timestamp: "18.915355",
//!       },
//!     },
//!   },
//!   ...
//! }
//! ```

use crate::agent::{Context, Payload};
use crate::blueprint_definition;
use crate::event::{Event, Payload as EventPayload};
use crate::message::base::{filter, MessageEvent, MessengerType};
use crate::service::{self as service, TryFromWithClient};
use crate::service_context::ExternalServiceEvent;
use crate::trace;

use fuchsia_async as fasync;
use fuchsia_inspect::{component, Node};
use fuchsia_inspect_derive::{IValue, Inspect, WithInspect};
use futures::StreamExt;
use settings_inspect_utils::managed_inspect_map::ManagedInspectMap;
use settings_inspect_utils::managed_inspect_queue::ManagedInspectQueue;
use std::sync::Arc;

blueprint_definition!(
    "external_apis",
    crate::agent::inspect::external_apis::ExternalApiInspectAgent::create
);

/// The key for the queue for completed calls per protocol.
const COMPLETED_CALLS_KEY: &str = "completed_calls";

/// The key for the queue for pending calls per protocol.
const PENDING_CALLS_KEY: &str = "pending_calls";

/// The maximum number of recently completed calls that will be kept in
/// inspect per protocol.
const MAX_COMPLETED_CALLS: usize = 10;

/// The maximum number of still pending calls that will be kept in
/// inspect per protocol.
const MAX_PENDING_CALLS: usize = 10;

// TODO(fxb/108679): Explore reducing size of keys in inspect.
#[derive(Debug, Default, Inspect)]
struct ExternalApiCallInfo {
    /// Node of this info.
    inspect_node: Node,

    /// The request sent via the external API.
    request: IValue<String>,

    /// The response received by the external API.
    response: IValue<String>,

    /// The timestamp at which the request was sent.
    request_timestamp: IValue<String>,

    /// The timestamp at which the response was received.
    response_timestamp: IValue<String>,
}

impl ExternalApiCallInfo {
    fn new(
        request: &str,
        response: &str,
        request_timestamp: &str,
        response_timestamp: &str,
    ) -> Self {
        let mut info = Self::default();
        info.request.iset(request.to_string());
        info.response.iset(response.to_string());
        info.request_timestamp.iset(request_timestamp.to_string());
        info.response_timestamp.iset(response_timestamp.to_string());
        info
    }
}

#[derive(Default, Inspect)]
struct ExternalApiCallsWrapper {
    inspect_node: Node,
    /// The number of total calls that have been made on this protocol.
    count: IValue<u64>,
    /// The most recent pending and completed calls per-protocol.
    calls: ManagedInspectMap<ManagedInspectQueue<ExternalApiCallInfo>>,
    /// The external api event counts.
    event_counts: ManagedInspectMap<IValue<u64>>,
}

/// The [SettingTypeUsageInspectAgent] is responsible for listening to requests to external
/// APIs and recording their requests and responses to Inspect.
pub(crate) struct ExternalApiInspectAgent {
    /// Map from the API call type to its most recent calls.
    ///
    /// Example structure:
    /// ```text
    /// {
    ///   "fuchsia.ui.brightness.Control": {
    ///     "count": 6,
    ///     "calls": {
    ///       "pending_calls": {
    ///         "00000000000000000006": {
    ///           request: "set_manual_brightness(0.7)",
    ///           response: "None",
    ///           request_timestamp: "19.002716",
    ///           response_timestamp: "None",
    ///         },
    ///       }],
    ///       "completed_calls": [{
    ///         "00000000000000000003": {
    ///           request: "set_manual_brightness(0.6)",
    ///           response: "Ok(None)",
    ///           request_timestamp: "18.293864",
    ///           response_timestamp: "18.466811",
    ///         },
    ///         "00000000000000000005": {
    ///           request: "set_manual_brightness(0.8)",
    ///           response: "Ok(None)",
    ///           request_timestamp: "18.788366",
    ///           response_timestamp: "18.915355",
    ///         },
    ///       },
    ///     },
    ///     "event_counts": {
    ///       "Connect": 1,
    ///       "ApiCall": 3,
    ///       "ApiResponse": 2,
    ///     }
    ///   },
    ///   ...
    /// }
    /// ```
    api_calls: ManagedInspectMap<ExternalApiCallsWrapper>,
}

impl ExternalApiInspectAgent {
    /// Creates the `ExternalApiInspectAgent` with the given `context`.
    async fn create(context: Context) {
        Self::create_with_node(
            context,
            component::inspector().root().create_child("external_apis"),
        )
        .await;
    }

    /// Creates the `ExternalApiInspectAgent` with the given `context` and Inspect `node`.
    async fn create_with_node(context: Context, node: Node) {
        let (_, message_rx) = context
            .delegate
            .create(MessengerType::Broker(Some(filter::Builder::single(
                filter::Condition::Custom(Arc::new(move |message| {
                    // TODO(fxb/108370): Explore combining inspect agents.
                    // Only catch external api requests.
                    matches!(
                        message.payload(),
                        service::Payload::Event(EventPayload::Event(Event::ExternalServiceEvent(
                            _
                        )))
                    )
                })),
            ))))
            .await
            .expect("should receive client");

        let mut agent = ExternalApiInspectAgent {
            api_calls: ManagedInspectMap::<ExternalApiCallsWrapper>::with_node(node),
        };

        fasync::Task::spawn({
            async move {
                let _ = &context;
                let id = fuchsia_trace::Id::new();
                trace!(id, "external_api_inspect_agent");
                let event = message_rx.fuse();
                let agent_event = context.receptor.fuse();
                futures::pin_mut!(agent_event, event);

                let mut message_event_fut = event.select_next_some();
                let mut agent_message_fut = agent_event.select_next_some();
                loop {
                    futures::select! {
                        message_event = message_event_fut => {
                            trace!(
                                id,
                                "message_event"
                            );
                            agent.process_message_event(message_event);
                            message_event_fut = event.select_next_some();
                        },
                        agent_message = agent_message_fut => {
                            trace!(
                                id,
                                "agent_event"
                            );
                            if let MessageEvent::Message(
                                    service::Payload::Agent(Payload::Invocation(_)), client)
                                    = agent_message {
                                // Since the agent runs at creation, there is no
                                // need to handle state here.
                                client.reply(Payload::Complete(Ok(())).into()).send().ack();
                            }
                            agent_message_fut = agent_event.select_next_some();
                        },
                    }
                }
            }
        })
        .detach();
    }

    /// Processes the given `event` and writes it to Inspect if it is an
    /// `ExternalServiceEvent`.
    fn process_message_event(&mut self, event: service::message::MessageEvent) {
        if let Ok((EventPayload::Event(Event::ExternalServiceEvent(external_service_event)), _)) =
            EventPayload::try_from_with_client(event)
        {
            match external_service_event {
                ExternalServiceEvent::Created(protocol, timestamp) => {
                    let count = self.get_count(protocol) + 1;
                    let info = ExternalApiCallInfo::new("connect", "none", "none", &timestamp);
                    self.add_info(protocol, COMPLETED_CALLS_KEY, "Created", info, count);
                }
                ExternalServiceEvent::ApiCall(protocol, request, timestamp) => {
                    let count = self.get_count(protocol) + 1;
                    let info = ExternalApiCallInfo::new(&request, "none", &timestamp, "none");
                    self.add_info(protocol, PENDING_CALLS_KEY, "ApiCall", info, count);
                }
                ExternalServiceEvent::ApiResponse(
                    protocol,
                    response,
                    request,
                    request_timestamp,
                    response_timestamp,
                ) => {
                    let count = self.get_count(protocol) + 1;
                    let info = ExternalApiCallInfo::new(
                        &request,
                        &response,
                        &request_timestamp,
                        &response_timestamp,
                    );
                    self.remove_pending(protocol, &info);
                    self.add_info(protocol, COMPLETED_CALLS_KEY, "ApiResponse", info, count);
                }
                ExternalServiceEvent::ApiError(
                    protocol,
                    error,
                    request,
                    request_timestamp,
                    error_timestamp,
                ) => {
                    let count = self.get_count(protocol) + 1;
                    let info = ExternalApiCallInfo::new(
                        &request,
                        &error,
                        &request_timestamp,
                        &error_timestamp,
                    );
                    self.remove_pending(protocol, &info);
                    self.add_info(protocol, COMPLETED_CALLS_KEY, "ApiError", info, count);
                }
                ExternalServiceEvent::Closed(
                    protocol,
                    request,
                    request_timestamp,
                    response_timestamp,
                ) => {
                    let count = self.get_count(protocol) + 1;
                    let info = ExternalApiCallInfo::new(
                        &request,
                        "closed",
                        &request_timestamp,
                        &response_timestamp,
                    );
                    self.remove_pending(protocol, &info);
                    self.add_info(protocol, COMPLETED_CALLS_KEY, "Closed", info, count);
                }
            }
        }
    }

    /// Retrieves the total call count for the given `protocol`. Implicitly
    /// calls `ensure_protocol_exists`.
    fn get_count(&mut self, protocol: &str) -> u64 {
        self.ensure_protocol_exists(protocol);
        *self.api_calls.get(protocol).expect("Wrapper should exist").count
    }

    /// Ensures that an entry exists for the given `protocol`, adding a new one if
    /// it does not yet exist.
    fn ensure_protocol_exists(&mut self, protocol: &str) {
        let _ = self
            .api_calls
            .get_or_insert_with(protocol.to_string(), ExternalApiCallsWrapper::default);
    }

    /// Ensures that an entry exists for the given `protocol`, and `queue_key` adding a
    /// new queue of max size `queue_size` if one does not yet exist. Implicitly calls
    /// `ensure_protocol_exists`.
    fn ensure_queue_exists(&mut self, protocol: &str, queue_key: &'static str, queue_size: usize) {
        self.ensure_protocol_exists(protocol);

        let protocol_map = self.api_calls.get_mut(protocol).expect("Protocol entry should exist");
        let _ = protocol_map.calls.get_or_insert_with(queue_key.to_string(), || {
            ManagedInspectQueue::<ExternalApiCallInfo>::new(queue_size)
        });
    }

    /// Inserts the given `info` into the entry at `protocol` and `queue_key`, incrementing
    /// the total call count to the protocol's wrapper entry.
    fn add_info(
        &mut self,
        protocol: &str,
        queue_key: &'static str,
        event_type: &str,
        info: ExternalApiCallInfo,
        count: u64,
    ) {
        self.ensure_queue_exists(
            protocol,
            queue_key,
            if queue_key == COMPLETED_CALLS_KEY { MAX_COMPLETED_CALLS } else { MAX_PENDING_CALLS },
        );
        let wrapper = self.api_calls.get_mut(protocol).expect("Protocol entry should exist");
        {
            let mut wrapper_guard = wrapper.count.as_mut();
            *wrapper_guard += 1;
        }
        let event_count =
            wrapper.event_counts.get_or_insert_with(event_type.to_string(), || IValue::new(0));
        {
            let mut event_count_guard = event_count.as_mut();
            *event_count_guard += 1;
        }

        let queue = wrapper.calls.get_mut(queue_key).expect("Queue should exist");
        let key = format!("{count:020}");
        queue.push(
            &key,
            info.with_inspect(queue.inspect_node(), &key)
                .expect("Failed to create ExternalApiCallInfo node"),
        );
    }

    /// Removes the call with the same request timestamp from the `protocol`'s pending
    /// call queue, indicating that the call has completed. Should be called along with
    /// `add_info` to add the completed call.
    fn remove_pending(&mut self, protocol: &str, info: &ExternalApiCallInfo) {
        let wrapper = self.api_calls.get_mut(protocol).expect("Protocol entry should exist");
        let pending_queue =
            wrapper.calls.get_mut(PENDING_CALLS_KEY).expect("Pending queue should exist");
        let req_timestamp = &*info.request_timestamp;
        pending_queue.retain(|pending| &*pending.request_timestamp != req_timestamp);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::event::Event;
    use crate::message::base::Audience;
    use crate::message::MessageHubUtil;
    use crate::service;

    use fuchsia_inspect::{assert_data_tree, Inspector};
    use std::collections::HashSet;

    const MOCK_PROTOCOL_NAME: &str = "fuchsia.external.FakeAPI";

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

        async fn send_and_receive(&self, external_api_event: ExternalServiceEvent) {
            let (messenger, _) =
                self.delegate.create(MessengerType::Unbound).await.expect("should be created");

            let (_, mut receptor) =
                self.delegate.create(MessengerType::Unbound).await.expect("should be created");

            let _ = messenger
                .message(
                    service::Payload::Event(EventPayload::Event(Event::ExternalServiceEvent(
                        external_api_event,
                    ))),
                    Audience::Broadcast,
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
    async fn test_inspect_create_connection() {
        let inspector = Inspector::new();
        let inspect_node = inspector.root().create_child("external_apis");
        let context = create_context().await;

        let request_processor = RequestProcessor::new(context.delegate.clone());

        ExternalApiInspectAgent::create_with_node(context, inspect_node).await;

        let connection_created_event =
            ExternalServiceEvent::Created(MOCK_PROTOCOL_NAME, "0.000000".into());

        request_processor.send_and_receive(connection_created_event.clone()).await;

        assert_data_tree!(inspector, root: {
            external_apis: {
                "fuchsia.external.FakeAPI": {
                    "calls": {
                        "completed_calls": {
                            "00000000000000000001": {
                                request: "connect",
                                response: "none",
                                request_timestamp: "none",
                                response_timestamp: "0.000000",
                            },
                        },
                    },
                    "count": 1u64,
                    "event_counts": {
                        "Created": 1u64,
                    },
                },
            },
        });
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_inspect_pending() {
        let inspector = Inspector::new();
        let inspect_node = inspector.root().create_child("external_apis");
        let context = create_context().await;

        let request_processor = RequestProcessor::new(context.delegate.clone());

        ExternalApiInspectAgent::create_with_node(context, inspect_node).await;

        let api_call_event = ExternalServiceEvent::ApiCall(
            MOCK_PROTOCOL_NAME,
            "set_manual_brightness(0.6)".into(),
            "0.000000".into(),
        );

        request_processor.send_and_receive(api_call_event.clone()).await;

        assert_data_tree!(inspector, root: {
            external_apis: {
                "fuchsia.external.FakeAPI": {
                    "calls": {
                        "pending_calls": {
                            "00000000000000000001": {
                                request: "set_manual_brightness(0.6)",
                                response: "none",
                                request_timestamp: "0.000000",
                                response_timestamp: "none",
                            },
                        },
                    },
                    "count": 1u64,
                    "event_counts": {
                        "ApiCall": 1u64,
                    },
                },
            },
        });
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_inspect_success_response() {
        let inspector = Inspector::new();
        let inspect_node = inspector.root().create_child("external_apis");
        let context = create_context().await;

        let request_processor = RequestProcessor::new(context.delegate.clone());

        ExternalApiInspectAgent::create_with_node(context, inspect_node).await;

        let api_call_event = ExternalServiceEvent::ApiCall(
            MOCK_PROTOCOL_NAME,
            "set_manual_brightness(0.6)".into(),
            "0.000000".into(),
        );
        let api_response_event = ExternalServiceEvent::ApiResponse(
            MOCK_PROTOCOL_NAME,
            "Ok(None)".into(),
            "set_manual_brightness(0.6)".into(),
            "0.000000".into(),
            "0.129987".into(),
        );

        request_processor.send_and_receive(api_call_event.clone()).await;
        assert_data_tree!(inspector, root: {
            external_apis: {
                "fuchsia.external.FakeAPI": {
                    "calls": {
                        "pending_calls": {
                            "00000000000000000001": {
                                request: "set_manual_brightness(0.6)",
                                response: "none",
                                request_timestamp: "0.000000",
                                response_timestamp: "none",
                            },
                        },
                    },
                    "count": 1u64,
                    "event_counts": {
                        "ApiCall": 1u64,
                    },
                },
            },
        });

        request_processor.send_and_receive(api_response_event.clone()).await;

        assert_data_tree!(inspector, root: {
            external_apis: {
                "fuchsia.external.FakeAPI": {
                    "calls": {
                        "pending_calls": {},
                        "completed_calls": {
                            "00000000000000000002": {
                                request: "set_manual_brightness(0.6)",
                                response: "Ok(None)",
                                request_timestamp: "0.000000",
                                response_timestamp: "0.129987",
                            },
                        },
                    },
                    "count": 2u64,
                    "event_counts": {
                        "ApiCall": 1u64,
                        "ApiResponse": 1u64,
                    },
                },
            },
        });
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_inspect_error() {
        let inspector = Inspector::new();
        let inspect_node = inspector.root().create_child("external_apis");
        let context = create_context().await;

        let request_processor = RequestProcessor::new(context.delegate.clone());

        ExternalApiInspectAgent::create_with_node(context, inspect_node).await;

        let api_call_event = ExternalServiceEvent::ApiCall(
            MOCK_PROTOCOL_NAME,
            "set_manual_brightness(0.6)".into(),
            "0.000000".into(),
        );
        let error_event = ExternalServiceEvent::ApiError(
            MOCK_PROTOCOL_NAME,
            "Err(INTERNAL_ERROR)".into(),
            "set_manual_brightness(0.6)".into(),
            "0.000000".into(),
            "0.129987".into(),
        );

        request_processor.send_and_receive(api_call_event.clone()).await;

        assert_data_tree!(inspector, root: {
            external_apis: {
                "fuchsia.external.FakeAPI": {
                    "calls": {
                        "pending_calls": {
                            "00000000000000000001": {
                                request: "set_manual_brightness(0.6)",
                                response: "none",
                                request_timestamp: "0.000000",
                                response_timestamp: "none",
                            },
                        },
                    },
                    "count": 1u64,
                    "event_counts": {
                        "ApiCall": 1u64,
                    },
                },
            },
        });

        request_processor.send_and_receive(error_event.clone()).await;

        assert_data_tree!(inspector, root: {
            external_apis: {
                "fuchsia.external.FakeAPI": {
                    "calls": {
                        "pending_calls": {},
                        "completed_calls": {
                            "00000000000000000002": {
                                request: "set_manual_brightness(0.6)",
                                response: "Err(INTERNAL_ERROR)",
                                request_timestamp: "0.000000",
                                response_timestamp: "0.129987",
                            },
                        },
                    },
                    "count": 2u64,
                    "event_counts": {
                        "ApiCall": 1u64,
                        "ApiError": 1u64,
                    },
                },
            },
        });
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_inspect_channel_closed() {
        let inspector = Inspector::new();
        let inspect_node = inspector.root().create_child("external_apis");
        let context = create_context().await;

        let request_processor = RequestProcessor::new(context.delegate.clone());

        ExternalApiInspectAgent::create_with_node(context, inspect_node).await;

        let api_call_event = ExternalServiceEvent::ApiCall(
            MOCK_PROTOCOL_NAME,
            "set_manual_brightness(0.6)".into(),
            "0.000000".into(),
        );
        let closed_event = ExternalServiceEvent::Closed(
            MOCK_PROTOCOL_NAME,
            "set_manual_brightness(0.6)".into(),
            "0.000000".into(),
            "0.129987".into(),
        );

        request_processor.send_and_receive(api_call_event.clone()).await;

        assert_data_tree!(inspector, root: {
            external_apis: {
                "fuchsia.external.FakeAPI": {
                    "calls": {
                        "pending_calls": {
                            "00000000000000000001": {
                                request: "set_manual_brightness(0.6)",
                                response: "none",
                                request_timestamp: "0.000000",
                                response_timestamp: "none",
                            },
                        },
                    },
                    "count": 1u64,
                    "event_counts": {
                        "ApiCall": 1u64,
                    },
                },
            },
        });

        request_processor.send_and_receive(closed_event.clone()).await;

        assert_data_tree!(inspector, root: {
            external_apis: {
                "fuchsia.external.FakeAPI": {
                    "calls": {
                        "pending_calls": {},
                        "completed_calls": {
                            "00000000000000000002": {
                                request: "set_manual_brightness(0.6)",
                                response: "closed",
                                request_timestamp: "0.000000",
                                response_timestamp: "0.129987",
                            },
                        },
                    },
                    "count": 2u64,
                    "event_counts": {
                        "ApiCall": 1u64,
                        "Closed": 1u64,
                    },
                },
            },
        });
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_inspect_multiple_requests() {
        let inspector = Inspector::new();
        let inspect_node = inspector.root().create_child("external_apis");
        let context = create_context().await;

        let request_processor = RequestProcessor::new(context.delegate.clone());

        ExternalApiInspectAgent::create_with_node(context, inspect_node).await;

        let api_call_event = ExternalServiceEvent::ApiCall(
            MOCK_PROTOCOL_NAME,
            "set_manual_brightness(0.6)".into(),
            "0.000000".into(),
        );
        let api_response_event = ExternalServiceEvent::ApiResponse(
            MOCK_PROTOCOL_NAME,
            "Ok(None)".into(),
            "set_manual_brightness(0.6)".into(),
            "0.000000".into(),
            "0.129987".into(),
        );

        let api_call_event_2 = ExternalServiceEvent::ApiCall(
            MOCK_PROTOCOL_NAME,
            "set_manual_brightness(0.7)".into(),
            "0.139816".into(),
        );
        let api_response_event_2 = ExternalServiceEvent::ApiResponse(
            MOCK_PROTOCOL_NAME,
            "Ok(None)".into(),
            "set_manual_brightness(0.7)".into(),
            "0.139816".into(),
            "0.141235".into(),
        );

        request_processor.send_and_receive(api_call_event.clone()).await;
        assert_data_tree!(inspector, root: {
            external_apis: {
                "fuchsia.external.FakeAPI": {
                    "calls": {
                        "pending_calls": {
                            "00000000000000000001": {
                                request: "set_manual_brightness(0.6)",
                                response: "none",
                                request_timestamp: "0.000000",
                                response_timestamp: "none",
                            },
                        },
                    },
                    "count": 1u64,
                    "event_counts": {
                        "ApiCall": 1u64,
                    },
                },
            },
        });

        request_processor.send_and_receive(api_response_event.clone()).await;

        assert_data_tree!(inspector, root: {
            external_apis: {
                "fuchsia.external.FakeAPI": {
                    "calls": {
                        "pending_calls": {},
                        "completed_calls": {
                            "00000000000000000002": {
                                request: "set_manual_brightness(0.6)",
                                response: "Ok(None)",
                                request_timestamp: "0.000000",
                                response_timestamp: "0.129987",
                            },
                        },
                    },
                    "count": 2u64,
                    "event_counts": {
                        "ApiCall": 1u64,
                        "ApiResponse": 1u64,
                    },
                },
            },
        });

        request_processor.send_and_receive(api_call_event_2.clone()).await;
        assert_data_tree!(inspector, root: {
            external_apis: {
                "fuchsia.external.FakeAPI": {
                    "calls": {
                        "pending_calls": {
                            "00000000000000000003": {
                                request: "set_manual_brightness(0.7)",
                                response: "none",
                                request_timestamp: "0.139816",
                                response_timestamp: "none",
                            },
                        },
                        "completed_calls": {
                            "00000000000000000002": {
                                request: "set_manual_brightness(0.6)",
                                response: "Ok(None)",
                                request_timestamp: "0.000000",
                                response_timestamp: "0.129987",
                            },
                        },
                    },
                    "count": 3u64,
                    "event_counts": {
                        "ApiCall": 2u64,
                        "ApiResponse": 1u64,
                    },
                },
            },
        });

        request_processor.send_and_receive(api_response_event_2.clone()).await;

        assert_data_tree!(inspector, root: {
            external_apis: {
                "fuchsia.external.FakeAPI": {
                    "calls": {
                        "pending_calls": {},
                        "completed_calls": {
                            "00000000000000000002": {
                                request: "set_manual_brightness(0.6)",
                                response: "Ok(None)",
                                request_timestamp: "0.000000",
                                response_timestamp: "0.129987",
                            },
                            "00000000000000000004": {
                                request: "set_manual_brightness(0.7)",
                                response: "Ok(None)",
                                request_timestamp: "0.139816",
                                response_timestamp: "0.141235",
                            },
                        },
                    },
                    "count": 4u64,
                    "event_counts": {
                        "ApiCall": 2u64,
                        "ApiResponse": 2u64,
                    },
                },
            },
        });
    }
}
