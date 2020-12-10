// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::handler::base::{
    Command, Context, Environment, GenerateHandler, SettingHandlerFactory,
    SettingHandlerFactoryError, State,
};
use crate::handler::device_storage::DeviceStorageFactory;
use crate::internal::handler::{message, Payload};
use crate::message::base::{Audience, MessageEvent, MessengerType, Status};
use crate::service_context::ServiceContextHandle;
use crate::switchboard::base::SettingType;
use async_trait::async_trait;
use futures::lock::Mutex;
use futures::StreamExt;
use std::collections::HashMap;
use std::collections::HashSet;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;

/// SettingHandlerFactoryImpl houses registered closures for generating setting
/// handlers.
pub struct SettingHandlerFactoryImpl<T: DeviceStorageFactory + Send + Sync> {
    environment: Environment<T>,
    generators: HashMap<SettingType, GenerateHandler<T>>,

    /// Atomic counter used to generate new IDs, which uniquely identify a context.
    context_id_counter: Arc<AtomicU64>,
}

#[async_trait]
impl<T: DeviceStorageFactory + Send + Sync> SettingHandlerFactory for SettingHandlerFactoryImpl<T> {
    async fn generate(
        &mut self,
        setting_type: SettingType,
        messenger_factory: message::Factory,
        notifier_signature: message::Signature,
    ) -> Result<message::Signature, SettingHandlerFactoryError> {
        if !self.environment.settings.contains(&setting_type) {
            return Err(SettingHandlerFactoryError::SettingNotFound(setting_type));
        }

        let (messenger, receptor) = messenger_factory
            .create(MessengerType::Unbound)
            .await
            .map_err(|_| SettingHandlerFactoryError::HandlerMessengerError)?;
        let signature = receptor.get_signature();

        let generate_function = self
            .generators
            .get(&setting_type)
            .ok_or(SettingHandlerFactoryError::GeneratorNotFound(setting_type))?;

        (generate_function)(Context::new(
            setting_type,
            messenger,
            receptor,
            notifier_signature,
            self.environment.clone(),
            self.context_id_counter.fetch_add(1, Ordering::Relaxed),
        ))
        .await
        .map_err(|_| SettingHandlerFactoryError::HandlerStartupError(setting_type))?;

        let (controller_messenger, _) = messenger_factory
            .create(MessengerType::Unbound)
            .await
            .map_err(|_| SettingHandlerFactoryError::ControllerMessengerError)?;

        // At this point, we know the controller was constructed successfully.
        // Tell the controller to run the Startup phase to initialize its state.
        let mut controller_receptor = controller_messenger
            .message(
                Payload::Command(Command::ChangeState(State::Startup)),
                Audience::Messenger(signature),
            )
            .send();

        // Wait for the startup phase to be over before continuing.
        if let Some(MessageEvent::Status(Status::Received)) = controller_receptor.next().await {
            // Startup phase is complete and had no errors. The proxy can assume it
            // has an active controller with create() and startup() already run on it
            // before handling its request.
            return Ok(signature);
        } else {
            return Err(SettingHandlerFactoryError::HandlerStartupError(setting_type));
        }
    }
}

impl<T: DeviceStorageFactory + Send + Sync> SettingHandlerFactoryImpl<T> {
    pub fn new(
        settings: HashSet<SettingType>,
        service_context_handle: ServiceContextHandle,
        storage_factory_handle: Arc<Mutex<T>>,
        context_id_counter: Arc<AtomicU64>,
    ) -> SettingHandlerFactoryImpl<T> {
        SettingHandlerFactoryImpl {
            environment: Environment::new(settings, service_context_handle, storage_factory_handle),
            generators: HashMap::new(),
            context_id_counter,
        }
    }

    pub fn register(&mut self, setting_type: SettingType, generate_function: GenerateHandler<T>) {
        self.generators.insert(setting_type, generate_function);
    }
}
