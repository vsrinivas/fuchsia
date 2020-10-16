// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::policy::{PolicyId, Request, Response};
use crate::handler::base::SettingHandlerResult;
use crate::internal::core;
use crate::internal::core::message::Messenger;
use crate::internal::policy;
use crate::internal::policy::Address;
use crate::message::base::{Audience, MessengerType};
use crate::policy::base as policy_base;
use crate::policy::policy_handler::{PolicyHandler, Transform};
use crate::policy::policy_proxy::PolicyProxy;
use crate::switchboard::base::{
    PrivacyInfo, SettingAction, SettingActionData, SettingEvent, SettingRequest, SettingResponse,
    SettingType,
};
use crate::tests::message_utils::verify_payload;
use async_trait::async_trait;
use futures::future::BoxFuture;

static REQUEST_ID: u64 = 100;
static SETTING_TYPE: SettingType = SettingType::Privacy;
static SETTING_REQUEST_PAYLOAD: core::Payload = core::Payload::Action(SettingAction {
    id: REQUEST_ID,
    setting_type: SETTING_TYPE,
    data: SettingActionData::Request(SettingRequest::Get),
});
static SETTING_REQUEST_PAYLOAD_2: core::Payload = core::Payload::Action(SettingAction {
    id: REQUEST_ID,
    setting_type: SETTING_TYPE,
    data: SettingActionData::Listen(1),
});
static SETTING_RESPONSE_PAYLOAD: core::Payload = core::Payload::Event(SettingEvent::Response(
    REQUEST_ID,
    Ok(Some(SettingResponse::Privacy(PrivacyInfo { user_data_sharing_consent: Some(true) }))),
));
static SETTING_RESULT_NO_RESPONSE: SettingHandlerResult = Ok(None);

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

/// Simple test that verifies the constructor succeeds.
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

/// Verify that policy messages sent to the proxy are passed to the handler, then that the handler's
/// response is returned via the proxy.
#[fuchsia_async::run_until_stalled(test)]
async fn test_policy_messages_passed_to_handler() {
    let policy_request = policy_base::Request::Audio(Request::Get);
    let policy_payload =
        policy_base::response::Payload::Audio(Response::Policy(PolicyId::create(0)));

    let core_messenger_factory = core::message::create_hub();
    let policy_messenger_factory = policy::message::create_hub();
    // Initialize the policy proxy and a messenger to communicate with it.
    PolicyProxy::create(
        SETTING_TYPE,
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
            Audience::Address(Address::Policy(SETTING_TYPE)),
        )
        .send();

    // Wait for a response.
    let (policy_response, _) = policy_send_receptor.next_payload().await.unwrap();

    // Policy handler returned its response through the policy proxy, back to the client.
    assert_eq!(policy_response, policy::Payload::Response(Ok(policy_payload)));
}

/// Verify that when the policy handler doesn't take any action on a setting request, it will
/// continue on to its intended destination without interference.
#[fuchsia_async::run_until_stalled(test)]
async fn test_setting_message_pass_through() {
    let core_messenger_factory = core::message::create_hub();
    let policy_messenger_factory = policy::message::create_hub();
    PolicyProxy::create(
        SETTING_TYPE,
        // Include None as the transform result so that the message passes through the policy layer
        // without interruption.
        Box::new(FakePolicyHandler::create(Err(policy_base::response::Error::Unexpected), None)),
        core_messenger_factory.clone(),
        policy_messenger_factory,
    )
    .await
    .ok();

    // Create a messenger that represents the switchboard.
    let (switchboard_messenger, _) = core_messenger_factory
        .create(MessengerType::Addressable(core::Address::Switchboard))
        .await
        .unwrap();
    // Create a messenger that represents a setting handler.
    let (setting_handler_messenger, mut setting_handler_receptor) =
        core_messenger_factory.create(MessengerType::Unbound).await.unwrap();

    // Send a setting request from the switchboard to the setting handler.
    let mut settings_send_receptor = switchboard_messenger
        .message(
            SETTING_REQUEST_PAYLOAD.clone(),
            Audience::Messenger(setting_handler_messenger.get_signature()),
        )
        .send();

    // Verify the setting handler received the original payload, then reply with a response.
    verify_payload(
        SETTING_REQUEST_PAYLOAD.clone(),
        &mut setting_handler_receptor,
        Some(Box::new(|client| -> BoxFuture<'_, ()> {
            Box::pin(async move {
                client.reply(SETTING_RESPONSE_PAYLOAD.clone()).send();
            })
        })),
    )
    .await;

    // Verify that the "switchboard" receives the response the setting handler sent.
    verify_payload(SETTING_RESPONSE_PAYLOAD.clone(), &mut settings_send_receptor, None).await;
}

/// Verify that when the policy handler returns a result to give directly to the client, that the
/// given result is provided back to the switchboard without reaching the setting handler.
#[fuchsia_async::run_until_stalled(test)]
async fn test_setting_message_result_replacement() {
    let core_messenger_factory = core::message::create_hub();
    let policy_messenger_factory = policy::message::create_hub();
    PolicyProxy::create(
        SETTING_TYPE,
        // Include a different response than the original as the transform result, so that the
        // original request is ignored.
        Box::new(FakePolicyHandler::create(
            Err(policy_base::response::Error::Unexpected),
            Some(Transform::Result(SETTING_RESULT_NO_RESPONSE.clone())),
        )),
        core_messenger_factory.clone(),
        policy_messenger_factory,
    )
    .await
    .ok();

    // Create a messenger that represents the switchboard.
    let (switchboard_messenger, _) = core_messenger_factory
        .create(MessengerType::Addressable(core::Address::Switchboard))
        .await
        .unwrap();
    // Create a messenger that represents a setting handler.
    let (setting_handler_messenger, mut setting_handler_receptor) =
        core_messenger_factory.create(MessengerType::Unbound).await.unwrap();

    // Send a setting request from the switchboard to the setting handler.
    let mut settings_send_receptor = switchboard_messenger
        .message(
            SETTING_REQUEST_PAYLOAD.clone(),
            Audience::Messenger(setting_handler_messenger.get_signature()),
        )
        .send();

    // We want to verify that the setting handler didn't receive the original message from the
    // switchboard. To do this, we create a new messenger and send a message to the setting handler
    // and wait for it to be received. Since messages are delivered in-order, if the setting handler
    // received the switchboard's message, this will fail. We can't send a message as the
    // switchboard, since the policy proxy will intercept it.
    let (test_messenger, _) = core_messenger_factory.create(MessengerType::Unbound).await.unwrap();
    test_messenger
        .message(
            SETTING_REQUEST_PAYLOAD_2.clone(),
            Audience::Messenger(setting_handler_messenger.get_signature()),
        )
        .send();
    verify_payload(SETTING_REQUEST_PAYLOAD_2.clone(), &mut setting_handler_receptor, None).await;

    // Verify that the "switchboard" receives the response that the policy handler returned.
    verify_payload(
        core::Payload::Event(SettingEvent::Response(
            REQUEST_ID,
            SETTING_RESULT_NO_RESPONSE.clone(),
        )),
        &mut settings_send_receptor,
        None,
    )
    .await;
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
