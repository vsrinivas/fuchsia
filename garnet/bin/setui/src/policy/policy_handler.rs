// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingType;
use crate::handler::base::{Payload as HandlerPayload, Request, Response as SettingResponse};
use crate::handler::device_storage::{DeviceStorage, DeviceStorageCompatible};
use crate::handler::setting_handler::{SettingHandlerResult, StorageFactory};
use crate::internal::core::message::{Audience as CoreAudience, Messenger, Receptor, Signature};
use crate::internal::core::Payload;
use crate::message::base::Audience;
use crate::policy::base::response::{Error as PolicyError, Response};
use crate::policy::base::{
    BoxedHandler, Context, GenerateHandlerResult, PolicyType, Request as PolicyRequest,
};
use crate::service;
use crate::switchboard::base::{SettingAction, SettingActionData, SettingEvent};
use anyhow::Error;
use async_trait::async_trait;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use std::sync::Arc;

pub trait Storage: DeviceStorageCompatible + Send + Sync {}
impl<T: DeviceStorageCompatible + Send + Sync> Storage for T {}

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

    /// Called when a setting event is intercepted from the setting this policy handler supervises.
    ///
    /// If there are no policies or the event does not need to be modified, `None` should be
    /// returned.
    ///
    /// If this handler wants to modify the event and still let the switchboard handle it,
    /// [`EventTransform::Event`] should be returned, containing the modified event.
    ///
    /// [`EventTransform::Event`]: enum.EventTransform.html
    async fn handle_setting_event(&mut self, event: SettingEvent) -> Option<EventTransform>;

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

/// `EventTransform` is returned by a [`PolicyHandler`] in response to a setting event that a
/// [`PolicyProxy`] intercepted. The presence of this value indicates that the policy handler has
/// decided to take action in order to apply policies.
///
/// [`PolicyHandler`]: trait.PolicyHandler.html
/// [`PolicyProxy`]: ../policy_proxy/struct.PolicyProxy.html
///
#[derive(Clone, Debug, PartialEq)]
pub enum EventTransform {
    /// A new, modified event that should be forwarded to the switchboard for processing.
    Event(SettingEvent),
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
pub trait Create<S: Storage>: Sized {
    async fn create(handler: ClientProxy<S>) -> Result<Self, Error>;
}

/// Creates a [`PolicyHandler`] from the given [`Context`].
///
/// [`PolicyHandler`]: trait.PolicyHandler.html
/// [`Context`]: ../base/struct.Context.html
pub fn create_handler<S, C, T: StorageFactory + 'static>(
    context: Context<T>,
) -> BoxFuture<'static, GenerateHandlerResult>
where
    S: Storage + 'static,
    C: Create<S> + PolicyHandler + Send + Sync + 'static,
{
    Box::pin(async move {
        let storage = context.storage_factory_handle.lock().await.get_store::<S>(context.id);

        let proxy = ClientProxy::<S>::new(
            context.service_messenger,
            context.messenger,
            context.setting_proxy_signature,
            storage,
            context.policy_type,
        );

        C::create(proxy).await.map(|handler| Box::new(handler) as BoxedHandler)
    })
}

/// `ClientProxy` provides common functionality, like messaging and persistence to policy handlers.
#[derive(Clone)]
pub struct ClientProxy<S: Storage + 'static> {
    service_messenger: service::message::Messenger,
    messenger: Messenger,
    setting_proxy_signature: Signature,
    storage: Arc<Mutex<DeviceStorage<S>>>,
    policy_type: PolicyType,
}

impl<S: Storage + 'static> ClientProxy<S> {
    /// Sends a setting request to the underlying setting proxy this policy handler controls.
    pub fn send_setting_request(&self, request: Request) -> Receptor {
        self.messenger
            .message(
                Payload::Action(SettingAction {
                    id: 0,
                    setting_type: self.policy_type.setting_type(),
                    data: SettingActionData::Request(request),
                }),
                CoreAudience::Messenger(self.setting_proxy_signature),
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

impl<S: Storage + 'static> ClientProxy<S> {
    pub fn new(
        service_messenger: service::message::Messenger,
        messenger: Messenger,
        setting_proxy_signature: Signature,
        storage: Arc<Mutex<DeviceStorage<S>>>,
        policy_type: PolicyType,
    ) -> Self {
        Self { service_messenger, messenger, setting_proxy_signature, storage, policy_type }
    }

    pub fn policy_type(&self) -> PolicyType {
        self.policy_type
    }

    pub async fn read(&self) -> S {
        self.storage.lock().await.get().await
    }

    /// Returns Ok if the value was written, or an Error if the write failed. The argument
    /// `write_through` will block returning until the value has been completely written to
    /// persistent store, rather than any temporary in-memory caching.
    pub async fn write(&self, value: S, write_through: bool) -> Result<(), PolicyError> {
        if value == self.read().await {
            return Ok(());
        }

        match self.storage.lock().await.write(&value, write_through).await {
            Ok(_) => Ok(()),
            Err(_) => Err(PolicyError::WriteFailure(self.policy_type)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::ClientProxy;
    use crate::base::SettingType;
    use crate::handler::base::{Payload as HandlerPayload, Request};
    use crate::handler::device_storage::testing::InMemoryStorageFactory;
    use crate::handler::device_storage::DeviceStorageFactory;
    use crate::internal::core;
    use crate::internal::core::Payload;
    use crate::message::base::MessengerType;
    use crate::policy::base::PolicyType;
    use crate::privacy::types::PrivacyInfo;
    use crate::service;
    use crate::switchboard::base::{SettingAction, SettingActionData};
    use crate::tests::message_utils::verify_payload;

    const CONTEXT_ID: u64 = 0;

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_client_proxy_send_setting_request() {
        let policy_type = PolicyType::Unknown;
        let setting_request = Request::Get;

        let service_messenger_factory = service::message::create_hub();
        let core_messenger_factory = core::message::create_hub();
        let (messenger, _) = core_messenger_factory
            .create(MessengerType::Unbound)
            .await
            .expect("core messenger created");
        let (_, mut setting_proxy_receptor) = core_messenger_factory
            .create(MessengerType::Unbound)
            .await
            .expect("setting proxy messenger created");
        let storage_factory = InMemoryStorageFactory::create();
        let store = storage_factory.lock().await.get_store::<PrivacyInfo>(CONTEXT_ID);

        let client_proxy = ClientProxy {
            service_messenger: service_messenger_factory
                .create(MessengerType::Unbound)
                .await
                .expect("messenger should be created")
                .0,
            messenger,
            setting_proxy_signature: setting_proxy_receptor.get_signature(),
            storage: store,
            policy_type,
        };

        client_proxy.send_setting_request(setting_request.clone());

        verify_payload(
            Payload::Action(SettingAction {
                id: 0,
                setting_type: policy_type.setting_type(),
                data: SettingActionData::Request(setting_request),
            }),
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
        let core_messenger_factory = core::message::create_hub();

        let (messenger, _) = core_messenger_factory
            .create(MessengerType::Unbound)
            .await
            .expect("core messenger created");
        let (_, setting_proxy_receptor) = core_messenger_factory
            .create(MessengerType::Unbound)
            .await
            .expect("setting proxy messenger created");
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
            messenger,
            setting_proxy_signature: setting_proxy_receptor.get_signature(),
            storage: InMemoryStorageFactory::create()
                .lock()
                .await
                .get_store::<PrivacyInfo>(CONTEXT_ID),
            policy_type,
        };

        client_proxy.request_rebroadcast(setting_type);

        verify_payload(HandlerPayload::Request(Request::Rebroadcast).into(), &mut receptor, None)
            .await
    }
}
