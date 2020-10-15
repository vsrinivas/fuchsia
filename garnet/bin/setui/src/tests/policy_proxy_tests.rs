// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::policy::{PolicyId, Request, Response};
use crate::internal::core;
use crate::internal::core::message::Messenger;
use crate::internal::policy;
use crate::internal::policy::Address;
use crate::message::base::{Audience, MessengerType};
use crate::policy::base as policy_base;
use crate::policy::policy_handler::{PolicyHandler, Transform};
use crate::policy::policy_proxy::PolicyProxy;
use crate::switchboard::base::{SettingRequest, SettingType};
use async_trait::async_trait;

struct FakePolicyHandler {
    policy_response: policy_base::response::Response,
    setting_response: Option<Transform>,
}

impl FakePolicyHandler {
    fn create(
        policy_response: policy_base::response::Response,
        setting_response: Option<Transform>,
    ) -> Self {
        Self { policy_response, setting_response }
    }
}

#[async_trait]
impl PolicyHandler for FakePolicyHandler {
    async fn handle_policy_request(
        &mut self,
        _request: policy_base::Request,
    ) -> policy_base::response::Response {
        self.policy_response.clone()
    }

    async fn handle_setting_request(
        &mut self,
        _request: SettingRequest,
        _messenger: Messenger,
    ) -> Option<Transform> {
        self.setting_response.clone()
    }
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_policy_proxy_creation() {
    let core_messenger_factory = core::message::create_hub();
    let policy_messenger_factory = policy::message::create_hub();

    // Create the policy proxy.
    let policy_proxy_result = PolicyProxy::create(
        SettingType::Audio,
        Box::new(FakePolicyHandler::create(
            Ok(policy_base::response::Payload::Audio(Response::Policy(PolicyId::create(0)))),
            None,
        )),
        core_messenger_factory,
        policy_messenger_factory,
    )
    .await;

    // Creation is successful.
    assert!(policy_proxy_result.is_ok());
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_policy_messages_passed_to_handler() {
    let setting_type = SettingType::Audio;
    let policy_request = policy_base::Request::Audio(Request::Get);
    let policy_payload =
        policy_base::response::Payload::Audio(Response::Policy(PolicyId::create(0)));

    let core_messenger_factory = core::message::create_hub();
    let policy_messenger_factory = policy::message::create_hub();
    // Initialize the policy proxy and a messenger to communicate with it.
    PolicyProxy::create(
        setting_type,
        Box::new(FakePolicyHandler::create(Ok(policy_payload.clone()), None)),
        core_messenger_factory,
        policy_messenger_factory.clone(),
    )
    .await
    .ok();
    let (policy_messenger, _) =
        policy_messenger_factory.create(MessengerType::Unbound).await.unwrap();

    // Send a policy request to the policy proxy.
    let mut policy_send_receptor = policy_messenger
        .message(
            policy::Payload::Request(policy_request),
            Audience::Address(Address::Policy(setting_type)),
        )
        .send();

    // Wait for a response.
    let (policy_response, _) = policy_send_receptor.next_payload().await.unwrap();

    // Policy handler returned its response through the policy proxy, back to the client.
    assert_eq!(policy_response, policy::Payload::Response(Ok(policy_payload)));
}

/// Exercises the main loop in the policy proxy by sending a series of messages and ensuring they're
/// all answered.
#[fuchsia_async::run_until_stalled(test)]
async fn test_multiple_messages() {
    let setting_type = SettingType::Audio;
    let policy_request = policy_base::Request::Audio(Request::Get);
    let policy_payload =
        policy_base::response::Payload::Audio(Response::Policy(PolicyId::create(0)));

    let core_messenger_factory = core::message::create_hub();
    let policy_messenger_factory = policy::message::create_hub();
    // Initialize the policy proxy and a messenger to communicate with it.
    PolicyProxy::create(
        setting_type,
        Box::new(FakePolicyHandler::create(Ok(policy_payload.clone()), None)),
        core_messenger_factory,
        policy_messenger_factory.clone(),
    )
    .await
    .ok();
    let (policy_messenger, _) =
        policy_messenger_factory.create(MessengerType::Unbound).await.unwrap();

    // Send a few policy requests to the policy proxy.
    for _ in 0..3 {
        let mut policy_send_receptor = policy_messenger
            .message(
                policy::Payload::Request(policy_request.clone()),
                Audience::Address(Address::Policy(setting_type)),
            )
            .send();

        // Verify a response is returned each time.
        let (policy_response, _) = policy_send_receptor.next_payload().await.unwrap();
        assert_eq!(policy_response, policy::Payload::Response(Ok(policy_payload.clone())));

        // TODO(fxbug.dev/59747): Send some core message hub messages as well to test interleaved
        // messages.
    }
}
