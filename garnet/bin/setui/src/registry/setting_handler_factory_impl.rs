// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::registry::base::{
    Context, Environment, GenerateHandler, SettingHandler, SettingHandlerFactory,
};
use crate::registry::device_storage::DeviceStorageFactory;
use crate::service_context::ServiceContextHandle;
use crate::switchboard::base::SettingType;
use futures::lock::Mutex;
use std::collections::HashMap;
use std::collections::HashSet;
use std::sync::Arc;

/// SettingHandlerFactoryImpl houses registered closures for generating setting
/// handlers.
pub struct SettingHandlerFactoryImpl<T: DeviceStorageFactory> {
    environment: Environment<T>,
    generators: HashMap<SettingType, GenerateHandler<T>>,
}

impl<T: DeviceStorageFactory> SettingHandlerFactory for SettingHandlerFactoryImpl<T> {
    fn generate(&mut self, setting_type: SettingType) -> Option<SettingHandler> {
        if !self.environment.settings.contains(&setting_type) {
            return None;
        }

        if let Some(generate_function) = self.generators.get(&setting_type) {
            return Some((generate_function)(&Context::new(self.environment.clone())));
        }

        None
    }
}

impl<T: DeviceStorageFactory> SettingHandlerFactoryImpl<T> {
    pub fn new(
        settings: HashSet<SettingType>,
        service_context_handle: ServiceContextHandle,
        storage_factory_handle: Arc<Mutex<T>>,
    ) -> SettingHandlerFactoryImpl<T> {
        SettingHandlerFactoryImpl {
            environment: Environment::new(settings, service_context_handle, storage_factory_handle),
            generators: HashMap::new(),
        }
    }

    pub fn register(&mut self, setting_type: SettingType, generate_function: GenerateHandler<T>) {
        self.generators.insert(setting_type, generate_function);
    }
}
