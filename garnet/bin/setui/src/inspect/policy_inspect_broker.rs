// Copyright 2021 The Fuchsia Authors. All rights reserved.
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
use futures::StreamExt;

use crate::base::SettingType;
use crate::clock;
use crate::internal::policy;
use crate::internal::policy::message::{Audience, Factory, MessageClient, Messenger, Signature};
use crate::internal::policy::Payload;
use crate::message::base::{MessageEvent, MessengerType};
use crate::policy::base as policy_base;
use crate::policy::base::Request;

/// A broker that listens in on messages sent on the policy message hub to policy proxies handlers
/// to record their internal state to inspect.
pub struct PolicyInspectBroker {
    messenger_client: Messenger,
    inspect_node: Arc<inspect::Node>,
    policy_values: HashMap<String, PolicyInspectInfo>,
}

/// Information about a policy to be written to inspect.
///
/// Inspect nodes and properties are not used, but need to be held as they're deleted from inspect
/// once they go out of scope.
struct PolicyInspectInfo {
    /// Node of this info.
    _node: inspect::Node,

    /// Debug string representation of the state of this policy.
    value: inspect::StringProperty,

    /// Milliseconds since Unix epoch that this policy was modified.
    timestamp: inspect::StringProperty,
}

impl PolicyInspectBroker {
    pub async fn create(
        messenger_factory: Factory,
        policy_proxies: HashMap<policy::message::Signature, SettingType>,
        inspect_node: inspect::Node,
    ) -> Result<(), Error> {
        // Create broker to listen in on all messages on the policy message hub.
        let (messenger_client, mut receptor) =
            messenger_factory.create(MessengerType::Broker(None)).await.unwrap();

        let mut broker = Self {
            messenger_client,
            inspect_node: Arc::new(inspect_node),
            policy_values: HashMap::new(),
        };

        fasync::Task::spawn(async move {
            // Request initial values from all policy types.
            // TODO(fxbug.dev/68266): consider how to get initial policy values without needing
            // explicit mapping of policy proxies.
            for (signature, policy_type) in policy_proxies.iter() {
                if let Err(err) = broker.request_and_write_to_inspect(signature.clone()).await {
                    return fx_log_err!(
                        "Failed request initial value from policy type {:?}: {:?}",
                        policy_type,
                        err
                    );
                }
            }

            while let Some(message_event) = receptor.next().await {
                if let MessageEvent::Message(Payload::Request(_), client) = message_event {
                    // When we see a request to a policy proxy, we assume that the policy will be
                    // modified, so we wait for the reply to get the signature of the proxy, then
                    // ask the proxy for its latest value.
                    match PolicyInspectBroker::watch_reply(client).await {
                        Ok(reply_signature) => {
                            if let Err(err) =
                                broker.request_and_write_to_inspect(reply_signature).await
                            {
                                return fx_log_err!(
                                    "Failed request value from policy proxy: {:?}",
                                    err
                                );
                            }
                        }
                        Err(err) => {
                            fx_log_err!("Failed to watch reply to request: {:?}", err)
                        }
                    }
                }
            }
        })
        .detach();

        return Ok(());
    }

    /// Watches for the reply to a sent message and returns the author of the reply.
    async fn watch_reply(mut client: MessageClient) -> Result<Signature, Error> {
        let mut reply_receptor = client.observe();

        reply_receptor.next_payload().await.map(|(_, reply_client)| reply_client.get_author())
    }

    /// Requests the policy state from a given signature for a policy handler and records the result
    /// in inspect.
    async fn request_and_write_to_inspect(&mut self, signature: Signature) -> Result<(), Error> {
        // Send the request to the policy proxy.
        let mut send_receptor = self
            .messenger_client
            .message(Payload::Request(Request::Get), Audience::Messenger(signature))
            .send();

        // Wait for a response from the policy proxy.
        let (payload, _) = send_receptor.next_payload().await?;

        self.write_response_to_inspect(payload).await
    }

    async fn write_response_to_inspect(&mut self, payload: Payload) -> Result<(), Error> {
        let policy_info =
            if let Payload::Response(Ok(policy_base::response::Payload::PolicyInfo(policy_info))) =
                payload
            {
                policy_info
            } else {
                return Err(format_err!("did not receive policy state"));
            };

        // Convert the response to a string for inspect.
        let policy_name = policy_info.name();
        let inspect_str = policy_info.value_str();

        let timestamp = clock::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .map(|duration| duration.as_millis())
            .unwrap_or(0);

        match self.policy_values.get_mut(&policy_name.to_string()) {
            Some(policy_info) => {
                // Value already known, just update its fields.
                policy_info.timestamp.set(&timestamp.to_string());
                policy_info.value.set(&inspect_str);
            }
            None => {
                // Policy info not recorded yet, create a new inspect node.
                let node = self.inspect_node.create_child(policy_name);
                let value_prop = node.create_string("value", inspect_str);
                let timestamp_prop = node.create_string("timestamp", timestamp.to_string());
                self.policy_values.insert(
                    policy_name.to_string(),
                    PolicyInspectInfo { _node: node, value: value_prop, timestamp: timestamp_prop },
                );
            }
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use fuchsia_inspect as inspect;
    use fuchsia_inspect::assert_inspect_tree;
    use futures::future::BoxFuture;
    use futures::StreamExt;
    use std::collections::HashMap;
    use std::time::SystemTime;

    use crate::internal::policy::message::{create_hub, Audience, Signature};
    use crate::internal::policy::Payload;

    use crate::audio::policy as audio_policy;
    use crate::audio::policy::{PolicyId, StateBuilder, TransformFlags};
    use crate::audio::types::AudioStreamType;
    use crate::base::SettingType;
    use crate::clock;
    use crate::inspect::policy_inspect_broker::PolicyInspectBroker;
    use crate::message::base::{MessageEvent, MessengerType, Status};
    use crate::policy::base as policy_base;
    use crate::policy::base::PolicyInfo;
    use crate::tests::message_utils::verify_payload;

    const GET_REQUEST: Payload = Payload::Request(policy_base::Request::Get);

    /// Verifies that inspect broker requests and writes state for each policy on start.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_write_policy_inspect_on_start() {
        // Set the clock so that timestamps will always be 0.
        clock::mock::set(SystemTime::UNIX_EPOCH);

        let messenger_factory = create_hub();

        // Create a receptor representing the policy proxy.
        let (_, mut policy_receptor) =
            messenger_factory.create(MessengerType::Unbound).await.unwrap();

        // Create the inspect broker.
        let mut policy_proxy_signatures: HashMap<Signature, SettingType> = HashMap::new();
        policy_proxy_signatures.insert(policy_receptor.get_signature(), SettingType::Audio);

        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child("policy_values");
        PolicyInspectBroker::create(messenger_factory, policy_proxy_signatures, inspect_node)
            .await
            .expect("could not create policy inspect broker");

        let expected_state = StateBuilder::new()
            .add_property(AudioStreamType::Media, TransformFlags::TRANSFORM_MAX)
            .build();

        // Policy proxy receives a get request on start and returns the state.
        let state_clone = expected_state.clone();
        verify_payload(
            GET_REQUEST.clone(),
            &mut policy_receptor,
            Some(Box::new(|client| -> BoxFuture<'_, ()> {
                Box::pin(async move {
                    let mut receptor = client
                        .reply(Payload::Response(Ok(policy_base::response::Payload::PolicyInfo(
                            PolicyInfo::Audio(state_clone),
                        ))))
                        .send();
                    // Wait until the policy inspect broker receives the message and writes to
                    // inspect.
                    while let Some(event) = receptor.next().await {
                        match event {
                            MessageEvent::Status(Status::Received) => {
                                return;
                            }
                            _ => {}
                        }
                    }
                })
            })),
        )
        .await;

        // Inspect broker writes value to inspect.
        assert_inspect_tree!(inspector, root: {
            policy_values: {
                "Audio": {
                    value: format!("{:?}", expected_state),
                    timestamp: "0",
                }
            }
        });
    }

    /// Verifies that inspect broker intercepts policy requests and writes their values to inspect.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_write_inspect_on_changed() {
        // Set the clock so that timestamps will always be 0.
        clock::mock::set(SystemTime::UNIX_EPOCH);

        let messenger_factory = create_hub();

        // Create a receptor representing the policy proxy.
        let (_, mut policy_receptor) =
            messenger_factory.create(MessengerType::Unbound).await.unwrap();

        // Create a messenger on the policy message hub to send requests for the inspect broker to
        // intercept.
        let (policy_sender, _) = messenger_factory.create(MessengerType::Unbound).await.unwrap();

        // Create the inspect broker.
        let mut policy_proxy_signatures: HashMap<Signature, SettingType> = HashMap::new();
        policy_proxy_signatures.insert(policy_receptor.get_signature(), SettingType::Audio);

        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child("policy_values");
        PolicyInspectBroker::create(messenger_factory, policy_proxy_signatures, inspect_node)
            .await
            .expect("could not create policy inspect broker");

        // Starting state for audio policy.
        let initial_state = StateBuilder::new()
            .add_property(AudioStreamType::Media, TransformFlags::TRANSFORM_MAX)
            .build();

        // While this isn't a change in state that would happen in the real world, it's fine for
        // testing.
        let expected_state = StateBuilder::new()
            .add_property(AudioStreamType::Background, TransformFlags::TRANSFORM_MIN)
            .build();

        // Policy proxy receives a get request on start and returns the initial state.
        verify_payload(
            GET_REQUEST.clone(),
            &mut policy_receptor,
            Some(Box::new(|client| -> BoxFuture<'_, ()> {
                Box::pin(async move {
                    client
                        .reply(Payload::Response(Ok(policy_base::response::Payload::PolicyInfo(
                            PolicyInfo::Audio(initial_state),
                        ))))
                        .send();
                })
            })),
        )
        .await;

        // Send a message to the policy proxy.
        policy_sender
            .message(GET_REQUEST.clone(), Audience::Messenger(policy_receptor.get_signature()))
            .send();

        // Policy proxy receives the request from the policy_sender. Inspect broker waits for a
        // reply to know where to ask for the policy state so send a nonsensical reply.
        verify_payload(
            GET_REQUEST.clone(),
            &mut policy_receptor,
            Some(Box::new(|client| -> BoxFuture<'_, ()> {
                Box::pin(async move {
                    client
                        .reply(Payload::Response(Ok(policy_base::response::Payload::Audio(
                            audio_policy::Response::Policy(PolicyId::create(0)),
                        ))))
                        .send();
                })
            })),
        )
        .await;

        // Policy proxy receives a get request from the inspect broker and returns the expected
        // state.
        let state_clone = expected_state.clone();
        verify_payload(
            GET_REQUEST.clone(),
            &mut policy_receptor,
            Some(Box::new(|client| -> BoxFuture<'_, ()> {
                Box::pin(async move {
                    let mut receptor = client
                        .reply(Payload::Response(Ok(policy_base::response::Payload::PolicyInfo(
                            PolicyInfo::Audio(state_clone),
                        ))))
                        .send();
                    // Wait until the policy inspect broker receives the message and writes to
                    // inspect.
                    while let Some(event) = receptor.next().await {
                        match event {
                            MessageEvent::Status(Status::Received) => {
                                return;
                            }
                            _ => {}
                        }
                    }
                })
            })),
        )
        .await;

        // Inspect broker writes value to inspect.
        assert_inspect_tree!(inspector, root: {
            policy_values: {
                "Audio": {
                    value: format!("{:?}", expected_state),
                    timestamp: "0",
                }
            }
        });
    }
}
