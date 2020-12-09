// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::handler::base::SettingHandlerResult;
use crate::handler::device_storage::DeviceStorage;
use crate::handler::setting_handler::persist::Storage;
use crate::handler::setting_handler::StorageFactory;
use crate::internal::core;
use crate::policy::base::response::{Error as PolicyError, Response};
use crate::policy::base::{BoxedHandler, Context, GenerateHandlerResult, Request};
use crate::switchboard::base::{SettingRequest, SettingType};
use anyhow::Error;
use async_trait::async_trait;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use std::sync::Arc;

/// PolicyHandlers are in charge of applying and persisting policies set by clients.
#[async_trait]
pub trait PolicyHandler {
    /// Called when a policy client makes a request on the policy API this handler controls.
    async fn handle_policy_request(&mut self, request: Request) -> Response;

    /// Called when a setting request is intercepted for the setting this policy handler supervises.
    ///
    /// If there are no policies or the request does not need to be modified, `None` should be
    /// returned.
    ///
    /// If this handler wants to consume the request and respond to the client directly, it should
    /// return [`Transform::Result`].
    ///
    /// If this handler wants to modify the request, then let the setting handler handle it,
    /// [`Transform::Request`] should be returned, with the modified request.
    ///
    /// [`Transform::Result`]: enum.Transform.html
    /// [`Transform::Request`]: enum.Transform.html
    async fn handle_setting_request(
        &mut self,
        request: SettingRequest,
        messenger: core::message::Messenger,
    ) -> Option<Transform>;
}

/// `Transform` is returned by a [`PolicyHandler`] in response to a setting request that a
/// [`PolicyProxy`] intercepted. The presence of this value indicates that the policy handler has
/// decided to take action in order to apply policies.
///
/// [`PolicyHandler`]: trait.PolicyHandler.html
/// [`PolicyProxy`]: ../policy_proxy/struct.PolicyProxy.html
///
// TODO(fxbug.dev/59747): remove when used
#[allow(dead_code)]
#[derive(Clone, Debug, PartialEq)]
pub enum Transform {
    /// A new, modified request that should be forwarded to the setting handler for processing.
    Request(SettingRequest),

    /// A result to return directly to the settings client.
    Result(SettingHandlerResult),
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
        let setting_type = context.setting_type;

        let proxy = ClientProxy::<S>::new(context.messenger.clone(), storage, setting_type);
        let handler_result = C::create(proxy).await;

        match handler_result {
            Err(err) => Err(err),
            Ok(handler) => Ok(Box::new(handler) as BoxedHandler),
        }
    })
}

/// `ClientProxy` provides common functionality, like messaging and persistence to policy handlers.
#[derive(Clone)]
pub struct ClientProxy<S: Storage + 'static> {
    messenger: core::message::Messenger,
    storage: Arc<Mutex<DeviceStorage<S>>>,
    setting_type: SettingType,
}

impl<S: Storage + 'static> ClientProxy<S> {
    pub fn new(
        messenger: core::message::Messenger,
        storage: Arc<Mutex<DeviceStorage<S>>>,
        setting_type: SettingType,
    ) -> Self {
        Self { messenger, storage, setting_type }
    }

    pub fn setting_type(&self) -> SettingType {
        self.setting_type
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
            Err(_) => Err(PolicyError::WriteFailure(self.setting_type)),
        }
    }
}
