// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxb/68069): Add module documentation describing Setting Proxy's role in
// setting handling.
use crate::handler::base::{
    Command, Error as HandlerError, Event, ExitResult, Payload as HandlerPayload,
    Request as HandlerRequest, SettingHandlerFactory, SettingHandlerResult, State,
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

use crate::base::{SettingInfo, SettingType};
use crate::handler::base::{Payload, Request};
use crate::internal::core;
use crate::internal::event;
use crate::internal::handler;
use crate::message::base::{Audience, MessageEvent, MessengerType, Status};
use crate::service;
use crate::service::TryFromWithClient;
use crate::switchboard::base::{SettingAction, SettingActionData, SettingEvent};
use fuchsia_zircon::Duration;

/// An enumeration of the different client types that provide requests to
/// setting handlers.
#[derive(Clone, Debug)]
enum Client {
    /// A client from the core MessageHub to communicate with the switchboard.
    /// The first element represents the id of the action while the second
    /// element is the client to communicate back responses to.
    Core(u64, core::message::MessageClient),
    /// A client from the Unified (service) MessageHub
    Service(service::message::MessageClient),
}

/// A container for associating a Handler Request with a given [`Client`].
#[derive(Clone, Debug)]
struct RequestInfo {
    setting_request: Request,
    client: Client,
}

impl RequestInfo {
    /// Sends the supplied result as a reply with the associated [`Client`].
    pub fn reply(&self, result: SettingHandlerResult) {
        match &self.client {
            Client::Core(id, client) => {
                client.reply(core::Payload::Event(SettingEvent::Response(*id, result))).send();
            }
            Client::Service(client) => {
                // While the switchboard is still being used, we must manually
                // convert the ControllerError into a HandlerError if present.
                // Once switchboard has been removed, we can move SettingProxy
                // and controller implementations to report back HandlerErrors
                // directly.
                client
                    .reply(HandlerPayload::Response(result.map_err(HandlerError::from)).into())
                    .send();
            }
        }
    }
}

#[derive(Clone, Debug)]
struct ActiveRequest {
    request: RequestInfo,
    // The number of attempts that have been made on this request.
    attempts: u64,
    last_result: Option<SettingHandlerResult>,
}

impl ActiveRequest {
    pub fn get_request(&self) -> Request {
        self.request.setting_request.clone()
    }
}

#[derive(Clone, Debug)]
enum ProxyRequest {
    /// Adds a request to the pending request queue.
    Add(RequestInfo),
    /// Executes the next pending request, recreating the handler if the
    /// argument is set to true.
    Execute(bool),
    /// Evaluates supplied the result for the active request.
    HandleResult(SettingHandlerResult),
    /// Request to remove the active request.
    RemoveActive,
    /// Requests resources be torn down. Called when there are no more requests
    /// to process.
    Teardown,
    /// Request to retry the active request.
    Retry,
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

    core_messenger_client: core::message::Messenger,

    client_signature: Option<handler::message::Signature>,
    active_request: Option<ActiveRequest>,
    pending_requests: VecDeque<RequestInfo>,
    has_active_listener: bool,

    /// Handler factory.
    handler_factory: Arc<Mutex<dyn SettingHandlerFactory + Send + Sync>>,
    /// Factory for creating messengers to communicate with handlers.
    controller_messenger_factory: handler::message::Factory,
    /// Client for communicating with handlers.
    controller_messenger_client: handler::message::Messenger,
    /// Signature for the controller messenger.
    controller_messenger_signature: handler::message::Signature,
    /// Client for communicating events.
    event_publisher: event::Publisher,

    /// Sender for passing messages about the active requests and controllers.
    proxy_request_sender: UnboundedSender<ProxyRequest>,
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
        messenger_factory: service::message::Factory,
        core_messenger_factory: core::message::Factory,
        controller_messenger_factory: handler::message::Factory,
        event_messenger_factory: event::message::Factory,
        max_attempts: u64,
        request_timeout: Option<Duration>,
        retry_on_timeout: bool,
    ) -> Result<(core::message::Signature, handler::message::Signature), Error> {
        let (_, mut receptor) = messenger_factory
            .create(MessengerType::Addressable(service::Address::Handler(setting_type)))
            .await
            .map_err(Error::new)?;

        // TODO(fxbug.dev/67536): Remove receptors below as their logic is
        // migrated to the MessageHub defined above.

        let (core_client, mut core_receptor) =
            core_messenger_factory.create(MessengerType::Unbound).await.map_err(Error::new)?;

        let signature = core_receptor.get_signature();

        let (controller_messenger_client, mut controller_receptor) = controller_messenger_factory
            .create(MessengerType::Unbound)
            .await
            .map_err(Error::new)?;

        let event_publisher = event::Publisher::create(
            &event_messenger_factory,
            MessengerType::Addressable(event::Address::SettingProxy(setting_type)),
        )
        .await;

        let handler_signature = controller_receptor.get_signature();

        let (proxy_request_sender, mut proxy_request_receiver) =
            futures::channel::mpsc::unbounded::<ProxyRequest>();

        // We must create handle here rather than return back the value as we
        // reference the proxy in the async tasks below.
        let mut proxy = Self {
            setting_type,
            handler_factory,
            client_signature: None,
            active_request: None,
            pending_requests: VecDeque::new(),
            has_active_listener: false,
            core_messenger_client: core_client,
            controller_messenger_signature: handler_signature.clone(),
            controller_messenger_client,
            controller_messenger_factory,
            event_publisher: event_publisher,
            proxy_request_sender,
            max_attempts,
            request_timeout: request_timeout,
            retry_on_timeout,
        };

        // Main task loop for receiving and processing incoming messages.
        fasync::Task::spawn(async move {
            loop {
                let receptor_fuse = receptor.next().fuse();
                let controller_fuse = controller_receptor.next().fuse();
                let core_fuse = core_receptor.next().fuse();
                futures::pin_mut!(controller_fuse, core_fuse, receptor_fuse);

                futures::select! {
                    event = receptor_fuse => {
                        if let Ok((Payload::Request(request), client)) =
                                event.map_or(Err("no event"),
                                Payload::try_from_with_client) {
                            proxy.process_service_request(request, client).await;
                        }
                    }
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
                                Event::Changed(setting_info) => {
                                    proxy.notify(setting_info);
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

                    // Handles messages for enqueueing requests and processing
                    // results on the main event loop for proxy.
                    request = proxy_request_receiver.next() => {
                        if let Some(request) = request {
                            match request {
                                ProxyRequest::Add(request) => {
                                    proxy.add_request(request);
                                }
                                ProxyRequest::Execute(recreate_handler) => {
                                    proxy.execute_next_request(recreate_handler).await;
                                }
                                ProxyRequest::RemoveActive => {
                                    proxy.remove_active_request();
                                }
                                ProxyRequest::Teardown => {
                                    proxy.teardown_if_needed().await
                                }
                                ProxyRequest::Retry => {
                                    proxy.retry();
                                }
                                ProxyRequest::HandleResult(result) => {
                                    proxy.handle_result(result);
                                }
                                ProxyRequest::Listen(event) => {
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

    async fn process_service_request(
        &mut self,
        request: HandlerRequest,
        message_client: service::message::MessageClient,
    ) {
        self.process_request(RequestInfo {
            setting_request: request,
            client: Client::Service(message_client),
        })
        .await;
    }

    /// Interpret action from switchboard into proxy actions.
    async fn process_action(
        &mut self,
        action: SettingAction,
        mut message_client: core::message::MessageClient,
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
                self.process_request(RequestInfo {
                    setting_request: request,
                    client: Client::Core(action.id, message_client),
                })
                .await;
            }
            SettingActionData::Listen(size) => {
                self.request(ProxyRequest::Listen(ListenEvent::ListenerCount(size)));
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
                    self.controller_messenger_signature.clone(),
                )
                .await
                .map_or(None, Some);
        }

        self.client_signature
    }

    /// Informs the Switchboard when the controller has indicated the setting
    /// has changed.
    fn notify(&self, setting_info: SettingInfo) {
        if !self.has_active_listener {
            return;
        }

        self.core_messenger_client
            .message(
                core::Payload::Event(SettingEvent::Changed(setting_info)),
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
        if self.active_request.is_some() {
            self.proxy_request_sender
                .unbounded_send(ProxyRequest::HandleResult(Err(ControllerError::ExitError)))
                .ok();
        }

        // If there is an active listener, forefully refetch
        if self.has_active_listener {
            self.request(ProxyRequest::Listen(ListenEvent::Restart));
        }
    }

    /// Ensures we first have an active controller (spun up by
    /// get_handler_signature if not already active) before adding the request
    /// to the proxy's queue.
    async fn process_request(&mut self, request: RequestInfo) {
        match self.get_handler_signature(false).await {
            None => {
                request.reply(Err(ControllerError::UnhandledType(self.setting_type)));
            }
            Some(_) => {
                self.request(ProxyRequest::Add(request));
            }
        }
    }

    /// Adds a request to the request queue for this setting.
    ///
    /// If this is the first request in the queue, processing will begin immediately.
    ///
    /// Should only be called on the main task spawned in [SettingProxy::create](#method.create).
    fn add_request(&mut self, request: RequestInfo) {
        self.pending_requests.push_back(request);

        // If this is the first request (no active request or pending requests),
        // request the controller begin execution of requests. Otherwise, the
        // controller is already executing requests and will eventually process
        // this new request.
        if self.pending_requests.len() == 1 && self.active_request.is_none() {
            self.request(ProxyRequest::Execute(false));
        }
    }

    /// Sends a request to be processed by the proxy. Requests are sent as
    /// messages and marshalled onto a single event loop to ensure proper
    /// ordering.
    fn request(&mut self, request: ProxyRequest) {
        self.proxy_request_sender.unbounded_send(request).ok();
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

        self.request(ProxyRequest::Teardown);
    }

    /// Evaluates the supplied result for the current active request. Based
    /// on the return result, also determines whether the request should be
    /// retried. Based on this determination, the function will request from
    /// the proxy whether to retry the request or remove the request (and send
    /// response).
    fn handle_result(&mut self, mut result: SettingHandlerResult) {
        let active_request = self.active_request.as_mut().expect("request should be present");
        let mut retry = false;

        if matches!(result, Err(ControllerError::ExternalFailure(..)))
            || matches!(result, Err(ControllerError::ExitError))
        {
            result = Err(ControllerError::IrrecoverableError);
            retry = true;
        } else if matches!(result, Err(ControllerError::TimeoutError)) {
            publish!(
                self,
                event::handler::Event::Request(
                    event::handler::Action::Timeout,
                    active_request.get_request()
                )
            );
            retry = self.retry_on_timeout;
        }

        active_request.last_result = Some(result);

        if retry {
            self.request(ProxyRequest::Retry);
        } else {
            self.request(ProxyRequest::RemoveActive);
        }
    }

    /// Removes the active request for this setting.
    ///
    /// Should only be called once a request is finished processing.
    ///
    /// Should only be called on the main task spawned in [SettingProxy::create](#method.create).
    fn remove_active_request(&mut self) {
        let mut removed_request = self.active_request.take().expect("request should be present");

        // Send result back to original caller if present.
        if let Some(result) = removed_request.last_result.take() {
            removed_request.request.reply(result)
        }

        // If there are still requests to process, then request for the next to
        // be processed. Otherwise request teardown.
        if !self.pending_requests.is_empty() {
            self.request(ProxyRequest::Execute(false));
        } else {
            self.request(ProxyRequest::Teardown);
        }
    }

    /// Processes the next request in the queue of pending requests.
    ///
    /// If the queue is empty, nothing happens.
    ///
    /// Should only be called on the main task spawned in [SettingProxy::create](#method.create).
    async fn execute_next_request(&mut self, recreate_handler: bool) {
        if self.active_request.is_none() {
            // Add the request to the queue of requests to process.
            self.active_request = Some(ActiveRequest {
                request: self
                    .pending_requests
                    .pop_front()
                    .expect("execute should only be called with present requests"),
                attempts: 0,
                last_result: None,
            });
        }

        // Recreating signature is always honored, even if the request is not.
        let signature = self
            .get_handler_signature(recreate_handler)
            .await
            .expect("failed to generate handler signature");

        let active_request =
            self.active_request.as_mut().expect("active request should be present");
        active_request.attempts += 1;

        // Note that we must copy these values as we are borrowing self for
        // active_requests and self is needed to remove active below.
        let request = active_request.get_request();

        // If we have exceeded the maximum number of attempts, remove this
        // request from the queue.
        if active_request.attempts > self.max_attempts {
            publish!(
                self,
                event::handler::Event::Request(
                    event::handler::Action::AttemptsExceeded,
                    request.clone()
                )
            );

            self.request(ProxyRequest::RemoveActive);
            return;
        }

        publish!(
            self,
            event::handler::Event::Request(event::handler::Action::Execute, request.clone())
        );

        let mut receptor = self
            .controller_messenger_client
            .message(
                handler::Payload::Command(Command::HandleRequest(request.clone())),
                Audience::Messenger(signature),
            )
            .set_timeout(self.request_timeout)
            .send();

        let proxy_request_sender_clone = self.proxy_request_sender.clone();

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
                    proxy_request_sender_clone
                        .unbounded_send(ProxyRequest::HandleResult(result))
                        .ok();
                    return;
                }
            }
        })
        .detach();
    }

    /// Requests the active request to be tried again, forcefully recreating the
    /// handler.
    fn retry(&mut self) {
        publish!(
            self,
            event::handler::Event::Request(
                event::handler::Action::Retry,
                self.active_request
                    .as_ref()
                    .expect("active request should be present")
                    .get_request(),
            )
        );

        self.request(ProxyRequest::Execute(true));
    }

    /// Transitions the controller for the [setting_type] to the Teardown phase
    /// and removes it from the active_controllers.
    async fn teardown_if_needed(&mut self) {
        if self.active_request.is_some()
            || !self.pending_requests.is_empty()
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
