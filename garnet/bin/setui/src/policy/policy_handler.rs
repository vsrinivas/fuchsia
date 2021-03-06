// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingType;
use crate::handler::base::{Payload as HandlerPayload, Request, Response as SettingResponse};
use crate::handler::device_storage::{DeviceStorage, DeviceStorageCompatible};
use crate::handler::setting_handler::{SettingHandlerResult, StorageFactory};
use crate::message::base::Audience;
use crate::policy::response::{Error as PolicyError, Response};
use crate::policy::{
    BoxedHandler, Context, GenerateHandlerResult, PolicyType, Request as PolicyRequest,
};
use crate::service;
use anyhow::Error;
use async_trait::async_trait;
use futures::future::BoxFuture;
use std::sync::Arc;

/// PolicyHandlers are in charge of applying and persisting policies set by clients.
#[async_trait]
pub trait PolicyHandler {
    /// Called when a policy client makes a request on the policy API this handler controls.
    async fn handle_policy_request(&mut self, request: PolicyRequest) -> Response;

    /// Called when a setting request is intercepted for the setting this policy handler supervises.
    ///
    /// If there are no policies or the request does not need to be modified, `None` should be
    /// returned.
    ///
    /// If this handler wants to consume the request and respond to the client directly, it should
    /// return [`RequestTransform::Result`].
    ///
    /// If this handler wants to modify the request, then let the setting handler handle it,
    /// [`RequestTransform::Request`] should be returned, with the modified request.
    ///
    /// [`RequestTransform::Result`]: enum.RequestTransform.html
    /// [`RequestTransform::Request`]: enum.RequestTransform.html
    async fn handle_setting_request(&mut self, request: Request) -> Option<RequestTransform>;

    /// Called when a setting response is intercepted from the setting this policy handler
    /// supervises.
    ///
    /// If there are no policies or the response does not need to be modified, `None` should be
    /// returned.
    ///
    /// If this handler wants to modify the response and still let the original audience handle it,
    /// [`Response`] should be returned, containing the modified response.
    ///
    /// [`Response`]: ResponseTransform::Response
    async fn handle_setting_response(
        &mut self,
        response: SettingResponse,
    ) -> Option<ResponseTransform>;
}

/// `RequestTransform` is returned by a [`PolicyHandler`] in response to a setting request that a
/// [`PolicyProxy`] intercepted. The presence of this value indicates that the policy handler has
/// decided to take action in order to apply policies.
///
/// [`PolicyHandler`]: trait.PolicyHandler.html
/// [`PolicyProxy`]: ../policy_proxy/struct.PolicyProxy.html
///
#[derive(Clone, Debug, PartialEq)]
pub enum RequestTransform {
    /// A new, modified request that should be forwarded to the setting handler for processing.
    Request(Request),

    /// A result to return directly to the settings client.
    Result(SettingHandlerResult),
}

/// `ResponseTransform` is returned by a [`PolicyHandler`] in response to a setting response that a
/// [`PolicyProxy`] intercepted. The presence of this value indicates that the policy handler has
/// decided to take action in order to apply policies.
///
/// [`PolicyHandler`]: trait.PolicyHandler.html
/// [`PolicyProxy`]: ../policy_proxy/struct.PolicyProxy.html
///
#[derive(Clone, Debug, PartialEq)]
pub enum ResponseTransform {
    /// A new, modified response that should be forwarded.
    Response(SettingResponse),
}

/// Trait used to create policy handlers.
#[async_trait]
pub trait Create: Sized {
    async fn create(handler: ClientProxy) -> Result<Self, Error>;
}

/// Creates a [`PolicyHandler`] from the given [`Context`].
///
/// [`PolicyHandler`]: trait.PolicyHandler.html
/// [`Context`]: ../base/struct.Context.html
pub fn create_handler<C, T: StorageFactory + 'static>(
    context: Context<T>,
) -> BoxFuture<'static, GenerateHandlerResult>
where
    C: Create + PolicyHandler + Send + Sync + 'static,
{
    Box::pin(async move {
        let storage = context.storage_factory.get_store(context.id).await;

        let proxy = ClientProxy::new(context.service_messenger, storage, context.policy_type);

        C::create(proxy).await.map(|handler| Box::new(handler) as BoxedHandler)
    })
}

/// `ClientProxy` provides common functionality, like messaging and persistence to policy handlers.
#[derive(Clone)]
pub struct ClientProxy {
    service_messenger: service::message::Messenger,
    storage: Arc<DeviceStorage>,
    policy_type: PolicyType,
}

impl ClientProxy {
    /// Sends a setting request to the underlying setting proxy this policy handler controls.
    pub fn send_setting_request(
        &self,
        setting_type: SettingType,
        request: Request,
    ) -> service::message::Receptor {
        self.service_messenger
            .message(
                HandlerPayload::Request(request).into(),
                Audience::Address(service::Address::Handler(setting_type)),
            )
            .send()
    }

    /// Requests the setting handler to rebroadcast a settings changed event to its listeners.
    pub fn request_rebroadcast(&self, setting_type: SettingType) {
        self.service_messenger
            .message(
                HandlerPayload::Request(Request::Rebroadcast).into(),
                Audience::Address(service::Address::Handler(setting_type)),
            )
            .send();
    }
}

impl ClientProxy {
    pub fn new(
        service_messenger: service::message::Messenger,
        storage: Arc<DeviceStorage>,
        policy_type: PolicyType,
    ) -> Self {
        Self { service_messenger, storage, policy_type }
    }

    pub fn policy_type(&self) -> PolicyType {
        self.policy_type
    }

    pub async fn read<S>(&self) -> S
    where
        S: DeviceStorageCompatible,
    {
        self.storage.get().await
    }

    /// Returns Ok if the value was written, or an Error if the write failed. The argument
    /// `write_through` will block returning until the value has been completely written to
    /// persistent store, rather than any temporary in-memory caching.
    pub async fn write<S>(&self, value: &S, write_through: bool) -> Result<(), PolicyError>
    where
        S: DeviceStorageCompatible,
    {
        if *value == self.read().await {
            return Ok(());
        }

        match self.storage.write(value, write_through).await {
            Ok(_) => Ok(()),
            Err(_) => Err(PolicyError::WriteFailure(self.policy_type)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::base::SettingType;
    use crate::handler::base::{Payload as HandlerPayload, Request};
    use crate::handler::device_storage::testing::InMemoryStorageFactory;
    use crate::handler::device_storage::DeviceStorageFactory;
    use crate::message::base::MessengerType;
    use crate::policy::PolicyType;
    use crate::privacy::types::PrivacyInfo;
    use crate::service;
    use crate::tests::message_utils::verify_payload;

    const CONTEXT_ID: u64 = 0;

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_client_proxy_send_setting_request() {
        let policy_type = PolicyType::Unknown;
        let setting_request = Request::Get;
        let target_setting_type = SettingType::Unknown;

        let service_messenger_factory = service::message::create_hub();
        let (_, mut setting_proxy_receptor) = service_messenger_factory
            .create(MessengerType::Addressable(service::Address::Handler(
                policy_type.setting_type(),
            )))
            .await
            .expect("setting proxy messenger created");
        let storage_factory = InMemoryStorageFactory::new();
        storage_factory.initialize_storage::<PrivacyInfo>().await;
        let storage = storage_factory.get_store(CONTEXT_ID).await;

        let client_proxy = ClientProxy {
            service_messenger: service_messenger_factory
                .create(MessengerType::Unbound)
                .await
                .expect("messenger should be created")
                .0,
            storage,
            policy_type,
        };

        client_proxy.send_setting_request(target_setting_type, setting_request.clone());

        verify_payload(
            service::Payload::Setting(HandlerPayload::Request(setting_request)),
            &mut setting_proxy_receptor,
            None,
        )
        .await
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_client_proxy_request_rebroadcast() {
        let policy_type = PolicyType::Unknown;
        let setting_type = SettingType::Unknown;

        let service_messenger_factory = service::message::create_hub();

        let (_, mut receptor) = service_messenger_factory
            .create(MessengerType::Addressable(service::Address::Handler(setting_type)))
            .await
            .expect("service receptor created");

        let client_proxy = ClientProxy {
            service_messenger: service_messenger_factory
                .create(MessengerType::Unbound)
                .await
                .expect("messenger should be created")
                .0,
            storage: InMemoryStorageFactory::new().get_store(CONTEXT_ID).await,
            policy_type,
        };

        client_proxy.request_rebroadcast(setting_type);

        verify_payload(HandlerPayload::Request(Request::Rebroadcast).into(), &mut receptor, None)
            .await
    }
}
