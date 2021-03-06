// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::handler::base::{Request, Response as SettingResponse};
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::handler::device_storage::DeviceStorageFactory;
use crate::message::base::MessengerType;
use crate::policy::policy_handler::{
    ClientProxy, Create, PolicyHandler, RequestTransform, ResponseTransform,
};
use crate::policy::response::{Payload, Response};
use crate::policy::{PolicyInfo, PolicyType, Request as PolicyRequest, UnknownInfo};
use crate::privacy::types::PrivacyInfo;
use crate::service;
use anyhow::Error;
use async_trait::async_trait;
use futures::future::BoxFuture;

const CONTEXT_ID: u64 = 0;

pub type HandlePolicyRequestCallback =
    Box<dyn Fn(PolicyRequest, ClientProxy) -> BoxFuture<'static, Response> + Send + Sync>;

pub struct FakePolicyHandler {
    client_proxy: ClientProxy,
    handle_policy_request_callback: Option<HandlePolicyRequestCallback>,
}

impl FakePolicyHandler {
    fn set_handle_policy_request_callback(
        &mut self,
        handle_policy_request_callback: HandlePolicyRequestCallback,
    ) {
        self.handle_policy_request_callback = Some(handle_policy_request_callback);
    }
}

#[async_trait]
impl Create for FakePolicyHandler {
    async fn create(client_proxy: ClientProxy) -> Result<Self, Error> {
        Ok(Self { client_proxy, handle_policy_request_callback: None })
    }
}

#[async_trait]
impl PolicyHandler for FakePolicyHandler {
    async fn handle_policy_request(&mut self, request: PolicyRequest) -> Response {
        self.handle_policy_request_callback.as_ref().unwrap()(request, self.client_proxy.clone())
            .await
    }

    async fn handle_setting_request(&mut self, _request: Request) -> Option<RequestTransform> {
        None
    }

    async fn handle_setting_response(
        &mut self,
        _response: SettingResponse,
    ) -> Option<ResponseTransform> {
        None
    }
}

/// Verifies that policy handlers are able to write to storage through their client proxy.
#[fuchsia_async::run_until_stalled(test)]
async fn test_write() {
    let expected_value = PrivacyInfo { user_data_sharing_consent: Some(true) };

    let messenger_factory = service::message::create_hub();
    let (messenger, _) = messenger_factory.create(MessengerType::Unbound).await.unwrap();

    let storage_factory = InMemoryStorageFactory::new();
    storage_factory.initialize_storage::<PrivacyInfo>().await;
    let store = storage_factory.get_store(CONTEXT_ID).await;
    let client_proxy = ClientProxy::new(messenger, store.clone(), PolicyType::Unknown);

    // Create a handler that writes a value through the client proxy when handle_policy_request is
    // called.
    let mut handler =
        FakePolicyHandler::create(client_proxy.clone()).await.expect("failed to create handler");
    handler.set_handle_policy_request_callback(Box::new(move |_, client_proxy| {
        Box::pin(async move {
            client_proxy.write(&expected_value, false).await.expect("write failed");
            Ok(Payload::PolicyInfo(PolicyInfo::Unknown(UnknownInfo(true))))
        })
    }));

    // Call handle_policy_request.
    handler.handle_policy_request(PolicyRequest::Get).await.expect("handle failed");

    // Verify the value was written to the store through the client proxy.
    assert_eq!(store.get::<PrivacyInfo>().await, expected_value);

    // Verify that the written value can be read again through the client proxy.
    assert_eq!(client_proxy.read::<PrivacyInfo>().await, expected_value);
}
