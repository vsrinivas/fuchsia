// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::registry::device_storage::DeviceStorageFactory;
use crate::service_context::ServiceContextHandle;
use crate::switchboard::base::{SettingRequest, SettingRequestResponder, SettingType};
use anyhow::Error;
use async_trait::async_trait;
use futures::channel::mpsc::UnboundedSender;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use std::collections::HashSet;
use std::sync::Arc;

pub type Notifier = UnboundedSender<SettingType>;
pub type SettingHandler = UnboundedSender<Command>;
pub type SettingHandlerResult = Result<SettingHandler, Error>;

pub type GenerateHandler<T> =
    Box<dyn Fn(Context<T>) -> BoxFuture<'static, SettingHandlerResult> + Send + Sync>;

/// An command represents messaging from the registry to take a
/// particular action.
pub enum Command {
    ChangeState(State),
    HandleRequest(SettingRequest, SettingRequestResponder),
}

/// A given state the registry entity can move into.
pub enum State {
    Listen(Notifier),
    EndListen,
}

/// A factory capable of creating a handler for a given setting on-demand. If no
/// viable handler can be created, None will be returned.
#[async_trait]
pub trait SettingHandlerFactory {
    async fn generate(&mut self, setting_type: SettingType) -> Option<SettingHandler>;
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
    pub environment: Environment<T>,
}

impl<T: DeviceStorageFactory> Context<T> {
    pub fn new(setting_type: SettingType, environment: Environment<T>) -> Context<T> {
        return Context { setting_type: setting_type, environment: environment.clone() };
    }

    pub fn clone(&self) -> Self {
        Self::new(self.setting_type.clone(), self.environment.clone())
    }
}
