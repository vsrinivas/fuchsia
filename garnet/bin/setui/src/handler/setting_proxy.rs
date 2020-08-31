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
use crate::internal::event::{self, Event};
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
    attempts: u64,
}

#[derive(Clone, Debug)]
enum ActiveControllerRequest {
    /// Request to add an active request from a ControllerState.
    AddActive(ActiveRequest),
    /// Executes the next active request, recreating the handler if the
    /// argument is set to true.
    Execute(bool),
    /// Request to remove an active request from a ControllerState.
    ///
    /// In addition to the request id, a `Result` is provided to specify a
    /// `SettingEvent` to return in the success case or an empty Error to
    /// represent the that request has irrocoverably failed, resulting in
    /// `ControllerError::IrrecoverableError` being returned.
    RemoveActive(u64, Result<SettingEvent, ()>),
    /// Requests resources be torn down. Called when there are no more requests
    /// to process.
    Teardown,
    /// Request to retry the current request.
    Retry(u64),
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
    /// Client for communicating events.
    event_publisher: event::Publisher,

    /// Sender for passing messages about the active requests and controllers.
    active_controller_sender: UnboundedSender<ActiveControllerRequest>,
    max_attempts: u64,
}

impl SettingProxy {
    /// Creates a SettingProxy that is listening to SettingAction from the
    /// provided receiver and will send responses/updates on the given sender.
    pub async fn create(
        setting_type: SettingType,
        handler_factory: Arc<Mutex<dyn SettingHandlerFactory + Send + Sync>>,
        messenger_factory: core::message::Factory,
        controller_messenger_factory: handler::message::Factory,
        event_messenger_factory: event::message::Factory,
        max_attempts: u64,
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

        let event_publisher = event::Publisher::create(
            &event_messenger_factory,
            MessengerType::Addressable(event::Address::SettingProxy(setting_type)),
        )
        .await;

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
            event_publisher: event_publisher,
            active_controller_sender,
            max_attempts,
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
                                    proxy.add_active_request(active_request);
                                }
                                ActiveControllerRequest::Execute(recreate_handler) => {
                                    proxy.execute_next_request(recreate_handler).await;
                                }
                                ActiveControllerRequest::RemoveActive(id, result) => {
                                    proxy.remove_active_request(id, result);
                                }
                                ActiveControllerRequest::Teardown => {
                                    proxy.teardown_if_needed().await
                                }
                                ActiveControllerRequest::Retry(id) => {
                                    proxy.retry(id);
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

    async fn get_handler_signature(
        &mut self,
        force_create: bool,
    ) -> Option<handler::message::Signature> {
        if force_create || self.client_signature.is_none() {
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

        let optional_handler_signature = self.get_handler_signature(false).await;
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

        self.request(ActiveControllerRequest::Teardown);
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
        match self.get_handler_signature(false).await {
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
                let active_request = ActiveRequest {
                    id,
                    request: request.clone(),
                    client: client.clone(),
                    attempts: 0,
                };
                self.request(ActiveControllerRequest::AddActive(active_request));
            }
        }
    }

    /// Adds an active request to the request queue for this setting.
    ///
    /// If this is the first request in the queue, processing will begin immediately.
    ///
    /// Should only be called on the main task spawned in [SettingProxy::create](#method.create).
    fn add_active_request(&mut self, active_request: ActiveRequest) {
        self.active_requests.push_back(active_request);
        if self.active_requests.len() == 1 {
            self.request(ActiveControllerRequest::Execute(false));
        }
    }

    fn request(&mut self, request: ActiveControllerRequest) {
        self.active_controller_sender.unbounded_send(request).ok();
    }

    /// Removes an active request from the request queue for this setting.
    ///
    /// Should only be called once a request is finished processing.
    ///
    /// Should only be called on the main task spawned in [SettingProxy::create](#method.create).
    fn remove_active_request(&mut self, id: u64, result: Result<SettingEvent, ()>) {
        let request_index = self.active_requests.iter().position(|r| r.id == id);
        let removed_request = self
            .active_requests
            .remove(request_index.expect("request ID not found"))
            .expect("request should be present");

        // Send result back to original caller. If the result was an Error, then
        // send back `IrrecoverableError` to indicate an external failure. Other
        // errors outside this condition should be handed back as a
        // SettingEvent.
        removed_request
            .client
            .reply(core::Payload::Event(result.unwrap_or_else(|_| {
                self.event_publisher.send_event(Event::Handler(
                    event::handler::Event::AttemptsExceeded(
                        self.setting_type,
                        removed_request.request.clone(),
                    ),
                ));
                SettingEvent::Response(id, Err(ControllerError::IrrecoverableError))
            })))
            .send();

        // If there are still requests to process, then request for the next to
        // be processed. Otherwise request teardown.
        if !self.active_requests.is_empty() {
            self.request(ActiveControllerRequest::Execute(false));
        } else {
            self.request(ActiveControllerRequest::Teardown);
        }
    }

    /// Processes the next request in the queue of active requests.
    ///
    /// If the queue is empty, nothing happens.
    ///
    /// Should only be called on the main task spawned in [SettingProxy::create](#method.create).
    async fn execute_next_request(&mut self, recreate_handler: bool) {
        // Recreating signature is always honored, even if the request is not.
        let signature = self
            .get_handler_signature(recreate_handler)
            .await
            .expect("failed to generate handler signature");

        let mut active_request = self
            .active_requests
            .front_mut()
            .expect("execute should only be called with present requests");

        active_request.attempts += 1;

        // Note that we must copy these values as we are borrowing self for
        // active_requests and self is needed to remove active below.
        let id = active_request.id;
        let current_attempts = active_request.attempts;
        let request = active_request.request.clone();

        // If we have exceeded the maximum number of attempts, remove this
        // request from the queue.
        if current_attempts > self.max_attempts {
            self.request(ActiveControllerRequest::RemoveActive(id, Err(())));
            return;
        }

        self.event_publisher
            .send_event(Event::Handler(event::handler::Event::Execute(self.setting_type, id)));

        let mut receptor = self
            .controller_messenger_client
            .message(
                handler::Payload::Command(Command::HandleRequest(request.clone())),
                Audience::Messenger(signature),
            )
            .send();

        let active_controller_sender_clone = self.active_controller_sender.clone();

        // TODO(fxb/59016): add timeout for receptor.next() to remove the active request
        // entry and prevent leaks. Context: Faulty handlers can cause `receptor` to never run. When
        // rewriting this to handle retries, ensure that `RemoveActive` is called at some point, or
        // the client will be leaked within active_requests. This must be done especially if the
        // loop below never receives a result.
        fasync::Task::spawn(async move {
            while let Some(message_event) = receptor.next().await {
                let handler_result = match message_event {
                    MessageEvent::Message(handler::Payload::Result(result), _) => {
                        if let Err(ControllerError::ExternalFailure(..)) = result {
                            active_controller_sender_clone
                                .unbounded_send(ActiveControllerRequest::Retry(id))
                                .ok();
                            return;
                        }

                        Some(Ok(SettingEvent::Response(id, result)))
                    }
                    MessageEvent::Status(Status::Undeliverable) => Some(Err(())),
                    _ => None,
                };

                if let Some(result) = handler_result {
                    // Mark the request as having been handled after retries have been
                    // attempted and the client has been notified.
                    active_controller_sender_clone
                        .unbounded_send(ActiveControllerRequest::RemoveActive(id, result))
                        .ok();
                    return;
                }
            }
        })
        .detach();
    }

    /// Requests the first request in the queue be tried again, forcefully
    /// recreating the handler.
    fn retry(&mut self, id: u64) {
        // The supplied id should always match the first request in the queue.
        debug_assert!(
            id == self.active_requests.front().expect("active request should be present").id
        );

        self.event_publisher.send_event(Event::Handler(event::handler::Event::Retry(
            self.setting_type,
            self.active_requests.front().expect("active request should be present").request.clone(),
        )));

        self.request(ActiveControllerRequest::Execute(true));
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

        self.event_publisher
            .send_event(Event::Handler(event::handler::Event::Teardown(self.setting_type)));
    }
}
