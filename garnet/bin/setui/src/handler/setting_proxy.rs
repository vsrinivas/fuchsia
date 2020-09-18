// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::handler::base::{
    Command, Event, ExitResult, SettingHandlerFactory, SettingHandlerResult, State,
};
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
use crate::internal::event::{self};
use crate::internal::handler;
use crate::message::base::{Audience, MessageEvent, MessengerType, Status};
use crate::switchboard::base::{
    SettingAction, SettingActionData, SettingEvent, SettingRequest, SettingType,
};
use fuchsia_zircon::Duration;

#[derive(Clone, Debug)]
struct ActiveRequest {
    id: u64,
    request: SettingRequest,
    client: core::message::Client,
    attempts: u64,
    last_result: Option<SettingHandlerResult>,
}

#[derive(Clone, Debug)]
enum ActiveControllerRequest {
    /// Request to add an active request from a ControllerState.
    AddActive(ActiveRequest),
    /// Executes the next active request, recreating the handler if the
    /// argument is set to true.
    Execute(bool),
    /// Processes the result to a request.
    HandleResult(u64, SettingHandlerResult),
    /// Request to remove an active request from a ControllerState.
    RemoveActive(u64),
    /// Requests resources be torn down. Called when there are no more requests
    /// to process.
    Teardown,
    /// Request to retry the current request.
    Retry(u64),
    /// Requests listen
    Listen(ListenEvent),
}

#[derive(Clone, Debug, PartialEq)]
enum ListenEvent {
    Restart,
    ListenerCount(u64),
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
    request_timeout: Option<Duration>,
    retry_on_timeout: bool,
}

/// Publishes an event to the event_publisher.
macro_rules! publish {
    ($self:ident, $event:expr) => {
        $self.event_publisher.send_event(event::Event::Handler($self.setting_type, $event));
    };
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
        request_timeout: Option<Duration>,
        retry_on_timeout: bool,
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
            request_timeout: request_timeout,
            retry_on_timeout,
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
                            MessageEvent::Message(handler::Payload::Event(event), client)
                        ) = controller_event {
                            // Messages received after the client signature
                            // has been changed will be ignored.
                            if Some(client.get_author()) != proxy.client_signature {
                                continue;
                            }
                            match event {
                                Event::Changed => {
                                    proxy.notify();
                                }
                                Event::Exited(result) => {
                                    proxy.process_exit(result);
                                }
                            }
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
                                ActiveControllerRequest::RemoveActive(id) => {
                                    proxy.remove_active_request(id);
                                }
                                ActiveControllerRequest::Teardown => {
                                    proxy.teardown_if_needed().await
                                }
                                ActiveControllerRequest::Retry(id) => {
                                    proxy.retry(id);
                                }
                                ActiveControllerRequest::HandleResult(id, result) => {
                                    proxy.handle_result(id, result);
                                }
                                ActiveControllerRequest::Listen(event) => {
                                    proxy.handle_listen(event).await;
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
                self.request(ActiveControllerRequest::Listen(ListenEvent::ListenerCount(size)));
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

    /// Called by the receiver task when a sink has reported a change to its
    /// setting type.
    fn notify(&self) {
        if !self.has_active_listener {
            return;
        }

        self.messenger_client
            .message(
                core::Payload::Event(SettingEvent::Changed(self.setting_type)),
                Audience::Address(core::Address::Switchboard),
            )
            .send();
    }

    fn process_exit(&mut self, result: ExitResult) {
        // Log the exit
        self.event_publisher.send_event(event::Event::Handler(
            self.setting_type,
            event::handler::Event::Exit(result),
        ));

        // Clear the setting handler client signature
        self.client_signature = None;

        // If there is an active request, process the error
        if !self.active_requests.is_empty() {
            self.active_controller_sender
                .unbounded_send(ActiveControllerRequest::HandleResult(
                    self.active_requests.front().expect("active request should be present").id,
                    Err(ControllerError::ExitError),
                ))
                .ok();
        }

        // If there is an active listener, forefully refetch
        if self.has_active_listener {
            self.request(ActiveControllerRequest::Listen(ListenEvent::Restart));
        }
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
                    last_result: None,
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

    /// Notifies handler in the case the notification listener count is
    /// non-zero and we aren't already listening for changes or there
    /// are no more listeners and we are actively listening.
    async fn handle_listen(&mut self, event: ListenEvent) {
        if let ListenEvent::ListenerCount(size) = event {
            let no_more_listeners = size == 0;
            if no_more_listeners ^ self.has_active_listener {
                return;
            }

            self.has_active_listener = size > 0;
        }

        let optional_handler_signature =
            self.get_handler_signature(ListenEvent::Restart == event).await;
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

    fn handle_result(&mut self, id: u64, mut result: SettingHandlerResult) {
        let request_index = self.active_requests.iter().position(|r| r.id == id);
        let request = self
            .active_requests
            .get_mut(request_index.expect("request ID not found"))
            .expect("request should be present");
        let mut retry = false;

        if matches!(result, Err(ControllerError::ExternalFailure(..)))
            || matches!(result, Err(ControllerError::ExitError))
        {
            result = Err(ControllerError::IrrecoverableError);
            retry = true;
        } else if matches!(result, Err(ControllerError::TimeoutError)) {
            publish!(self, event::handler::Event::Timeout(request.request.clone()));
            retry = self.retry_on_timeout;
        }

        request.last_result = Some(result);

        if retry {
            self.request(ActiveControllerRequest::Retry(id));
        } else {
            self.request(ActiveControllerRequest::RemoveActive(id));
        }
    }

    /// Removes an active request from the request queue for this setting.
    ///
    /// Should only be called once a request is finished processing.
    ///
    /// Should only be called on the main task spawned in [SettingProxy::create](#method.create).
    fn remove_active_request(&mut self, id: u64) {
        let request_index = self.active_requests.iter().position(|r| r.id == id);
        let mut removed_request = self
            .active_requests
            .remove(request_index.expect("request ID not found"))
            .expect("request should be present");

        // Send result back to original caller if present.
        if let Some(result) = removed_request.last_result.take() {
            removed_request
                .client
                .reply(core::Payload::Event(SettingEvent::Response(removed_request.id, result)))
                .send();
        }

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
            publish!(self, event::handler::Event::AttemptsExceeded(request));
            self.request(ActiveControllerRequest::RemoveActive(id));
            return;
        }

        publish!(self, event::handler::Event::Execute(id));

        let mut receptor = self
            .controller_messenger_client
            .message(
                handler::Payload::Command(Command::HandleRequest(request.clone())),
                Audience::Messenger(signature),
            )
            .set_timeout(self.request_timeout)
            .send();

        let active_controller_sender_clone = self.active_controller_sender.clone();

        fasync::Task::spawn(async move {
            while let Some(message_event) = receptor.next().await {
                let handler_result = match message_event {
                    MessageEvent::Message(handler::Payload::Result(result), _) => Some(result),
                    MessageEvent::Status(Status::Undeliverable) => {
                        Some(Err(ControllerError::IrrecoverableError))
                    }
                    MessageEvent::Status(Status::Timeout) => {
                        Some(Err(ControllerError::TimeoutError))
                    }
                    _ => None,
                };

                if let Some(result) = handler_result {
                    // Mark the request as having been handled after retries have been
                    // attempted and the client has been notified.
                    active_controller_sender_clone
                        .unbounded_send(ActiveControllerRequest::HandleResult(id, result))
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

        publish!(
            self,
            event::handler::Event::Retry(
                self.active_requests
                    .front()
                    .expect("active request should be present")
                    .request
                    .clone(),
            )
        );

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

        publish!(self, event::handler::Event::Teardown);
    }
}
