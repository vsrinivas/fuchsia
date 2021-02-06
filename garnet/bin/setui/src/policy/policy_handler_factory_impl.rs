// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingType;
use crate::handler::device_storage::DeviceStorageFactory;
use crate::internal::core::message;
use crate::policy::base::{BoxedHandler, Context, PolicyHandlerFactoryError, PolicyType};
use crate::policy::base::{GenerateHandler, PolicyHandlerFactory};
use crate::service;
use async_trait::async_trait;
use futures::lock::Mutex;
use std::collections::HashMap;
use std::collections::HashSet;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;

/// PolicyHandlerFactoryImpl houses registered closures for generating setting
/// handlers.
pub struct PolicyHandlerFactoryImpl<T: DeviceStorageFactory + Send + Sync> {
    policies: HashSet<PolicyType>,
    settings: HashSet<SettingType>,
    storage_factory: Arc<Mutex<T>>,
    generators: HashMap<PolicyType, GenerateHandler<T>>,

    /// Atomic counter used to generate new IDs, which uniquely identify a context.
    context_id_counter: Arc<AtomicU64>,
}

#[async_trait]
impl<T: DeviceStorageFactory + Send + Sync> PolicyHandlerFactory for PolicyHandlerFactoryImpl<T> {
    async fn generate(
        &mut self,
        policy_type: PolicyType,
        service_messenger: service::message::Messenger,
        messenger: message::Messenger,
        setting_proxy_signature: message::Signature,
    ) -> Result<BoxedHandler, PolicyHandlerFactoryError> {
        let setting_type = policy_type.setting_type();
        if !self.policies.contains(&policy_type) {
            return Err(PolicyHandlerFactoryError::PolicyNotFound(policy_type));
        }

        if !self.settings.contains(&setting_type) {
            return Err(PolicyHandlerFactoryError::SettingNotFound(setting_type, policy_type));
        }

        let generate_function = self
            .generators
            .get(&policy_type)
            .ok_or(PolicyHandlerFactoryError::GeneratorNotFound(policy_type))?;

        let context = Context {
            policy_type,
            service_messenger,
            messenger,
            setting_proxy_signature,
            storage_factory_handle: self.storage_factory.clone(),
            id: self.context_id_counter.fetch_add(1, Ordering::Relaxed),
        };

        let handler = (generate_function)(context)
            .await
            .map_err(|_| PolicyHandlerFactoryError::HandlerStartupError(policy_type))?;

        return Ok(handler);
    }
}

impl<T: DeviceStorageFactory + Send + Sync> PolicyHandlerFactoryImpl<T> {
    pub fn new(
        policies: HashSet<PolicyType>,
        settings: HashSet<SettingType>,
        storage_factory_handle: Arc<Mutex<T>>,
        context_id_counter: Arc<AtomicU64>,
    ) -> PolicyHandlerFactoryImpl<T> {
        PolicyHandlerFactoryImpl {
            policies,
            settings,
            storage_factory: storage_factory_handle,
            generators: HashMap::new(),
            context_id_counter,
        }
    }

    pub fn register(&mut self, policy_type: PolicyType, generate_function: GenerateHandler<T>) {
        self.generators.insert(policy_type, generate_function);
    }
}
