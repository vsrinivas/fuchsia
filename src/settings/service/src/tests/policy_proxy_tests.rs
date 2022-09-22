// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType, UnknownInfo as SettingUnknownInfo};
use crate::handler::base::{Payload, Request, Response as SettingResponse};
use crate::handler::setting_handler::SettingHandlerResult;
use crate::message::base::{Audience, MessengerType};
use crate::message::MessageHubUtil;
use crate::policy::policy_handler::{PolicyHandler, RequestTransform, ResponseTransform};
use crate::policy::policy_handler_factory_impl::PolicyHandlerFactoryImpl;
use crate::policy::policy_proxy::PolicyProxy;
use crate::policy::{
    self as policy_base, BoxedHandler, PolicyHandlerFactory, PolicyType, UnknownInfo,
};
use crate::service;
use crate::storage::testing::InMemoryStorageFactory;
use crate::tests::message_utils::verify_payload;
use async_trait::async_trait;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use std::convert::TryFrom;
use std::sync::atomic::AtomicU64;
use std::sync::Arc;

static POLICY_TYPE: PolicyType = PolicyType::Unknown;
static SETTING_TYPE: SettingType = SettingType::Unknown;
static SETTING_REQUEST: Request = Request::Get;
static SETTING_REQUEST_PAYLOAD: Payload = Payload::Request(Request::Get);
static SETTING_REQUEST_PAYLOAD_2: Payload = Payload::Request(Request::Listen);

static SETTING_RESPONSE: SettingResponse = Ok(Some(SettingInfo::Unknown(SettingUnknownInfo(true))));

static SETTING_RESPONSE_PAYLOAD: Payload =
    Payload::Response(Ok(Some(SettingInfo::Unknown(SettingUnknownInfo(true)))));

static SETTING_RESPONSE_MODIFIED: SettingResponse =
    Ok(Some(SettingInfo::Unknown(SettingUnknownInfo(false))));

static SETTING_RESULT_NO_RESPONSE: SettingResponse = Ok(None);
static HANDLER_RESULT_NO_RESPONSE: SettingHandlerResult = Ok(None);

/// `FakePolicyHandler` always returns the provided responses for handling policy/setting requests.
/// The following mappings are Vectors rather than HashMaps as some of the represented values do not
/// implement Hash (such as f32).
#[derive(Clone)]
struct FakePolicyHandlerBuilder {
    policy_request_mapping: Vec<(policy_base::Request, policy_base::response::Response)>,
    policy_response: policy_base::response::Response,
    setting_request_mapping: Vec<(Request, RequestTransform)>,
    setting_request_transform: Option<RequestTransform>,
    setting_response_mapping: Vec<(SettingResponse, ResponseTransform)>,
    setting_response_transform: Option<ResponseTransform>,
}

impl FakePolicyHandlerBuilder {
    fn new() -> Self {
        Self {
            policy_request_mapping: Vec::new(),
            policy_response: Err(policy_base::response::Error::Unexpected),
            setting_request_mapping: Vec::new(),
            setting_request_transform: None,
            setting_response_mapping: Vec::new(),
            setting_response_transform: None,
        }
    }

    #[allow(dead_code)]
    fn add_policy_request_mapping(
        mut self,
        request: policy_base::Request,
        response: policy_base::response::Response,
    ) -> Self {
        self.policy_request_mapping.retain(|(key, _)| *key != request);
        self.policy_request_mapping.push((request, response));

        self
    }

    fn set_policy_response(mut self, response: policy_base::response::Response) -> Self {
        self.policy_response = response;

        self
    }

    fn add_request_mapping(mut self, request: Request, transform: RequestTransform) -> Self {
        self.setting_request_mapping.retain(|(key, _)| *key != request);
        self.setting_request_mapping.push((request, transform));

        self
    }

    fn set_request_transform(mut self, transform: RequestTransform) -> Self {
        self.setting_request_transform = Some(transform);

        self
    }

    fn add_response_mapping(
        mut self,
        response: SettingResponse,
        transform: ResponseTransform,
    ) -> Self {
        self.setting_response_mapping.retain(|(key, _)| *key != response);
        self.setting_response_mapping.push((response, transform));

        self
    }

    fn build(self) -> FakePolicyHandler {
        FakePolicyHandler {
            policy_request_mapping: self.policy_request_mapping,
            policy_response: self.policy_response,
            setting_request_mapping: self.setting_request_mapping,
            setting_request_transform: self.setting_request_transform,
            setting_response_mapping: self.setting_response_mapping,
            setting_response_transform: self.setting_response_transform,
        }
    }
}

/// `FakePolicyHandler` always returns the provided responses for handling policy/setting requests.
#[derive(Clone)]
struct FakePolicyHandler {
    policy_request_mapping: Vec<(policy_base::Request, policy_base::response::Response)>,
    policy_response: policy_base::response::Response,
    setting_request_mapping: Vec<(Request, RequestTransform)>,
    setting_request_transform: Option<RequestTransform>,
    setting_response_mapping: Vec<(SettingResponse, ResponseTransform)>,
    setting_response_transform: Option<ResponseTransform>,
}

#[async_trait]
impl PolicyHandler for FakePolicyHandler {
    async fn handle_policy_request(
        &mut self,
        request: policy_base::Request,
    ) -> policy_base::response::Response {
        self.policy_request_mapping
            .iter()
            .find(|(key, _)| *key == request)
            .map_or(self.policy_response.clone(), |x| x.1.clone())
    }

    async fn handle_setting_request(&mut self, request: Request) -> Option<RequestTransform> {
        self.setting_request_transform.clone().or_else(|| {
            self.setting_request_mapping
                .iter()
                .find(|(key, _)| *key == request)
                .map(|x| x.1.clone())
        })
    }

    async fn handle_setting_response(
        &mut self,
        response: SettingResponse,
    ) -> Option<ResponseTransform> {
        self.setting_response_transform.clone().or_else(|| {
            self.setting_response_mapping
                .iter()
                .find(|(key, _)| *key == response)
                .map(|x| x.1.clone())
        })
    }
}

/// Creates a handler factory with the given `FakePolicyHandler`.
fn create_handler_factory(
    storage_factory: InMemoryStorageFactory,
    policy_handler: FakePolicyHandler,
) -> Arc<Mutex<dyn PolicyHandlerFactory + Send + Sync>> {
    let mut handler_factory = PolicyHandlerFactoryImpl::new(
        [POLICY_TYPE].iter().copied().collect(),
        [SETTING_TYPE].iter().copied().collect(),
        Arc::new(storage_factory),
        Arc::new(AtomicU64::new(1)),
    );
    handler_factory.register(
        POLICY_TYPE,
        Box::new(move |_| {
            let handler_clone = policy_handler.clone();
            Box::pin(async move { Ok(Box::new(handler_clone) as BoxedHandler) })
        }),
    );
    Arc::new(Mutex::new(handler_factory))
}

// Simple test that verifies the constructor succeeds.
#[fuchsia_async::run_until_stalled(test)]
async fn test_policy_proxy_creation() {
    // Create the policy proxy.
    let policy_proxy_result = PolicyProxy::create(
        POLICY_TYPE,
        create_handler_factory(
            InMemoryStorageFactory::new(),
            FakePolicyHandlerBuilder::new()
                .set_policy_response(Ok(policy_base::response::Payload::PolicyInfo(
                    UnknownInfo(true).into(),
                )))
                .build(),
        ),
        service::MessageHub::create_hub(),
    )
    .await;

    // Creation is successful.
    assert!(policy_proxy_result.is_ok());
}

// Verify that policy messages sent to the proxy are passed to the handler, then that the handler's
// response is returned via the proxy.
#[fuchsia_async::run_until_stalled(test)]
async fn test_policy_messages_passed_to_handler() {
    let policy_request = policy_base::Request::Get;
    let policy_payload = policy_base::response::Payload::PolicyInfo(UnknownInfo(true).into());

    let service_delegate = service::MessageHub::create_hub();
    // Initialize the policy proxy and a messenger to communicate with it.
    let _ = PolicyProxy::create(
        POLICY_TYPE,
        create_handler_factory(
            InMemoryStorageFactory::new(),
            FakePolicyHandlerBuilder::new().set_policy_response(Ok(policy_payload.clone())).build(),
        ),
        service_delegate.clone(),
    )
    .await;

    let (policy_messenger, _) =
        service_delegate.create(MessengerType::Unbound).await.expect("policy messenger created");

    // Send a policy request to the policy proxy.
    let mut policy_send_receptor = policy_messenger
        .message(
            policy_base::Payload::Request(policy_request).into(),
            Audience::Address(service::Address::PolicyHandler(POLICY_TYPE)),
        )
        .send();

    // Wait for a response.
    let (policy_response, _) = policy_send_receptor
        .next_of::<policy_base::Payload>()
        .await
        .expect("policy response received");

    // Policy handler returned its response through the policy proxy, back to the client.
    assert_eq!(policy_response, policy_base::Payload::Response(Ok(policy_payload)));
}

// Verify that when the policy handler doesn't take any action on a setting request, it will
// continue on to its intended destination without interference.
#[fuchsia_async::run_until_stalled(test)]
async fn test_setting_message_pass_through() {
    let delegate = service::MessageHub::create_hub();
    let (_, mut setting_proxy_receptor) = delegate
        .create(MessengerType::Addressable(service::Address::Handler(SETTING_TYPE)))
        .await
        .expect("setting_proxy created");

    let _ = PolicyProxy::create(
        POLICY_TYPE,
        create_handler_factory(
            InMemoryStorageFactory::new(),
            FakePolicyHandlerBuilder::new().build(),
        ),
        delegate.clone(),
    )
    .await;

    // Create a messenger that represents the setting caller.
    let (messenger, _) = delegate.create(MessengerType::Unbound).await.expect("messenger created");

    // Send a setting request to the setting handler.
    let mut settings_send_receptor = messenger
        .message(
            SETTING_REQUEST_PAYLOAD.clone().into(),
            service::message::Audience::Address(service::Address::Handler(SETTING_TYPE)),
        )
        .send();

    // Verify the setting handler received the original payload, then reply with a response.
    verify_payload(
        SETTING_REQUEST_PAYLOAD.clone().into(),
        &mut setting_proxy_receptor,
        Some(Box::new(|client| -> BoxFuture<'_, ()> {
            Box::pin(async move {
                let _ = client.reply(SETTING_RESPONSE_PAYLOAD.clone().into()).send();
            })
        })),
    )
    .await;

    // Verify that the requestor receives the response the setting handler sent.
    verify_payload(
        service::Payload::try_from(SETTING_RESPONSE_PAYLOAD.clone())
            .expect("should derive payload"),
        &mut settings_send_receptor,
        None,
    )
    .await;
}

// Verify that when the policy handler returns a result to give directly to the client, that the
// given result is provided back to the requestor without reaching the setting handler.
#[fuchsia_async::run_until_stalled(test)]
async fn test_setting_message_result_replacement() {
    let delegate = service::MessageHub::create_hub();

    let (_, mut setting_proxy_receptor) = delegate
        .create(MessengerType::Addressable(service::Address::Handler(SETTING_TYPE)))
        .await
        .expect("setting_proxy created");

    let _ = PolicyProxy::create(
        POLICY_TYPE,
        create_handler_factory(
            InMemoryStorageFactory::new(),
            FakePolicyHandlerBuilder::new()
                // Include a different response than the original as the
                // transform result, so that the original request is ignored.
                .add_request_mapping(
                    SETTING_REQUEST.clone(),
                    RequestTransform::Result(HANDLER_RESULT_NO_RESPONSE.clone()),
                )
                .build(),
        ),
        delegate.clone(),
    )
    .await;

    // the address of the setting handler
    let setting_handler_address = service::Address::Handler(SETTING_TYPE);

    // Create a messenger that represents an outside caller.
    let (messenger, _) = delegate.create(MessengerType::Unbound).await.expect("messenger created");

    // Send a setting request from the requestor to the setting handler.
    let mut settings_send_receptor = messenger
        .message(
            SETTING_REQUEST_PAYLOAD.clone().into(),
            service::message::Audience::Address(setting_handler_address),
        )
        .send();

    // We want to verify that the setting handler didn't receive the original message from the
    // client. To do this, we create a new messenger and send a message to the setting handler
    // and wait for it to be received. Since messages are delivered in-order, if the setting handler
    // received the original message, this will fail.
    let (test_messenger, _) =
        delegate.create(MessengerType::Unbound).await.expect("messenger created");

    let _ = test_messenger
        .message(
            SETTING_REQUEST_PAYLOAD_2.clone().into(),
            service::message::Audience::Address(setting_handler_address),
        )
        .send();

    verify_payload(
        service::Payload::try_from(SETTING_REQUEST_PAYLOAD_2.clone())
            .expect("should derive payload"),
        &mut setting_proxy_receptor,
        None,
    )
    .await;

    // Verify that the client receives the response that the policy handler returned.
    verify_payload(
        service::Payload::try_from(Payload::Response(SETTING_RESULT_NO_RESPONSE.clone()))
            .expect("should derive payload"),
        &mut settings_send_receptor,
        None,
    )
    .await;
}

// Verify that when the policy handler returns a new request payload, that the payload is sent to
// the setting handler in place of the original message.
#[fuchsia_async::run_until_stalled(test)]
async fn test_setting_message_payload_replacement() {
    // Original request that will be sent by the client.
    let setting_request_1 = Request::Get;
    let setting_request_1_payload = Payload::Request(setting_request_1.clone());

    // Modified request that the policy handler will return.
    let setting_request_2 = Request::Restore;
    let setting_request_2_payload = Payload::Request(setting_request_2.clone());

    let delegate = service::MessageHub::create_hub();

    let setting_handler_address = service::Address::Handler(SETTING_TYPE);

    let (_, mut setting_proxy_receptor) = delegate
        .create(MessengerType::Addressable(setting_handler_address))
        .await
        .expect("setting proxy messenger created");

    let _ = PolicyProxy::create(
        POLICY_TYPE,
        create_handler_factory(
            InMemoryStorageFactory::new(),
            // Fake handler will return request 2 to be sent to the setting handler
            FakePolicyHandlerBuilder::new()
                .set_request_transform(RequestTransform::Request(setting_request_2))
                .build(),
        ),
        delegate.clone(),
    )
    .await;

    // Create a messenger that represents the client.
    let (messenger, _) = delegate.create(MessengerType::Unbound).await.expect("messenger created");

    // Send a setting request from the client to the setting handler.
    let mut settings_send_receptor = messenger
        .message(setting_request_1_payload.into(), Audience::Address(setting_handler_address))
        .send();

    // Verify the setting handler receives the payload that the policy handler specifies, not the
    // original sent by the client.
    verify_payload(
        setting_request_2_payload.into(),
        &mut setting_proxy_receptor,
        Some(Box::new(|client| -> BoxFuture<'_, ()> {
            Box::pin(async move {
                let _ = client.reply(SETTING_RESPONSE_PAYLOAD.clone().into()).send();
            })
        })),
    )
    .await;

    // Verify that the requestor receives the response that the setting handler returned.
    verify_payload(SETTING_RESPONSE_PAYLOAD.clone().into(), &mut settings_send_receptor, None)
        .await;
}

// Verify that when the policy handler doesn't take any action on a setting event, it will
// continue on to its intended destination without interference.
#[fuchsia_async::run_until_stalled(test)]
async fn test_setting_response_pass_through() {
    let delegate = service::MessageHub::create_hub();

    let (setting_proxy_messenger, _) = delegate
        .create(MessengerType::Addressable(service::Address::Handler(SETTING_TYPE)))
        .await
        .expect("setting proxy messenger created");

    let _ = PolicyProxy::create(
        POLICY_TYPE,
        create_handler_factory(
            InMemoryStorageFactory::new(),
            FakePolicyHandlerBuilder::new().build(),
        ),
        delegate.clone(),
    )
    .await;

    // Create a messenger that represents the client.
    let (_, mut receptor) =
        delegate.create(MessengerType::Unbound).await.expect("messenger created");

    // Send a setting event from the setting proxy to the client.
    let _ = setting_proxy_messenger
        .message(
            SETTING_RESPONSE_PAYLOAD.clone().into(),
            Audience::Messenger(receptor.get_signature()),
        )
        .send();

    // Verify the client receives the event.
    verify_payload(SETTING_RESPONSE_PAYLOAD.clone().into(), &mut receptor, None).await;
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_setting_response_replace() {
    let delegate = service::MessageHub::create_hub();
    let (setting_proxy_messenger, _) = delegate
        .create(MessengerType::Addressable(service::Address::Handler(SETTING_TYPE)))
        .await
        .expect("setting proxy messenger created");

    let _ = PolicyProxy::create(
        POLICY_TYPE,
        create_handler_factory(
            InMemoryStorageFactory::new(),
            FakePolicyHandlerBuilder::new()
                .add_response_mapping(
                    SETTING_RESPONSE.clone(),
                    ResponseTransform::Response(SETTING_RESPONSE_MODIFIED.clone()),
                )
                .build(),
        ),
        delegate.clone(),
    )
    .await;

    // Create a messenger that represents the client.
    let (_, mut receptor) =
        delegate.create(MessengerType::Unbound).await.expect("messenger created");

    // Send a setting request from the setting proxy to the client.
    let _ = setting_proxy_messenger
        .message(
            SETTING_RESPONSE_PAYLOAD.clone().into(),
            Audience::Messenger(receptor.get_signature()),
        )
        .send();

    // Verify the client receives the event.
    verify_payload(
        Payload::Response(SETTING_RESPONSE_MODIFIED.clone()).into(),
        &mut receptor,
        None,
    )
    .await;
}

// Exercises the main loop in the policy proxy by sending a series of messages
// and ensuring they're all answered.
#[fuchsia_async::run_until_stalled(test)]
async fn test_multiple_messages() {
    let policy_request = policy_base::Request::Get;
    let policy_payload = policy_base::response::Payload::PolicyInfo(UnknownInfo(true).into());

    let delegate = service::MessageHub::create_hub();

    let (_, _setting_proxy_receptor) = delegate
        .create(MessengerType::Addressable(service::Address::Handler(SETTING_TYPE)))
        .await
        .expect("setting proxy messenger created");

    // Initialize the policy proxy and a messenger to communicate with it.
    let _ = PolicyProxy::create(
        POLICY_TYPE,
        create_handler_factory(
            InMemoryStorageFactory::new(),
            FakePolicyHandlerBuilder::new()
                .set_policy_response(Ok(policy_payload.clone()))
                .add_request_mapping(
                    SETTING_REQUEST.clone(),
                    RequestTransform::Result(HANDLER_RESULT_NO_RESPONSE.clone()),
                )
                .build(),
        ),
        delegate.clone(),
    )
    .await;

    // Create a messenger for sending messages directly to the policy proxy.
    let (policy_messenger, _) =
        delegate.create(MessengerType::Unbound).await.expect("policy messenger created");

    // Create a messenger that represents the client.
    let (messenger, _) = delegate.create(MessengerType::Unbound).await.expect("messenger created");

    // Send a few requests to the policy proxy.
    for _ in 0..3 {
        // Send a policy request.
        let mut policy_send_receptor = policy_messenger
            .message(
                policy_base::Payload::Request(policy_request.clone()).into(),
                Audience::Address(service::Address::PolicyHandler(POLICY_TYPE)),
            )
            .send();

        // Verify a policy response is returned each time.
        verify_payload(
            policy_base::Payload::Response(Ok(policy_payload.clone())).into(),
            &mut policy_send_receptor,
            None,
        )
        .await;

        // Send a request that the policy proxy intercepts.
        let mut settings_send_receptor = messenger
            .message(
                SETTING_REQUEST_PAYLOAD.clone().into(),
                Audience::Address(service::Address::Handler(SETTING_TYPE)),
            )
            .send();

        // Verify that the client receives the response that the policy handler returns
        // each time.
        verify_payload(
            Payload::Response(SETTING_RESULT_NO_RESPONSE.clone()).into(),
            &mut settings_send_receptor,
            None,
        )
        .await;
    }
}
