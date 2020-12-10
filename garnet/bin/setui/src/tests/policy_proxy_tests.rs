// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::policy::{PolicyId, Request, Response};
use crate::handler::base::SettingHandlerResult;
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::internal::core;
use crate::internal::policy;
use crate::internal::policy::Address;
use crate::message::base::{Audience, MessengerType};
use crate::policy::base as policy_base;
use crate::policy::base::{BoxedHandler, PolicyHandlerFactory};
use crate::policy::policy_handler::{PolicyHandler, Transform};
use crate::policy::policy_handler_factory_impl::PolicyHandlerFactoryImpl;
use crate::policy::policy_proxy::PolicyProxy;
use crate::switchboard::base::{
    PrivacyInfo, SettingAction, SettingActionData, SettingEvent, SettingRequest, SettingResponse,
    SettingType,
};
use crate::tests::message_utils::verify_payload;
use async_trait::async_trait;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use std::sync::atomic::AtomicU64;
use std::sync::Arc;

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

/// `FakePolicyHandler` always returns the provided responses for handling policy/setting requests.
#[derive(Clone)]
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
        _messenger: core::message::Messenger,
    ) -> Option<Transform> {
        self.setting_response.clone()
    }
}

/// Creates a handler factory with the given `FakePolicyHandler`.
fn create_handler_factory(
    storage_factory_handler: Arc<Mutex<InMemoryStorageFactory>>,
    policy_handler: FakePolicyHandler,
) -> Arc<Mutex<dyn PolicyHandlerFactory + Send + Sync>> {
    let mut handler_factory = PolicyHandlerFactoryImpl::new(
        vec![SETTING_TYPE].into_iter().collect(),
        storage_factory_handler,
        Arc::new(AtomicU64::new(1)),
    );
    handler_factory.register(
        SETTING_TYPE,
        Box::new(move |_| {
            let handler_clone = policy_handler.clone();
            Box::pin(async move { Ok(Box::new(handler_clone) as BoxedHandler) })
        }),
    );
    Arc::new(Mutex::new(handler_factory))
}

/// Simple test that verifies the constructor succeeds.
#[fuchsia_async::run_until_stalled(test)]
async fn test_policy_proxy_creation() {
    let core_messenger_factory = core::message::create_hub();
    let policy_messenger_factory = policy::message::create_hub();
    let storage_factory = InMemoryStorageFactory::create();
    let handler_factory = create_handler_factory(
        storage_factory,
        FakePolicyHandler::create(
            Ok(policy_base::response::Payload::Audio(Response::Policy(PolicyId::create(0)))),
            None,
        ),
    );

    // Create the policy proxy.
    let policy_proxy_result = PolicyProxy::create(
        SETTING_TYPE,
        handler_factory,
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
    let storage_factory = InMemoryStorageFactory::create();
    let handler_factory = create_handler_factory(
        storage_factory,
        FakePolicyHandler::create(Ok(policy_payload.clone()), None),
    );

    let core_messenger_factory = core::message::create_hub();
    let policy_messenger_factory = policy::message::create_hub();
    // Initialize the policy proxy and a messenger to communicate with it.
    PolicyProxy::create(
        SETTING_TYPE,
        handler_factory,
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
    let storage_factory = InMemoryStorageFactory::create();
    // Include None as the transform result so that the message passes through the policy layer
    // without interruption.
    let handler_factory = create_handler_factory(
        storage_factory,
        FakePolicyHandler::create(Err(policy_base::response::Error::Unexpected), None),
    );
    PolicyProxy::create(
        SETTING_TYPE,
        handler_factory,
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
    let mut setting_handler_receptor = core_messenger_factory
        .create(MessengerType::Unbound)
        .await
        .expect("messenger should be created")
        .1;

    // Send a setting request from the switchboard to the setting handler.
    let mut settings_send_receptor = switchboard_messenger
        .message(
            SETTING_REQUEST_PAYLOAD.clone(),
            Audience::Messenger(setting_handler_receptor.get_signature()),
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
    let storage_factory = InMemoryStorageFactory::create();
    // Include a different response than the original as the transform result, so that the
    // original request is ignored.
    let handler_factory = create_handler_factory(
        storage_factory,
        FakePolicyHandler::create(
            Err(policy_base::response::Error::Unexpected),
            Some(Transform::Result(SETTING_RESULT_NO_RESPONSE.clone())),
        ),
    );
    PolicyProxy::create(
        SETTING_TYPE,
        handler_factory,
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
    let mut setting_handler_receptor = core_messenger_factory
        .create(MessengerType::Unbound)
        .await
        .expect("messenger should be created")
        .1;

    // Send a setting request from the switchboard to the setting handler.
    let mut settings_send_receptor = switchboard_messenger
        .message(
            SETTING_REQUEST_PAYLOAD.clone(),
            Audience::Messenger(setting_handler_receptor.get_signature()),
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
            Audience::Messenger(setting_handler_receptor.get_signature()),
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

/// Verify that when the policy handler returns a new request payload, that the payload is sent to
/// the setting handler in place of the original message.
#[fuchsia_async::run_until_stalled(test)]
async fn test_setting_message_payload_replacement() {
    // Original request that will be sent by the switchboard.
    let setting_request_1 = SettingRequest::Get;
    let setting_request_1_payload = core::Payload::Action(SettingAction {
        id: REQUEST_ID,
        setting_type: SETTING_TYPE,
        data: SettingActionData::Request(setting_request_1.clone()),
    });

    // Modified request that the policy handler will return.
    let setting_request_2 = SettingRequest::Restore;
    let setting_request_2_payload = core::Payload::Action(SettingAction {
        id: REQUEST_ID,
        setting_type: SETTING_TYPE,
        data: SettingActionData::Request(setting_request_2.clone()),
    });

    let core_messenger_factory = core::message::create_hub();
    let policy_messenger_factory = policy::message::create_hub();
    let storage_factory = InMemoryStorageFactory::create();
    // Fake handler will return request 2 to be sent to the setting handler.
    let handler_factory = create_handler_factory(
        storage_factory,
        FakePolicyHandler::create(
            Err(policy_base::response::Error::Unexpected),
            Some(Transform::Request(setting_request_2)),
        ),
    );
    PolicyProxy::create(
        SETTING_TYPE,
        handler_factory,
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
    let mut setting_handler_receptor = core_messenger_factory
        .create(MessengerType::Unbound)
        .await
        .expect("setting handler should be created")
        .1;

    // Send a setting request from the switchboard to the setting handler.
    let mut settings_send_receptor = switchboard_messenger
        .message(
            setting_request_1_payload,
            Audience::Messenger(setting_handler_receptor.get_signature()),
        )
        .send();

    // Verify the setting handler receives the payload that the policy handler specifies, not the
    // original sent by the switchboard.
    verify_payload(
        setting_request_2_payload,
        &mut setting_handler_receptor,
        Some(Box::new(|client| -> BoxFuture<'_, ()> {
            Box::pin(async move {
                client.reply(SETTING_RESPONSE_PAYLOAD.clone()).send();
            })
        })),
    )
    .await;

    // Verify that the "switchboard" receives the response that the setting handler returned.
    verify_payload(SETTING_RESPONSE_PAYLOAD.clone(), &mut settings_send_receptor, None).await;
}

/// Exercises the main loop in the policy proxy by sending a series of messages and ensuring they're
/// all answered.
#[fuchsia_async::run_until_stalled(test)]
async fn test_multiple_messages() {
    let policy_request = policy_base::Request::Audio(Request::Get);
    let policy_payload =
        policy_base::response::Payload::Audio(Response::Policy(PolicyId::create(0)));

    let core_messenger_factory = core::message::create_hub();
    let policy_messenger_factory = policy::message::create_hub();
    let storage_factory = InMemoryStorageFactory::create();
    let handler_factory = create_handler_factory(
        storage_factory,
        FakePolicyHandler::create(
            Ok(policy_payload.clone()),
            Some(Transform::Result(SETTING_RESULT_NO_RESPONSE.clone())),
        ),
    );
    // Initialize the policy proxy and a messenger to communicate with it.
    PolicyProxy::create(
        SETTING_TYPE,
        handler_factory,
        core_messenger_factory.clone(),
        policy_messenger_factory.clone(),
    )
    .await
    .ok();

    // Create a messagenger for sending messages directly to the policy proxy.
    let (policy_messenger, _) =
        policy_messenger_factory.create(MessengerType::Unbound).await.unwrap();
    // Create a messenger that represents the switchboard.
    let (switchboard_messenger, _) = core_messenger_factory
        .create(MessengerType::Addressable(core::Address::Switchboard))
        .await
        .unwrap();
    // Create a messenger that represents a setting handler.
    let setting_handler_signature = core_messenger_factory
        .create(MessengerType::Unbound)
        .await
        .expect("messenger should be created")
        .1
        .get_signature();

    // Send a few requests to the policy proxy.
    for _ in 0..3 {
        // Send a policy request.
        let mut policy_send_receptor = policy_messenger
            .message(
                policy::Payload::Request(policy_request.clone()),
                Audience::Address(Address::Policy(SETTING_TYPE)),
            )
            .send();

        // Verify a policy response is returned each time.
        verify_payload(
            policy::Payload::Response(Ok(policy_payload.clone())),
            &mut policy_send_receptor,
            None,
        )
        .await;

        // Send a switchboard request that the policy proxy intercepts.
        let mut settings_send_receptor = switchboard_messenger
            .message(
                SETTING_REQUEST_PAYLOAD.clone(),
                Audience::Messenger(setting_handler_signature),
            )
            .send();

        // Verify that the "switchboard" receives the response that the policy handler returns
        // each time.
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
}
