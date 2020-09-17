// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::sync::Arc;
use std::time::SystemTime;

use anyhow::{format_err, Error};
use fuchsia_async as fasync;
use fuchsia_inspect as inspect;
use fuchsia_inspect::Property;
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use futures::StreamExt;

use crate::clock;
use crate::handler::base::{Command, Event};
use crate::internal::handler::message::{Client, Factory, Messenger, Signature};
use crate::internal::handler::Payload;
use crate::message::base::{Audience, MessageEvent, MessengerType};
use crate::switchboard::base::{SettingRequest, SettingResponse};

/// A broker that listens in on messages between the proxy and setting handlers to record the
/// values of all settings to inspect.
pub struct InspectBroker {
    messenger_client: Messenger,
    inspect_node: Arc<inspect::Node>,
    setting_values: HashMap<&'static str, SettingInfo>,
}

/// Information about a setting to be written to inspect.
///
/// Inspect nodes and properties are not used, but need to be held as they're deleted from inspect
/// once they go out of scope.
struct SettingInfo {
    /// Node of this info.
    _node: inspect::Node,

    /// Debug string representation of the value of this setting.
    value: inspect::StringProperty,

    /// Milliseconds since Unix epoch that this setting's value was changed.
    timestamp: inspect::StringProperty,
}

impl InspectBroker {
    pub async fn create(
        messenger_factory: Factory,
        inspect_node: inspect::Node,
    ) -> Result<(), Error> {
        // Create broker to listen in on all messages between Proxy and setting handlers.
        let (messenger_client, mut receptor) =
            messenger_factory.create(MessengerType::Broker).await.unwrap();

        let broker = Arc::new(Mutex::new(Self {
            messenger_client,
            inspect_node: Arc::new(inspect_node),
            setting_values: HashMap::new(),
        }));

        fasync::Task::spawn(async move {
            while let Some(message_event) = receptor.next().await {
                if let MessageEvent::Message(payload, client) = message_event {
                    match payload {
                        // When we see a Restore message, we know a given setting is starting up, so
                        // we watch the reply to get the signature of the setting handler and ask
                        // for its value, so that we have the value of all settings immediately on
                        // start.
                        Payload::Command(Command::HandleRequest(SettingRequest::Restore)) => {
                            match InspectBroker::watch_reply(client).await {
                                Ok(reply_signature) => {
                                    broker
                                        .lock()
                                        .await
                                        .request_and_write_to_inspect(reply_signature)
                                        .await
                                        .ok();
                                }
                                Err(err) => {
                                    fx_log_err!("Failed to watch reply to restore: {:?}", err)
                                }
                            }
                        }
                        // Whenever we see a setting handler tell the proxy it changed, we ask it
                        // for its value again.
                        Payload::Event(Event::Changed) => {
                            broker
                                .lock()
                                .await
                                .request_and_write_to_inspect(client.get_author())
                                .await
                                .map_err(|err| {
                                    fx_log_err!("Failed to request value on change: {:?}", err)
                                })
                                .ok();
                        }
                        _ => {}
                    }
                }
            }
        })
        .detach();

        return Ok(());
    }

    /// Watches for the reply to a sent message and return the author of the reply.
    async fn watch_reply(mut client: Client) -> Result<Signature, Error> {
        let mut reply_receptor = client.observe();

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
    async fn request_value(&mut self, signature: Signature) -> Result<SettingResponse, Error> {
        let mut send_receptor = self
            .messenger_client
            .message(
                Payload::Command(Command::HandleRequest(SettingRequest::Get)),
                Audience::Messenger(signature),
            )
            .send();

        send_receptor.next_payload().await.and_then(|payload| {
            if let (Payload::Result(Ok(Some(setting))), _) = payload {
                Ok(setting)
            } else {
                Err(format_err!("did not receive setting value"))
            }
        })
    }

    /// Writes a setting value to inspect.
    async fn write_setting_to_inspect(&mut self, setting: SettingResponse) {
        let (key, value) = setting.for_inspect();

        let timestamp = clock::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .map(|duration| duration.as_millis())
            .unwrap_or(0);

        match self.setting_values.get_mut(key) {
            Some(setting) => {
                // Value already known, just update its fields.
                setting.timestamp.set(&timestamp.to_string());
                setting.value.set(&value);
            }
            None => {
                // Setting value not recorded yet, create a new inspect node.
                let node = self.inspect_node.create_child(key);
                let value_prop = node.create_string("value", value);
                let timestamp_prop = node.create_string("timestamp", timestamp.to_string());
                self.setting_values.insert(
                    key,
                    SettingInfo { _node: node, value: value_prop, timestamp: timestamp_prop },
                );
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use fuchsia_inspect::assert_inspect_tree;

    use crate::internal::handler::message::{create_hub, Receptor};
    use crate::internal::handler::Address;
    use crate::switchboard::base::SettingResponse;
    use crate::switchboard::intl_types::{IntlInfo, LocaleId, TemperatureUnit};

    use super::*;

    /// Verifies the next payload on the given receptor matches the given payload.
    ///
    /// Returns the message client of the received payload for convenience.
    async fn verify_payload(mut receptor: Receptor, expected: Payload) -> Client {
        let result = receptor.next_payload().await;
        assert!(result.is_ok());
        let (received, message_client) = result.unwrap();
        assert_eq!(received, expected);
        return message_client;
    }

    /// Verifies that inspect broker intercepts a restore request and writes the setting value to
    /// inspect.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_write_inspect_on_restore() {
        clock::mock::set(SystemTime::UNIX_EPOCH);

        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child("setting_values");

        let messenger_factory = create_hub();

        let (proxy, _) = messenger_factory.create(MessengerType::Unbound).await.unwrap();

        let setting_handler_address = Address::Handler(1);
        let (_, mut setting_handler_receptor) = messenger_factory
            .create(MessengerType::Addressable(setting_handler_address.clone()))
            .await
            .unwrap();

        InspectBroker::create(messenger_factory, inspect_node)
            .await
            .expect("could not create inspect");

        // Proxy sends restore request.
        proxy
            .message(
                Payload::Command(Command::HandleRequest(SettingRequest::Restore)),
                Audience::Address(setting_handler_address),
            )
            .send();

        // Setting handler acks the restore.
        let (_, reply_client) = setting_handler_receptor.next_payload().await.unwrap();
        reply_client.reply(Payload::Result(Ok(None))).send().ack();

        // Inspect broker sends get request to setting handler, handler replies with value.
        let inspect_broker_client = verify_payload(
            setting_handler_receptor,
            Payload::Command(Command::HandleRequest(SettingRequest::Get)),
        )
        .await;
        inspect_broker_client
            .reply(Payload::Result(Ok(Some(SettingResponse::Intl(IntlInfo {
                locales: Some(vec![LocaleId { id: "en-US".to_string() }]),
                temperature_unit: Some(TemperatureUnit::Celsius),
                time_zone_id: Some("UTC".to_string()),
                hour_cycle: None,
            })))))
            .send()
            .next()
            .await
            .expect("failed to reply to get request");

        // Inspect broker writes value to inspect.
        assert_inspect_tree!(inspector, root: {
            setting_values: {
                "Intl": {
                    value: "IntlInfo { locales: Some([LocaleId { id: \"en-US\" }]), temperature_unit: Some(Celsius), time_zone_id: Some(\"UTC\"), hour_cycle: None }",
                    timestamp: "0",
                }
            }
        });
    }

    /// Verifies that inspect broker intercepts setting change events and writes the setting value
    /// to inspect.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_write_inspect_on_changed() {
        clock::mock::set(SystemTime::UNIX_EPOCH);

        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child("setting_values");

        let messenger_factory = create_hub();

        let (proxy_messenger_client, _) =
            messenger_factory.create(MessengerType::Unbound).await.unwrap();

        let setting_handler_address = Address::Handler(1);
        let (setting_handler, setting_handler_receptor) = messenger_factory
            .create(MessengerType::Addressable(setting_handler_address.clone()))
            .await
            .unwrap();

        InspectBroker::create(messenger_factory, inspect_node)
            .await
            .expect("could not create inspect");

        // Setting handler notifies proxy of setting changed.
        setting_handler
            .message(
                Payload::Event(Event::Changed),
                Audience::Messenger(proxy_messenger_client.get_signature()),
            )
            .send();

        // Inspect broker sends get request to setting handler, handler replies with value.
        let inspect_broker_client = verify_payload(
            setting_handler_receptor,
            Payload::Command(Command::HandleRequest(SettingRequest::Get)),
        )
        .await;
        inspect_broker_client
            .reply(Payload::Result(Ok(Some(SettingResponse::Intl(IntlInfo {
                locales: Some(vec![LocaleId { id: "en-US".to_string() }]),
                temperature_unit: Some(TemperatureUnit::Celsius),
                time_zone_id: Some("UTC".to_string()),
                hour_cycle: None,
            })))))
            .send()
            .next()
            .await
            .expect("failed to reply to get request");

        // Inspect broker writes value to inspect.
        assert_inspect_tree!(inspector, root: {
            setting_values: {
                "Intl": {
                    value: "IntlInfo { locales: Some([LocaleId { id: \"en-US\" }]), temperature_unit: Some(Celsius), time_zone_id: Some(\"UTC\"), hour_cycle: None }",
                    timestamp: "0",
                }
            }
        });
    }
}
