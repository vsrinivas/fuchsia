// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::handler::device_storage::DeviceStorageFactory;
use crate::internal::core::message;
use crate::message::base::MessengerType;
use crate::policy::base::{BoxedHandler, Context, PolicyHandlerFactoryError};
use crate::policy::base::{GenerateHandler, PolicyHandlerFactory};
use crate::switchboard::base::SettingType;
use async_trait::async_trait;
use futures::lock::Mutex;
use std::collections::HashMap;
use std::collections::HashSet;
use std::sync::Arc;

/// PolicyHandlerFactoryImpl houses registered closures for generating setting
/// handlers.
pub struct PolicyHandlerFactoryImpl<T: DeviceStorageFactory + Send + Sync> {
    settings: HashSet<SettingType>,
    storage_factory: Arc<Mutex<T>>,
    generators: HashMap<SettingType, GenerateHandler<T>>,

    /// Used to uniquely identify a context.
    // TODO(fxbug.dev/65736): use IDs from the same source as setting handlers.
    next_context_id: u64,
}

#[async_trait]
impl<T: DeviceStorageFactory + Send + Sync> PolicyHandlerFactory for PolicyHandlerFactoryImpl<T> {
    async fn generate(
        &mut self,
        setting_type: SettingType,
        messenger_factory: message::Factory,
    ) -> Result<BoxedHandler, PolicyHandlerFactoryError> {
        if !self.settings.contains(&setting_type) {
            return Err(PolicyHandlerFactoryError::SettingNotFound(setting_type));
        }

        let (messenger, _) = messenger_factory
            .create(MessengerType::Unbound)
            .await
            .map_err(|_| PolicyHandlerFactoryError::HandlerMessengerError)?;

        let generate_function = self
            .generators
            .get(&setting_type)
            .ok_or(PolicyHandlerFactoryError::GeneratorNotFound(setting_type))?;

        let context = Context {
            setting_type,
            messenger: messenger.clone(),
            storage_factory_handle: self.storage_factory.clone(),
            id: self.next_context_id,
        };

        let handler = (generate_function)(context)
            .await
            .map_err(|_| PolicyHandlerFactoryError::HandlerStartupError(setting_type))?;

        self.next_context_id += 1;

        return Ok(handler);
    }
}

impl<T: DeviceStorageFactory + Send + Sync> PolicyHandlerFactoryImpl<T> {
    pub fn new(
        settings: HashSet<SettingType>,
        storage_factory_handle: Arc<Mutex<T>>,
    ) -> PolicyHandlerFactoryImpl<T> {
        PolicyHandlerFactoryImpl {
            settings,
            storage_factory: storage_factory_handle,
            generators: HashMap::new(),
            next_context_id: 1,
        }
    }

    pub fn register(&mut self, setting_type: SettingType, generate_function: GenerateHandler<T>) {
        self.generators.insert(setting_type, generate_function);
    }
}
