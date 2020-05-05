// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::internal::core::{Address, MessageClient, MessengerClient, MessengerFactory, Payload};
use crate::internal::handler::{
    Address as ControllerAddress, MessengerClient as ControllerMessengerClient,
    MessengerFactory as ControllerMessengerFactory, Payload as ControllerPayload, Signature,
};
use crate::message::base::{Audience, DeliveryStatus, MessageEvent, MessengerType};
use crate::registry::base::{Command, SettingHandlerFactory, State};
use crate::switchboard::base::{
    SettingAction, SettingActionData, SettingEvent, SettingRequest, SettingType, SwitchboardError,
};
use fuchsia_async as fasync;

use anyhow::Error;
use futures::lock::Mutex;
use std::collections::HashMap;
use std::sync::Arc;

pub struct RegistryImpl {
    messenger_client: MessengerClient,
    active_controllers: HashMap<SettingType, Signature>,
    /// The current types being listened in on.
    active_listeners: Vec<SettingType>,
    /// handler factory
    handler_factory: Arc<Mutex<dyn SettingHandlerFactory + Send + Sync>>,
    /// Factory for creating messengers to communicate with handlers
    controller_messenger_factory: ControllerMessengerFactory,
    /// Client for communicating with handlers
    controller_messenger_client: ControllerMessengerClient,
}

impl RegistryImpl {
    /// Returns a handle to a RegistryImpl which is listening to SettingAction from the
    /// provided receiver and will send responses/updates on the given sender.
    pub async fn create(
        handler_factory: Arc<Mutex<dyn SettingHandlerFactory + Send + Sync>>,
        messenger_factory: MessengerFactory,
        controller_messenger_factory: ControllerMessengerFactory,
    ) -> Result<Arc<Mutex<RegistryImpl>>, Error> {
        let messenger_result =
            messenger_factory.create(MessengerType::Addressable(Address::Registry)).await;
        if let Err(error) = messenger_result {
            return Err(Error::new(error));
        }
        let (messenger_client, mut receptor) = messenger_result.unwrap();

        let controller_messenger_result = controller_messenger_factory
            .create(MessengerType::Addressable(ControllerAddress::Registry))
            .await;
        if let Err(error) = controller_messenger_result {
            return Err(Error::new(error));
        }
        let (controller_messenger_client, mut controller_receptor) =
            controller_messenger_result.unwrap();

        // We must create handle here rather than return back the value as we
        // reference the registry in the async tasks below.
        let registry = Arc::new(Mutex::new(Self {
            active_listeners: vec![],
            handler_factory: handler_factory,
            messenger_client: messenger_client,
            active_controllers: HashMap::new(),
            controller_messenger_client: controller_messenger_client,
            controller_messenger_factory: controller_messenger_factory,
        }));

        // Async task for handling top level message from controllers
        {
            let registry = registry.clone();
            fasync::spawn(async move {
                while let Ok(event) = controller_receptor.watch().await {
                    match event {
                        MessageEvent::Message(ControllerPayload::Changed(setting), _) => {
                            registry.lock().await.notify(setting);
                        }
                        _ => {}
                    }
                }
            });
        }

        // Async task for handling messages from the receptor.
        {
            let registry_clone = registry.clone();
            fasync::spawn(async move {
                while let Ok(event) = receptor.watch().await {
                    match event {
                        MessageEvent::Message(Payload::Action(action), client) => {
                            registry_clone.lock().await.process_action(action, client).await;
                        }
                        _ => {}
                    }
                }
            });
        }

        Ok(registry)
    }

    /// Interpret action from switchboard into registry actions.
    async fn process_action(&mut self, action: SettingAction, message_client: MessageClient) {
        match action.data {
            SettingActionData::Request(request) => {
                self.process_request(action.id, action.setting_type, request, message_client).await;
            }
            SettingActionData::Listen(size) => {
                self.process_listen(action.setting_type, size).await;
            }
        }
    }

    async fn get_handler_signature(&mut self, setting_type: SettingType) -> Option<Signature> {
        if !self.active_controllers.contains_key(&setting_type) {
            if let Some(signature) = self
                .handler_factory
                .lock()
                .await
                .generate(setting_type, self.controller_messenger_factory.clone())
                .await
            {
                self.active_controllers.insert(setting_type, signature);
            }
        }

        // HashMap returns a reference, we need a value here.
        if let Some(signature) = self.active_controllers.get(&setting_type) {
            return Some(signature.clone());
        } else {
            return None;
        }
    }

    /// Notifies proper sink in the case the notification listener count is
    /// non-zero and we aren't already listening for changes to the type or there
    /// are no more listeners and we are actively listening.
    async fn process_listen(&mut self, setting_type: SettingType, size: u64) {
        let optional_handler_signature = self.get_handler_signature(setting_type).await;
        if optional_handler_signature.is_none() {
            return;
        }

        let handler_signature = optional_handler_signature.unwrap();

        let listening = self.active_listeners.contains(&setting_type);

        let mut new_state = None;
        if size == 0 && listening {
            // FIXME: use `Vec::remove_item` upon stabilization
            let listener_to_remove =
                self.active_listeners.iter().enumerate().find(|(_i, elem)| **elem == setting_type);
            if let Some((i, _elem)) = listener_to_remove {
                self.active_listeners.remove(i);
            }
            new_state = Some(State::EndListen);
        } else if size > 0 && !listening {
            self.active_listeners.push(setting_type);
            new_state = Some(State::Listen);
        }

        if let Some(state) = new_state {
            self.controller_messenger_client
                .message(
                    ControllerPayload::Command(Command::ChangeState(state)),
                    Audience::Messenger(handler_signature),
                )
                .send()
                .ack();
        }
    }

    /// Called by the receiver task when a sink has reported a change to its
    /// setting type.
    fn notify(&self, setting_type: SettingType) {
        // Only return updates for types actively listened on.
        if self.active_listeners.contains(&setting_type) {
            self.messenger_client
                .message(
                    Payload::Event(SettingEvent::Changed(setting_type)),
                    Audience::Address(Address::Switchboard),
                )
                .send();
        }
    }

    /// Forwards request to proper sink. A new task is spawned in order to receive
    /// the response. If no sink is available, an error is immediately reported
    /// back.
    async fn process_request(
        &mut self,
        id: u64,
        setting_type: SettingType,
        request: SettingRequest,
        client: MessageClient,
    ) {
        match self.get_handler_signature(setting_type).await {
            None => {
                client
                    .reply(Payload::Event(SettingEvent::Response(
                        id,
                        Err(SwitchboardError::UnhandledType { setting_type: setting_type }),
                    )))
                    .send();
            }
            Some(signature) => {
                let mut receptor = self
                    .controller_messenger_client
                    .message(
                        ControllerPayload::Command(Command::HandleRequest(request.clone())),
                        Audience::Messenger(signature),
                    )
                    .send();

                fasync::spawn(async move {
                    while let Ok(message_event) = receptor.watch().await {
                        match message_event {
                            MessageEvent::Message(ControllerPayload::Result(result), _) => {
                                client
                                    .reply(Payload::Event(SettingEvent::Response(id, result)))
                                    .send();
                                return;
                            }
                            MessageEvent::Status(DeliveryStatus::Undeliverable) => {
                                client
                                    .reply(Payload::Event(SettingEvent::Response(
                                        id,
                                        Err(SwitchboardError::UndeliverableError {
                                            setting_type: setting_type,
                                            request: request,
                                        }),
                                    )))
                                    .send();
                                return;
                            }
                            _ => {}
                        }
                    }
                });
            }
        }
    }
}
