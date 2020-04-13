// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::internal::handler::{MessengerClient, MessengerFactory, Receptor};
use crate::registry::device_storage::DeviceStorageFactory;
use crate::service_context::ServiceContextHandle;
use crate::switchboard::base::{SettingRequest, SettingType};
use anyhow::Error;
use async_trait::async_trait;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use std::collections::HashSet;
use std::sync::Arc;

pub type HandlerId = usize;
pub type SettingHandlerResult = Result<(), Error>;

pub type GenerateHandler<T> =
    Box<dyn Fn(Context<T>) -> BoxFuture<'static, SettingHandlerResult> + Send + Sync>;

/// An command represents messaging from the registry to take a
/// particular action.
#[derive(Debug, Clone)]
pub enum Command {
    HandleRequest(SettingRequest),
    ChangeState(State),
}

#[derive(Debug, Clone, PartialEq)]
pub enum State {
    Listen,
    EndListen,
}

/// A factory capable of creating a handler for a given setting on-demand. If no
/// viable handler can be created, None will be returned.
#[async_trait]
pub trait SettingHandlerFactory {
    async fn generate(
        &mut self,
        setting_type: SettingType,
        messenger_factory: MessengerFactory,
    ) -> Option<HandlerId>;
}

pub struct Environment<T: DeviceStorageFactory> {
    pub settings: HashSet<SettingType>,
    pub service_context_handle: ServiceContextHandle,
    pub storage_factory_handle: Arc<Mutex<T>>,
}

impl<T: DeviceStorageFactory> Clone for Environment<T> {
    fn clone(&self) -> Environment<T> {
        Environment::new(
            self.settings.clone(),
            self.service_context_handle.clone(),
            self.storage_factory_handle.clone(),
        )
    }
}

impl<T: DeviceStorageFactory> Environment<T> {
    pub fn new(
        settings: HashSet<SettingType>,
        service_context_handle: ServiceContextHandle,
        storage_factory_handle: Arc<Mutex<T>>,
    ) -> Environment<T> {
        return Environment {
            settings: settings,
            service_context_handle: service_context_handle,
            storage_factory_handle: storage_factory_handle,
        };
    }
}

/// Context captures all details necessary for a handler to execute in a given
/// settings service environment.
pub struct Context<T: DeviceStorageFactory> {
    pub setting_type: SettingType,
    pub messenger: MessengerClient,
    pub receptor: Receptor,
    pub environment: Environment<T>,
}

impl<T: DeviceStorageFactory> Context<T> {
    pub fn new(
        setting_type: SettingType,
        messenger: MessengerClient,
        receptor: Receptor,
        environment: Environment<T>,
    ) -> Context<T> {
        return Context {
            setting_type: setting_type,
            messenger: messenger,
            receptor: receptor,
            environment: environment.clone(),
        };
    }

    pub fn clone(&self) -> Self {
        Self::new(
            self.setting_type.clone(),
            self.messenger.clone(),
            self.receptor.clone(),
            self.environment.clone(),
        )
    }
}
