// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::convert::TryFrom;
use std::sync::Arc;

use anyhow::{format_err, Error};
use fuchsia_async as fasync;
use fuchsia_inspect::{self as inspect, component, Property};
use fuchsia_syslog::fx_log_err;
use futures::StreamExt;

use crate::agent::{Context, Payload};
use crate::base::SettingInfo;
use crate::clock;
use crate::handler::base::Request;
use crate::handler::device_storage::DeviceStorageAccess;
use crate::handler::setting_handler::{Command, Event, Payload as HandlerPayload};
use crate::message::base::{filter, Audience, MessageEvent, MessengerType};
use crate::service;
use crate::service::message::{MessageClient, Messenger, Signature};
use crate::service::TryFromWithClient;

const INSPECT_NODE_NAME: &str = "setting_values";

/// An agent that listens in on messages between the proxy and setting handlers to record the
/// values of all settings to inspect.
pub struct InspectSettingAgent {
    messenger_client: Messenger,
    inspect_node: inspect::Node,
    setting_values: HashMap<&'static str, InspectSettingInfo>,
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
    pub async fn create(context: Context) {
        Self::create_with_node(
            context,
            component::inspector().root().create_child(INSPECT_NODE_NAME),
        )
        .await;
    }

    /// Create an agent to listen in on all messages between Proxy and setting
    /// handlers. Agent starts immediately without calling invocation, but
    /// acknowledges the invocation payload to let the Authority know the agent
    /// starts properly.
    pub async fn create_with_node(context: Context, inspect_node: inspect::Node) {
        // TODO(fxb/71826): log and exit instead of panicking
        let (messenger_client, receptor) = context
            .messenger_factory
            .create(MessengerType::Broker(Some(filter::Builder::single(
                filter::Condition::Custom(Arc::new(move |message| {
                    HandlerPayload::try_from(message.payload()).map_or(false, |payload| {
                        // Only catch messages that were originally sent from the interfaces, and
                        // that contain a request for the specific setting type we're interested in.
                        matches!(
                            payload,
                            HandlerPayload::Command(Command::HandleRequest(_))
                                | HandlerPayload::Event(Event::Changed(_))
                        )
                    })
                })),
            ))))
            .await
            .expect("could not create inspect");

        let mut agent = Self { messenger_client, inspect_node, setting_values: HashMap::new() };

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

    /// Identifies [`service::message::MessageEvent`] that contains a
    /// [`Restore`] message for setting handlers and records the setting values.
    /// Also, identifies a changing event from a setting handler to the proxy
    /// and records the setting values.
    async fn process_message_event(&mut self, event: service::message::MessageEvent) {
        if let Ok((payload, client)) = HandlerPayload::try_from_with_client(event) {
            match payload {
                // When we see a Restore message, we know a given setting is starting up, so
                // we watch the reply to get the signature of the setting handler and ask
                // for its value, so that we have the value of all settings immediately on
                // start.
                HandlerPayload::Command(Command::HandleRequest(Request::Restore)) => {
                    match InspectSettingAgent::watch_reply(client).await {
                        Ok(reply_signature) => {
                            self.request_and_write_to_inspect(reply_signature).await.ok();
                        }
                        Err(err) => {
                            fx_log_err!("Failed to watch reply to restore: {:?}", err)
                        }
                    }
                }
                // Whenever we see a setting handler tell the proxy it changed, we ask it
                // for its value again.
                // TODO(fxb/66294): Capture new value directly here.
                HandlerPayload::Event(Event::Changed(_)) => {
                    self.request_and_write_to_inspect(client.get_author())
                        .await
                        .map_err(|err| fx_log_err!("Failed to request value on change: {:?}", err))
                        .ok();
                }
                _ => {}
            }
        }
    }

    /// Watches for the reply to a sent message and return the author of the reply.
    async fn watch_reply(mut client: MessageClient) -> Result<Signature, Error> {
        let mut reply_receptor = client.spawn_observer();

        reply_receptor.next_payload().await.map(|(_, reply_client)| reply_client.get_author())
    }

    /// Requests the setting value from a given signature for a setting handler and records the
    /// result in inspect.
    async fn request_and_write_to_inspect(&mut self, signature: Signature) -> Result<(), Error> {
        let value = self.request_value(signature).await?;
        self.write_setting_to_inspect(value).await;
        Ok(())
    }

    /// Requests the setting value from a given signature for a setting handler.
    async fn request_value(&mut self, signature: Signature) -> Result<SettingInfo, Error> {
        let mut send_receptor = self
            .messenger_client
            .message(
                HandlerPayload::Command(Command::HandleRequest(Request::Get)).into(),
                Audience::Messenger(signature),
            )
            .send();

        send_receptor.next_payload().await.and_then(|payload| {
            if let Ok(HandlerPayload::Result(Ok(Some(setting)))) =
                HandlerPayload::try_from(payload.0)
            {
                Ok(setting)
            } else {
                // TODO(fxbug.dev/68479): Propagate the returned error or
                // generate proper error.
                Err(format_err!("did not receive setting value"))
            }
        })
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
                let timestamp_prop = node.create_string("timestamp", timestamp.clone());
                self.setting_values.insert(
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
    use fuchsia_inspect::assert_inspect_tree;
    use fuchsia_zircon::Time;
    use std::collections::HashSet;

    use crate::base::UnknownInfo;
    use crate::service::message::{create_hub, Receptor};

    use super::*;

    /// Verifies the next payload on the given receptor matches the given payload.
    ///
    /// Returns the message client of the received payload for convenience.
    async fn verify_payload(mut receptor: Receptor, expected: HandlerPayload) -> MessageClient {
        let result = receptor.next_payload().await;
        assert!(result.is_ok());
        let (received, message_client) = result.unwrap();
        assert_eq!(
            HandlerPayload::try_from(received).expect("payload should be extracted"),
            expected
        );
        return message_client;
    }

    async fn create_context() -> Context {
        Context::new(
            create_hub().create(MessengerType::Unbound).await.expect("should be present").1,
            create_hub(),
            HashSet::new(),
            None,
        )
        .await
    }

    /// Verifies that inspect agent intercepts a restore request and writes the setting value to
    /// inspect.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_write_inspect_on_restore() {
        // Set the clock so that timestamps will always be 0.
        clock::mock::set(Time::from_nanos(0));

        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child(INSPECT_NODE_NAME);
        let context = create_context().await;

        let (proxy, _) = context.messenger_factory.create(MessengerType::Unbound).await.unwrap();

        let (_, mut setting_handler_receptor) =
            context.messenger_factory.create(MessengerType::Unbound).await.unwrap();
        let setting_handler_signature = setting_handler_receptor.get_signature();

        InspectSettingAgent::create_with_node(context, inspect_node).await;

        // Proxy sends restore request.
        proxy
            .message(
                HandlerPayload::Command(Command::HandleRequest(Request::Restore)).into(),
                Audience::Messenger(setting_handler_signature),
            )
            .send();

        // Setting handler acks the restore.
        let (_, reply_client) = setting_handler_receptor.next_payload().await.unwrap();
        reply_client.reply(HandlerPayload::Result(Ok(None)).into()).send().ack();

        // Inspect agent sends get request to setting handler, handler replies with value.
        let inspect_agent_client = verify_payload(
            setting_handler_receptor,
            HandlerPayload::Command(Command::HandleRequest(Request::Get)),
        )
        .await;
        inspect_agent_client
            .reply(HandlerPayload::Result(Ok(Some(UnknownInfo(true).into()))).into())
            .send()
            .next()
            .await
            .expect("failed to reply to get request");

        // Inspect agent writes value to inspect.
        assert_inspect_tree!(inspector, root: {
            setting_values: {
                "Unknown": {
                    value: "UnknownInfo(true)",
                    timestamp: "0.000000000",
                }
            }
        });
    }

    /// Verifies that inspect agent intercepts setting change events and writes the setting value
    /// to inspect.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_write_inspect_on_changed() {
        // Set the clock so that timestamps will always be 0.
        clock::mock::set(Time::from_nanos(0));

        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child(INSPECT_NODE_NAME);
        let context = create_context().await;

        let proxy_signature = context
            .messenger_factory
            .create(MessengerType::Unbound)
            .await
            .expect("should create proxy messenger")
            .1
            .get_signature();

        let (setting_handler, setting_handler_receptor) =
            context.messenger_factory.create(MessengerType::Unbound).await.unwrap();

        InspectSettingAgent::create_with_node(context, inspect_node).await;

        // Setting handler notifies proxy of setting changed. The value does not
        // matter as it is fetched.
        // TODO(fxb/66294): Remove get call from inspect broker.
        setting_handler
            .message(
                HandlerPayload::Event(Event::Changed(UnknownInfo(true).into())).into(),
                Audience::Messenger(proxy_signature),
            )
            .send();

        // Inspect agent sends get request to setting handler, handler replies with value.
        let inspect_agent_client = verify_payload(
            setting_handler_receptor,
            HandlerPayload::Command(Command::HandleRequest(Request::Get)),
        )
        .await;
        inspect_agent_client
            .reply(HandlerPayload::Result(Ok(Some(UnknownInfo(true).into()))).into())
            .send()
            .next()
            .await
            .expect("failed to reply to get request");

        // Inspect agent writes value to inspect.
        assert_inspect_tree!(inspector, root: {
            setting_values: {
                "Unknown": {
                    value: "UnknownInfo(true)",
                    timestamp: "0.000000000",
                }
            }
        });
    }
}
