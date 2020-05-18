// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::internal::handler::message;
use crate::registry::device_storage::DeviceStorageFactory;
use crate::service_context::{ServiceContext, ServiceContextHandle};
use crate::switchboard::base::{SettingRequest, SettingType};
use anyhow::Error;
use async_trait::async_trait;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use std::collections::HashSet;
use std::sync::Arc;

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
        messenger_factory: message::Factory,
    ) -> Option<message::Signature>;
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
    pub messenger: message::Messenger,
    pub receptor: message::Receptor,
    pub environment: Environment<T>,
}

impl<T: DeviceStorageFactory> Context<T> {
    pub fn new(
        setting_type: SettingType,
        messenger: message::Messenger,
        receptor: message::Receptor,
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

/// ContextBuilder is a convenience builder to facilitate creating a Context
/// (and associated environment).
pub struct ContextBuilder<T: DeviceStorageFactory> {
    setting_type: SettingType,
    storage_factory: Arc<Mutex<T>>,
    settings: HashSet<SettingType>,
    service_context: Option<ServiceContextHandle>,
    messenger: message::Messenger,
    receptor: message::Receptor,
}

impl<T: DeviceStorageFactory> ContextBuilder<T> {
    pub fn new(
        setting_type: SettingType,
        storage_factory: Arc<Mutex<T>>,
        messenger: message::Messenger,
        receptor: message::Receptor,
    ) -> Self {
        Self {
            setting_type: setting_type,
            storage_factory: storage_factory,
            settings: HashSet::new(),
            service_context: None,
            messenger: messenger,
            receptor: receptor,
        }
    }

    // Sets the service context to be used.
    pub fn service_context(mut self, service_context_handle: ServiceContextHandle) -> Self {
        self.service_context = Some(service_context_handle);

        self
    }

    /// Adds the settings to given environment.
    pub fn add_settings(mut self, settings: &[SettingType]) -> Self {
        for setting in settings {
            self.settings.insert(setting.clone());
        }

        self
    }

    /// Generates the Context.
    pub fn build(self) -> Context<T> {
        let service_context = if self.service_context.is_none() {
            ServiceContext::create(None)
        } else {
            self.service_context.unwrap()
        };
        let environment = Environment::new(self.settings, service_context, self.storage_factory);

        Context::new(self.setting_type, self.messenger, self.receptor, environment)
    }
}
