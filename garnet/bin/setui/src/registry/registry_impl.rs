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

/// Tracks whether the listener is active and if there are any ongoing requests
/// for a controller.
#[derive(Clone, Debug)]
struct ControllerState {
    signature: handler::message::Signature,
    active_requests: HashMap<u64, ActiveRequest>,
    has_active_listener: bool,
}

#[derive(Clone, Debug)]
struct ActiveRequest {
    request: SettingRequest,
    client: core::message::Client,
}

#[derive(Clone, Debug)]
enum ActiveControllerRequest {
    /// Request to add an active request from a ControllerState.
    AddActive(SettingType, u64, ActiveRequest),
    /// Request to remove an active request from a ControllerState.
    RemoveActive(SettingType, u64),
}

impl ControllerState {
    /// Creates a ControllerState struct with a handler `signature`.
    pub fn create(signature: handler::message::Signature) -> ControllerState {
        Self { signature, active_requests: HashMap::new(), has_active_listener: false }
    }

    /// Marks a request with [id] as active.
    pub fn add_active_request(&mut self, id: u64, active_request: ActiveRequest) {
        self.active_requests.insert(id, active_request);
    }

    /// Returns true if there are no active requests for this controller.
    pub fn pending_requests_empty(&self) -> bool {
        self.active_requests.is_empty()
    }

    /// Unmarks a request with [id] as active.
    pub fn remove_active_request(&mut self, id: u64) -> bool {
        self.active_requests.remove(&id).is_some()
    }
}

pub struct RegistryImpl {
    setting_type: SettingType,

    messenger_client: core::message::Messenger,
    /// Controllers that are currently active. Wrapped in an Arc/Mutex because
    /// it needs to be modified inside an fasync thread, and Self can't be passed
    /// into the fasync thread.
    active_controllers: HashMap<SettingType, ControllerState>,

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
            messenger_client: registry_messenger_client,
            active_controllers: HashMap::new(),
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
                                ActiveControllerRequest::AddActive(setting_type, id, active_request) => {
                                    registry.add_active_request(id, active_request, setting_type);
                                }
                                ActiveControllerRequest::RemoveActive(setting_type, id) => {
                                    registry.remove_active_request(id, setting_type).await;
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
        match action.data {
            SettingActionData::Request(request) => {
                self.process_request(action.id, action.setting_type, request, message_client).await;
            }
            SettingActionData::Listen(size) => {
                self.process_listen(action.setting_type, size).await;
                // Inform client that the request has been processed, regardless
                // of whether a result was produced.
                message_client.acknowledge().await;
            }
        }
    }

    async fn get_handler_signature(&mut self) -> Option<handler::message::Signature> {
        if !self.active_controllers.contains_key(&self.setting_type) {
            // TODO(57374): Propagate error to event publisher.
            if let Ok(signature) = self
                .handler_factory
                .lock()
                .await
                .generate(
                    self.setting_type,
                    self.controller_messenger_factory.clone(),
                    self.controller_messenger_client.get_signature(),
                )
                .await
            {
                self.active_controllers
                    .insert(self.setting_type, ControllerState::create(signature));
            }
        }

        self.active_controllers
            .get(&self.setting_type)
            .map(|controller_state| controller_state.signature)
    }

    /// Notifies proper sink in the case the notification listener count is
    /// non-zero and we aren't already listening for changes to the type or there
    /// are no more listeners and we are actively listening.
    async fn process_listen(&mut self, setting_type: SettingType, size: u64) {
        let optional_handler_signature = self.get_handler_signature().await;
        if optional_handler_signature.is_none() {
            return;
        }

        let handler_signature = optional_handler_signature.unwrap();
        let controller_state = self.active_controllers.get_mut(&setting_type);
        let listening = controller_state.as_ref().map(|ac| ac.has_active_listener).unwrap_or(false);

        let (state, signature) = if size == 0 && listening {
            // The only way for listening to be true is if `controller_state` was `Some(_)`, so
            // the call to `unwrap` here is safe.
            let mut controller_state = controller_state.unwrap();
            controller_state.has_active_listener = false;

            let signature = if controller_state.pending_requests_empty() {
                self.active_controllers.remove(&setting_type).map(|cs| cs.signature)
            } else {
                None
            };
            (State::EndListen, signature)
        } else if size > 0 && !listening {
            if let Some(controller_state) = controller_state {
                controller_state.has_active_listener = true;
            }
            (State::Listen, None)
        } else {
            return;
        };

        self.controller_messenger_client
            .message(
                handler::Payload::Command(Command::ChangeState(state)),
                Audience::Messenger(handler_signature),
            )
            .send()
            .ack();

        if let Some(signature) = signature {
            self.teardown(setting_type, signature).await;
        }
    }

    /// Called by the receiver task when a sink has reported a change to its
    /// setting type.
    fn notify(&self, setting_type: SettingType) {
        // Only return updates for types actively listened on.
        let listening = self
            .active_controllers
            .get(&setting_type)
            .map(|ac| ac.has_active_listener)
            .unwrap_or(false);
        if listening {
            self.messenger_client
                .message(
                    core::Payload::Event(SettingEvent::Changed(setting_type)),
                    Audience::Address(core::Address::Switchboard),
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
        client: core::message::Client,
    ) {
        match self.get_handler_signature().await {
            None => {
                client
                    .reply(core::Payload::Event(SettingEvent::Response(
                        id,
                        Err(SwitchboardError::UnhandledType(setting_type)),
                    )))
                    .send();
            }
            Some(signature) => {
                // Mark the request as being handled.
                let active_request =
                    ActiveRequest { request: request.clone(), client: client.clone() };
                self.active_controller_sender
                    .unbounded_send(ActiveControllerRequest::AddActive(
                        setting_type,
                        id,
                        active_request.clone(),
                    ))
                    .ok();
                self.add_active_request(id, active_request, setting_type);

                let mut receptor = self
                    .controller_messenger_client
                    .message(
                        handler::Payload::Command(Command::HandleRequest(request.clone())),
                        Audience::Messenger(signature),
                    )
                    .send();

                let active_controller_sender_clone = self.active_controller_sender.clone();
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
                                    .unbounded_send(ActiveControllerRequest::RemoveActive(
                                        setting_type,
                                        id,
                                    ))
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
    fn add_active_request(
        &mut self,
        id: u64,
        active_request: ActiveRequest,
        setting_type: SettingType,
    ) {
        if let Some(controller_state) = self.active_controllers.get_mut(&setting_type) {
            controller_state.add_active_request(id, active_request);
        } else {
            fx_log_err!("[registry_impl] Controller mapping not created before adding request")
        }
    }

    /// Removes a request's [id] from the active requests for a given [setting_type].
    async fn remove_active_request(&mut self, id: u64, setting_type: SettingType) {
        let signature = match self.active_controllers.get_mut(&setting_type) {
            Some(controller_state) => {
                controller_state.remove_active_request(id);
                if !controller_state.pending_requests_empty()
                    || controller_state.has_active_listener
                {
                    return;
                }

                controller_state.signature
            }
            None => {
                fx_log_err!(
                    "[registry_impl] Tried to remove active request from nonexistent {:?} type mapping",
                    setting_type
                );
                return;
            }
        };

        self.active_controllers.remove(&setting_type);
        self.teardown(setting_type, signature).await;
    }

    /// Transitions the controller for the [setting_type] to the Teardown phase
    /// and removes it from the active_controllers.
    async fn teardown(
        &mut self,
        setting_type: SettingType,
        signature: handler::message::Signature,
    ) {
        // We don't need to look through for entries in active_controllers with empty
        // active_requests. In remove_active_request, if has_active_listeners was already
        // false and active_requests were empty, its entry will be removed from
        // active_controllers. In process_listen, if the listener count hits 0 and
        // there are no pending requests, the entry will be removed from
        // active_controllers.
        debug_assert!(!self.active_controllers.contains_key(&setting_type));

        let mut controller_receptor = self
            .controller_messenger_client
            .message(
                handler::Payload::Command(Command::ChangeState(State::Teardown)),
                Audience::Messenger(signature),
            )
            .send();

        // Wait for the teardown phase to be over before continuing.
        if let Some(MessageEvent::Status(Status::Received)) = controller_receptor.next().await {
            if self.active_controllers.contains_key(&setting_type) {
                fx_log_err!(
                    "active_controllers still unexpectedly contains {:?}, removing.",
                    setting_type
                );
                self.active_controllers.remove(&setting_type);
            }
        } else {
            // Invalid response from Teardown phase.
            fx_log_err!("Failed to tear down {:?} controller", setting_type);
        }

        // This ensures that the client event loop for the corresponding controller is
        // properly stopped. Without this, the client event loop will run forever.
        self.controller_messenger_factory.delete(signature);
    }
}
