// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::handler::base::{Command, SettingHandlerFactory, State};
use crate::handler::setting_handler::ControllerError;
use std::collections::VecDeque;
use std::sync::Arc;

use anyhow::Error;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use futures::channel::mpsc::UnboundedSender;
use futures::lock::Mutex;
use futures::{FutureExt, StreamExt};

use crate::internal::core;
use crate::internal::handler;
use crate::message::base::{Audience, MessageEvent, MessengerType, Status};
use crate::switchboard::base::{
    SettingAction, SettingActionData, SettingEvent, SettingRequest, SettingType,
};

#[derive(Clone, Debug)]
struct ActiveRequest {
    id: u64,
    request: SettingRequest,
    client: core::message::Client,
}

#[derive(Clone, Debug)]
enum ActiveControllerRequest {
    /// Request to add an active request from a ControllerState.
    AddActive(ActiveRequest),
    /// Request to remove an active request from a ControllerState.
    RemoveActive(u64),
}

pub struct SettingProxy {
    setting_type: SettingType,

    messenger_client: core::message::Messenger,

    client_signature: Option<handler::message::Signature>,
    active_requests: VecDeque<ActiveRequest>,
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

impl SettingProxy {
    /// Creates a SettingProxy that is listening to SettingAction from the
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
        let (core_client, mut core_receptor) = messenger_result.unwrap();

        let signature = core_client.get_signature();

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
        // reference the proxy in the async tasks below.
        let mut proxy = Self {
            setting_type,
            handler_factory,
            client_signature: None,
            active_requests: VecDeque::new(),
            has_active_listener: false,
            messenger_client: core_client,
            controller_messenger_client,
            controller_messenger_factory,
            active_controller_sender,
        };

        // Main task loop for receiving and processing incoming messages.
        fasync::Task::spawn(async move {
            loop {
                let controller_fuse = controller_receptor.next().fuse();
                let core_fuse = core_receptor.next().fuse();
                futures::pin_mut!(controller_fuse, core_fuse);

                futures::select! {
                    // Handle top level message from controllers.
                    controller_event = controller_fuse => {
                        if let Some(
                            MessageEvent::Message(handler::Payload::Changed(setting), _)
                        ) = controller_event {
                            proxy.notify(setting);
                        }
                    }

                    // Handle messages from the core messenger.
                    core_event = core_fuse => {
                        if let Some(
                            MessageEvent::Message(core::Payload::Action(action), message_client)
                        ) = core_event {
                            proxy.process_action(action, message_client).await;
                        }
                    }

                    // Handle messages for dealing with the active_controllers.
                    request = active_controller_receiver.next() => {
                        if let Some(request) = request {
                            match request {
                                ActiveControllerRequest::AddActive(active_request) => {
                                    proxy.add_active_request(active_request).await;
                                }
                                ActiveControllerRequest::RemoveActive(id) => {
                                    proxy.remove_active_request(id).await;
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

    /// Interpret action from switchboard into proxy actions.
    async fn process_action(
        &mut self,
        action: SettingAction,
        mut message_client: core::message::Client,
    ) {
        if self.setting_type != action.setting_type {
            message_client
                .reply(core::Payload::Event(SettingEvent::Response(
                    action.id,
                    Err(ControllerError::DeliveryError(action.setting_type, self.setting_type)),
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
                        Err(ControllerError::UnhandledType(self.setting_type)),
                    )))
                    .send();
            }
            Some(_) => {
                // Add the request to the queue of requests to process.
                let active_request =
                    ActiveRequest { id, request: request.clone(), client: client.clone() };
                self.active_controller_sender
                    .unbounded_send(ActiveControllerRequest::AddActive(active_request))
                    .ok();
            }
        }
    }

    /// Adds an active request to the request queue for this setting.
    ///
    /// If this is the first request in the queue, processing will begin immediately.
    ///
    /// Should only be called on the main task spawned in [SettingProxy::create](#method.create).
    async fn add_active_request(&mut self, active_request: ActiveRequest) {
        self.active_requests.push_back(active_request);
        if self.active_requests.len() == 1 {
            // Queue was empty before this request, start processing.
            self.process_active_requests().await;
        }
    }

    /// Removes an active request from the request queue for this setting.
    ///
    /// Should only be called once a request is finished processing.
    ///
    /// Should only be called on the main task spawned in [SettingProxy::create](#method.create).
    async fn remove_active_request(&mut self, id: u64) {
        let request_index = self.active_requests.iter().position(|r| r.id == id);
        let removed_request =
            self.active_requests.remove(request_index.expect("request ID not found"));
        debug_assert!(removed_request.is_some());

        // Since the previous request finished, resume processing.
        self.process_active_requests().await;

        self.teardown_if_needed().await;
    }

    /// Processes the next request in the queue of active requests.
    ///
    /// If the queue is empty, nothing happens.
    ///
    /// Should only be called on the main task spawned in [SettingProxy::create](#method.create).
    async fn process_active_requests(&mut self) {
        let active_request = match self.active_requests.front() {
            Some(request) => request,
            None => return,
        }
        .clone();

        let request = active_request.request;

        // It's okay to expect here since the handler signature should have been generated when the
        // request first came in. If at that time the generation failed, the client would have been
        // notified and the request would not have been queued.
        let signature = self.client_signature.expect("failed to generate handler signature");

        let mut receptor = self
            .controller_messenger_client
            .message(
                handler::Payload::Command(Command::HandleRequest(request.clone())),
                Audience::Messenger(signature),
            )
            .send();

        let id = active_request.id;
        let client = active_request.client;
        let active_controller_sender_clone = self.active_controller_sender.clone();
        let setting_type = self.setting_type;

        // TODO(fxb/57168): add timeout for receptor.next() to remove the active request
        // entry and prevent leaks. Context: Faulty handlers can cause `receptor` to never run. When
        // rewriting this to handle retries, ensure that `RemoveActive` is called at some point, or
        // the client will be leaked within active_requests. This must be done especially if the
        // loop below never receives a result.
        fasync::Task::spawn(async move {
            while let Some(message_event) = receptor.next().await {
                match message_event {
                    MessageEvent::Message(handler::Payload::Result(result), _) => {
                        match result {
                            Err(_) => {
                                // Handle ControllerError.

                                // TODO(fxb/57171): add retry logic. If unable to succeed with
                                // retries, reply with error. Consider breaking out a separate
                                // function to reduce rightward drift.
                                client
                                    .reply(core::Payload::Event(SettingEvent::Response(id, result)))
                                    .send();
                            }
                            Ok(_) => {
                                client
                                    .reply(core::Payload::Event(SettingEvent::Response(id, result)))
                                    .send();
                            }
                        }
                        break;
                    }
                    MessageEvent::Status(Status::Undeliverable) => {
                        client
                            .reply(core::Payload::Event(SettingEvent::Response(
                                id,
                                Err(ControllerError::UndeliverableError(setting_type, request)),
                            )))
                            .send();
                        break;
                    }
                    _ => {}
                }
            }

            // Mark the request as having been handled after retries have been
            // attempted and the client has been notified.
            active_controller_sender_clone
                .unbounded_send(ActiveControllerRequest::RemoveActive(id))
                .ok();
        })
        .detach();
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
