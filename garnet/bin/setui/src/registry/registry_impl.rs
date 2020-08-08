// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::internal::core;
use crate::internal::handler;
use crate::message::base::{Audience, MessageEvent, MessengerType, Status};
use crate::registry::base::{Command, SettingHandlerFactory, State};
use crate::switchboard::base::{
    SettingAction, SettingActionData, SettingEvent, SettingRequest, SettingType, SwitchboardError,
};

use anyhow::Error;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use futures::channel::mpsc::UnboundedSender;
use futures::lock::Mutex;
use futures::{FutureExt, StreamExt};
use std::collections::HashMap;
use std::sync::Arc;

#[derive(Clone, Debug)]
struct ActiveRequest {
    request: SettingRequest,
    client: core::message::Client,
}

#[derive(Clone, Debug)]
enum ActiveControllerRequest {
    /// Request to add an active request from a ControllerState.
    AddActive(u64, ActiveRequest),
    /// Request to remove an active request from a ControllerState.
    RemoveActive(u64),
}

pub struct RegistryImpl {
    setting_type: SettingType,

    messenger_client: core::message::Messenger,

    client_signature: Option<handler::message::Signature>,
    active_requests: HashMap<u64, ActiveRequest>,
    has_active_listener: bool,

    /// Handler factory.
    handler_factory: Arc<Mutex<dyn SettingHandlerFactory + Send + Sync>>,
    /// Factory for creating messengers to communicate with handlers.
    controller_messenger_factory: handler::message::Factory,
    /// Client for communicating with handlers.
    controller_messenger_client: handler::message::Messenger,
    /// Sender for passing messages about the active requests and controllers.
    active_controller_sender: UnboundedSender<ActiveControllerRequest>,
}

impl RegistryImpl {
    /// Creates a RegistryImpl that is listening to SettingAction from the
    /// provided receiver and will send responses/updates on the given sender.
    pub async fn create(
        setting_type: SettingType,
        handler_factory: Arc<Mutex<dyn SettingHandlerFactory + Send + Sync>>,
        messenger_factory: core::message::Factory,
        controller_messenger_factory: handler::message::Factory,
    ) -> Result<(core::message::Signature, handler::message::Signature), Error> {
        let messenger_result = messenger_factory.create(MessengerType::Unbound).await;
        if let Err(error) = messenger_result {
            return Err(Error::new(error));
        }
        let (registry_messenger_client, mut registry_messenger_receptor) =
            messenger_result.unwrap();

        let signature = registry_messenger_client.get_signature();

        let controller_messenger_result =
            controller_messenger_factory.create(MessengerType::Unbound).await;
        if let Err(error) = controller_messenger_result {
            return Err(Error::new(error));
        }
        let (controller_messenger_client, mut controller_receptor) =
            controller_messenger_result.unwrap();

        let handler_signature = controller_messenger_client.get_signature();

        let (active_controller_sender, mut active_controller_receiver) =
            futures::channel::mpsc::unbounded::<ActiveControllerRequest>();

        // We must create handle here rather than return back the value as we
        // reference the registry in the async tasks below.
        let mut registry = Self {
            setting_type,
            handler_factory,
            client_signature: None,
            active_requests: HashMap::new(),
            has_active_listener: false,
            messenger_client: registry_messenger_client,
            controller_messenger_client,
            controller_messenger_factory,
            active_controller_sender,
        };

        fasync::Task::spawn(async move {
            loop {
                let controller_fuse = controller_receptor.next().fuse();
                let registry_fuse = registry_messenger_receptor.next().fuse();
                futures::pin_mut!(controller_fuse, registry_fuse);

                futures::select! {
                    // handle top level message from controllers.
                    controller_event = controller_fuse => {
                        if let Some(
                            MessageEvent::Message(handler::Payload::Changed(setting), _)
                        ) = controller_event {
                            registry.notify(setting);
                        }
                    }

                    // Handle messages from the registry messenger.
                    registry_event = registry_fuse => {
                        if let Some(
                            MessageEvent::Message(core::Payload::Action(action), message_client)
                        ) = registry_event {
                            registry.process_action(action, message_client).await;
                        }
                    }

                    // Handle messages for dealing with the active_controllers.
                    request = active_controller_receiver.next() => {
                        if let Some(request) = request {
                            match request {
                                ActiveControllerRequest::AddActive(id, active_request) => {
                                    registry.add_active_request(id, active_request);
                                }
                                ActiveControllerRequest::RemoveActive(id) => {
                                    registry.remove_active_request(id).await;
                                }
                            }
                        }
                    }
                }
            }
        })
        .detach();
        Ok((signature, handler_signature))
    }

    /// Interpret action from switchboard into registry actions.
    async fn process_action(
        &mut self,
        action: SettingAction,
        mut message_client: core::message::Client,
    ) {
        if self.setting_type != action.setting_type {
            message_client
                .reply(core::Payload::Event(SettingEvent::Response(
                    action.id,
                    Err(SwitchboardError::DeliveryError(action.setting_type, self.setting_type)),
                )))
                .send();
            return;
        }

        match action.data {
            SettingActionData::Request(request) => {
                self.process_request(action.id, request, message_client).await;
            }
            SettingActionData::Listen(size) => {
                self.process_listen(size).await;
                // Inform client that the request has been processed, regardless
                // of whether a result was produced.
                message_client.acknowledge().await;
            }
        }
    }

    async fn get_handler_signature(&mut self) -> Option<handler::message::Signature> {
        if self.client_signature.is_none() {
            self.client_signature = self
                .handler_factory
                .lock()
                .await
                .generate(
                    self.setting_type,
                    self.controller_messenger_factory.clone(),
                    self.controller_messenger_client.get_signature(),
                )
                .await
                .map_or(None, Some);
        }

        self.client_signature
    }

    /// Notifies handler in the case the notification listener count is
    /// non-zero and we aren't already listening for changes or there
    /// are no more listeners and we are actively listening.
    async fn process_listen(&mut self, size: u64) {
        let no_more_listeners = size == 0;
        if no_more_listeners ^ self.has_active_listener {
            return;
        }

        self.has_active_listener = size > 0;

        let optional_handler_signature = self.get_handler_signature().await;
        if optional_handler_signature.is_none() {
            return;
        }

        let handler_signature =
            optional_handler_signature.expect("handler signature should be present");

        self.controller_messenger_client
            .message(
                handler::Payload::Command(Command::ChangeState(if self.has_active_listener {
                    State::Listen
                } else {
                    State::EndListen
                })),
                Audience::Messenger(handler_signature),
            )
            .send()
            .ack();

        self.teardown_if_needed().await;
    }

    /// Called by the receiver task when a sink has reported a change to its
    /// setting type.
    fn notify(&self, setting_type: SettingType) {
        debug_assert!(self.setting_type == setting_type);

        if !self.has_active_listener {
            return;
        }

        self.messenger_client
            .message(
                core::Payload::Event(SettingEvent::Changed(setting_type)),
                Audience::Address(core::Address::Switchboard),
            )
            .send();
    }

    /// Forwards request to proper sink. A new task is spawned in order to receive
    /// the response. If no sink is available, an error is immediately reported
    /// back.
    async fn process_request(
        &mut self,
        id: u64,
        request: SettingRequest,
        client: core::message::Client,
    ) {
        match self.get_handler_signature().await {
            None => {
                client
                    .reply(core::Payload::Event(SettingEvent::Response(
                        id,
                        Err(SwitchboardError::UnhandledType(self.setting_type)),
                    )))
                    .send();
            }
            Some(signature) => {
                // Mark the request as being handled.
                let active_request =
                    ActiveRequest { request: request.clone(), client: client.clone() };
                self.active_controller_sender
                    .unbounded_send(ActiveControllerRequest::AddActive(id, active_request.clone()))
                    .ok();
                self.add_active_request(id, active_request);

                let mut receptor = self
                    .controller_messenger_client
                    .message(
                        handler::Payload::Command(Command::HandleRequest(request.clone())),
                        Audience::Messenger(signature),
                    )
                    .send();

                let active_controller_sender_clone = self.active_controller_sender.clone();
                let setting_type = self.setting_type;

                // TODO(fxb/57168) Faulty handlers can cause `receptor` to never run. When rewriting
                // this to handle retries, ensure that `RemoveActive` is called at some point, or
                // the client will be leaked within active_requests. This must be done especially
                // if the loop below never receives a result.
                fasync::Task::spawn(async move {
                    while let Some(message_event) = receptor.next().await {
                        match message_event {
                            MessageEvent::Message(handler::Payload::Result(result), _) => {
                                // Mark the request as having been handled.
                                active_controller_sender_clone
                                    .unbounded_send(ActiveControllerRequest::RemoveActive(id))
                                    .ok();

                                client
                                    .reply(core::Payload::Event(SettingEvent::Response(id, result)))
                                    .send();
                                return;
                            }
                            MessageEvent::Status(Status::Undeliverable) => {
                                client
                                    .reply(core::Payload::Event(SettingEvent::Response(
                                        id,
                                        Err(SwitchboardError::UndeliverableError(
                                            setting_type,
                                            request,
                                        )),
                                    )))
                                    .send();
                                return;
                            }
                            _ => {}
                        }
                    }
                })
                .detach();
            }
        }
    }

    /// Adds a request's [id] to the active requests for a given [setting_type].
    fn add_active_request(&mut self, id: u64, active_request: ActiveRequest) {
        self.active_requests.insert(id, active_request);
    }

    /// Removes a request's [id] from the active requests for a given [setting_type].
    async fn remove_active_request(&mut self, id: u64) {
        let removed_request = self.active_requests.remove(&id);
        debug_assert!(removed_request.is_some());
        self.teardown_if_needed().await;
    }

    /// Transitions the controller for the [setting_type] to the Teardown phase
    /// and removes it from the active_controllers.
    async fn teardown_if_needed(&mut self) {
        if !self.active_requests.is_empty()
            || self.has_active_listener
            || self.client_signature.is_none()
        {
            return;
        }

        let signature = self.client_signature.take().expect("signature should be set");

        let mut controller_receptor = self
            .controller_messenger_client
            .message(
                handler::Payload::Command(Command::ChangeState(State::Teardown)),
                Audience::Messenger(signature),
            )
            .send();

        // Wait for the teardown phase to be over before continuing.
        if controller_receptor.next().await != Some(MessageEvent::Status(Status::Received)) {
            fx_log_err!("Failed to tear down {:?} controller", self.setting_type);
        }

        // This ensures that the client event loop for the corresponding controller is
        // properly stopped. Without this, the client event loop will run forever.
        self.controller_messenger_factory.delete(signature);
    }
}
