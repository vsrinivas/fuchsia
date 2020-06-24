// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::internal::handler::{message, Payload};
use crate::message::base::{Audience, MessageEvent, MessengerType, Status};
use crate::registry::base::{
    Command, Context, Environment, GenerateHandler, SettingHandlerFactory, State,
};
use crate::registry::device_storage::DeviceStorageFactory;
use crate::service_context::ServiceContextHandle;
use crate::switchboard::base::SettingType;
use async_trait::async_trait;
use futures::lock::Mutex;
use futures::StreamExt;
use std::collections::HashMap;
use std::collections::HashSet;
use std::sync::Arc;

/// SettingHandlerFactoryImpl houses registered closures for generating setting
/// handlers.
pub struct SettingHandlerFactoryImpl<T: DeviceStorageFactory + Send + Sync> {
    environment: Environment<T>,
    generators: HashMap<SettingType, GenerateHandler<T>>,

    /// Used to uniquely identify a context.
    next_context_id: u64,
}

#[async_trait]
impl<T: DeviceStorageFactory + Send + Sync> SettingHandlerFactory for SettingHandlerFactoryImpl<T> {
    async fn generate(
        &mut self,
        setting_type: SettingType,
        messenger_factory: message::Factory,
        controller_messenger_client: message::Messenger,
    ) -> Option<message::Signature> {
        if !self.environment.settings.contains(&setting_type) {
            return None;
        }

        let messenger_result = messenger_factory.create(MessengerType::Unbound).await;

        if messenger_result.is_err() {
            return None;
        }

        let (messenger, receptor) = messenger_result.unwrap();
        let signature = messenger.get_signature();

        if let Some(generate_function) = self.generators.get(&setting_type) {
            if (generate_function)(Context::new(
                setting_type,
                messenger,
                receptor,
                self.environment.clone(),
                self.next_context_id,
            ))
            .await
            .is_ok()
            {
                self.next_context_id += 1;
                // At this point, we know the controller was constructed successfully.
                // Tell the controller to run the Startup phase to initialize its state.
                let mut controller_receptor = controller_messenger_client
                    .message(
                        Payload::Command(Command::ChangeState(State::Startup)),
                        Audience::Messenger(signature.clone()),
                    )
                    .send();

                // Wait for the startup phase to be over before continuing.
                if let Some(MessageEvent::Status(Status::Received)) =
                    controller_receptor.next().await
                {
                    // Startup phase is complete and had no errors. The registry can assume it
                    // has an active controller with create() and startup() already run on it
                    // before handling its request.
                    return Some(signature);
                } else {
                    // Invalid response from Startup phase, don't return a signature.
                    return None;
                }
            }
        }

        None
    }
}

impl<T: DeviceStorageFactory + Send + Sync> SettingHandlerFactoryImpl<T> {
    pub fn new(
        settings: HashSet<SettingType>,
        service_context_handle: ServiceContextHandle,
        storage_factory_handle: Arc<Mutex<T>>,
    ) -> SettingHandlerFactoryImpl<T> {
        SettingHandlerFactoryImpl {
            environment: Environment::new(settings, service_context_handle, storage_factory_handle),
            generators: HashMap::new(),
            next_context_id: 1,
        }
    }

    pub fn register(&mut self, setting_type: SettingType, generate_function: GenerateHandler<T>) {
        self.generators.insert(setting_type, generate_function);
    }
}
