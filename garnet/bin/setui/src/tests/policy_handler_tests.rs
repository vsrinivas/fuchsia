// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use crate::audio::policy as audio;
use crate::audio::policy::PolicyId;
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::handler::device_storage::DeviceStorageFactory;
use crate::handler::setting_handler::persist::Storage;
use crate::internal::core;
use crate::internal::core::message::Messenger;
use crate::message::base::MessengerType;
use crate::policy::base::response::{Payload, Response};
use crate::policy::base::Request;
use crate::policy::policy_handler::{ClientProxy, Create, PolicyHandler, Transform};
use crate::switchboard::base::{PrivacyInfo, SettingRequest, SettingType};
use anyhow::Error;
use async_trait::async_trait;
use futures::future::BoxFuture;

const CONTEXT_ID: u64 = 0;

pub type HandlePolicyRequestCallback<S> =
    Box<dyn Fn(Request, ClientProxy<S>) -> BoxFuture<'static, Response> + Send + Sync>;

pub struct FakePolicyHandler<S: Storage + 'static> {
    client_proxy: ClientProxy<S>,
    handle_policy_request_callback: Option<HandlePolicyRequestCallback<S>>,
}

impl<S: Storage> FakePolicyHandler<S> {
    fn set_handle_policy_request_callback(
        &mut self,
        handle_policy_request_callback: HandlePolicyRequestCallback<S>,
    ) {
        self.handle_policy_request_callback = Some(handle_policy_request_callback);
    }
}

#[async_trait]
impl<S: Storage> Create<S> for FakePolicyHandler<S> {
    async fn create(client_proxy: ClientProxy<S>) -> Result<Self, Error> {
        Ok(Self { client_proxy, handle_policy_request_callback: None })
    }
}

#[async_trait]
impl<S: Storage> PolicyHandler for FakePolicyHandler<S> {
    async fn handle_policy_request(&mut self, request: Request) -> Response {
        self.handle_policy_request_callback.as_ref().unwrap()(request, self.client_proxy.clone())
            .await
    }

    async fn handle_setting_request(
        &mut self,
        _request: SettingRequest,
        _messenger: Messenger,
    ) -> Option<Transform> {
        None
    }
}

/// Verifies that policy handlers are able to write to storage through their client proxy.
#[fuchsia_async::run_until_stalled(test)]
async fn test_write() {
    let expected_value = PrivacyInfo { user_data_sharing_consent: Some(true) };
    let core_messenger_factory = core::message::create_hub();
    let (core_messenger, _) = core_messenger_factory.create(MessengerType::Unbound).await.unwrap();
    let storage_factory = InMemoryStorageFactory::create();
    let store = storage_factory.lock().await.get_store::<PrivacyInfo>(CONTEXT_ID);
    let client_proxy = ClientProxy::new(core_messenger, store.clone(), SettingType::Audio);

    // Create a handler that writes a value through the client proxy when handle_policy_request is
    // called.
    let mut handler =
        FakePolicyHandler::create(client_proxy.clone()).await.expect("failed to create handler");
    handler.set_handle_policy_request_callback(Box::new(move |_, client_proxy| {
        Box::pin(async move {
            client_proxy.write(expected_value.clone(), false).await.expect("write failed");
            Ok(Payload::Audio(audio::Response::Policy(PolicyId::create(0))))
        })
    }));

    // Call handle_policy_request.
    handler
        .handle_policy_request(Request::Audio(audio::Request::Get))
        .await
        .expect("handle failed");

    // Verify the value was written to the store through the client proxy.
    assert_eq!(store.lock().await.get().await, expected_value);

    // Verify that the written value can be read again through the client proxy.
    assert_eq!(client_proxy.read().await, expected_value);
}
