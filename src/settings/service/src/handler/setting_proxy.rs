// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxb/68069): Add module documentation describing Setting Proxy's role in
// setting handling.
use crate::base::{SettingInfo, SettingType};
use crate::handler::base::{
    Error as HandlerError, Payload as HandlerPayload, Request as HandlerRequest,
    SettingHandlerFactory,
};
use crate::handler::base::{Payload, Request};
use crate::handler::setting_handler;
use crate::handler::setting_handler::Command;
use crate::handler::setting_handler::{
    ControllerError, Event, ExitResult, SettingHandlerResult, State,
};
use crate::inspect::listener_logger::ListenerInspectLogger;
use crate::message::action_fuse::ActionFuseBuilder;
use crate::message::base::{Audience, MessageEvent, MessengerType, Status};
use crate::{clock, event, service, trace, trace_guard};
use anyhow::Error;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use fuchsia_trace as ftrace;
use fuchsia_zircon::Duration;
use futures::channel::mpsc::UnboundedSender;
use futures::lock::Mutex;
use futures::{FutureExt, StreamExt};
use std::collections::VecDeque;
use std::sync::Arc;

/// Maximum number of errors tracked per setting proxy before errors are rolled over.
// The value was chosen arbitrarily. Feel free to increase if it's too small.
pub(crate) const MAX_NODE_ERRORS: usize = 10;

/// An enumeration of the different client types that provide requests to
/// setting handlers.
#[derive(Clone, Debug)]
enum Client {
    /// A client from the service MessageHub.
    Service(service::message::MessageClient),
}

/// A container for associating a Handler Request with a given [`Client`].
#[derive(Clone, Debug)]
struct RequestInfo {
    setting_request: Request,
    client: Client,
    // This identifier is unique within each setting proxy to identify a
    // request. This can be used for removing a particular RequestInfo within a
    // set, such as the active change listeners.
    id: usize,
}

impl PartialEq for RequestInfo {
    fn eq(&self, other: &Self) -> bool {
        self.id == other.id
    }
}

impl RequestInfo {
    /// Sends the supplied result as a reply with the associated [`Client`].
    pub(crate) fn reply(&self, result: SettingHandlerResult) {
        match &self.client {
            Client::Service(client) => {
                // TODO(fxbug.dev/70985): return HandlerErrors directly
                // Ignore the receptor result.
                let _ = client
                    .reply(HandlerPayload::Response(result.map_err(HandlerError::from)).into())
                    .send();
            }
        }
    }

    /// Sends an acknowledge message back through the reply client. This used in
    /// long running requests (such a listen) where acknowledge message ensures
    /// the client the request was processed.
    async fn acknowledge(&mut self) {
        match &mut self.client {
            Client::Service(client) => {
                client.acknowledge().await;
            }
        }
    }

    /// Adds a closure that will be triggered when the recipient for a response
    /// to the request goes out of scope. This allows for the message handler to
    /// know when the recipient is no longer valid.
    async fn bind_to_scope(&mut self, trigger_fn: Box<dyn FnOnce(RequestInfo) + Sync + Send>) {
        let request = self.clone();

        let fuse = ActionFuseBuilder::new()
            .add_action(Box::new(move || {
                (trigger_fn)(request);
            }))
            .build();

        match &mut self.client {
            Client::Service(client) => {
                client.bind_to_recipient(fuse).await;
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
    pub(crate) fn get_request(&self) -> Request {
        self.request.setting_request.clone()
    }

    pub(crate) fn get_info(&mut self) -> &mut RequestInfo {
        &mut self.request
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
    /// Request to remove listen request.
    EndListen(RequestInfo),
    /// Starts a timeout for resources be torn down. Called when there are no
    /// more requests to process.
    TeardownTimeout,
    /// Request for resources to be torn down.
    Teardown,
    /// Request to retry the active request.
    Retry,
    /// Requests listen
    Listen(ListenEvent),
}

#[derive(Clone, Debug, PartialEq)]
enum ListenEvent {
    Restart,
}

pub(crate) struct SettingProxy {
    setting_type: SettingType,

    client_signature: Option<service::message::Signature>,
    active_request: Option<ActiveRequest>,
    pending_requests: VecDeque<RequestInfo>,
    listen_requests: Vec<RequestInfo>,
    next_request_id: usize,

    /// Factory for generating a new controller to service requests.
    handler_factory: Arc<Mutex<dyn SettingHandlerFactory + Send + Sync>>,
    /// Messenger factory for communication with service components.
    delegate: service::message::Delegate,
    /// Messenger to send messages to controllers.
    messenger: service::message::Messenger,
    /// Signature for messages from controllers to be direct towards.
    signature: service::message::Signature,
    /// Client for communicating events.
    event_publisher: event::Publisher,

    /// Sender for passing messages about the active requests and controllers.
    proxy_request_sender: UnboundedSender<ProxyRequest>,
    max_attempts: u64,
    teardown_timeout: Duration,
    request_timeout: Option<Duration>,
    retry_on_timeout: bool,
    teardown_cancellation: Option<futures::channel::oneshot::Sender<()>>,
    _node: fuchsia_inspect::Node,
    error_node: fuchsia_inspect::Node,
    node_errors: VecDeque<NodeError>,
    error_count: usize,

    /// Inspect logger for active listener counts.
    listener_logger: Arc<Mutex<ListenerInspectLogger>>,
}

struct NodeError {
    _node: fuchsia_inspect::Node,
    _timestamp: fuchsia_inspect::StringProperty,
    _value: fuchsia_inspect::StringProperty,
}

/// Publishes an event to the event_publisher.
macro_rules! publish {
    ($self:ident, $event:expr) => {
        $self.event_publisher.send_event(event::Event::Handler($self.setting_type, $event));
    };
}

impl SettingProxy {
    /// Creates a SettingProxy that is listening to requests from the
    /// provided receiver and will send responses/updates on the given sender.
    #[allow(clippy::too_many_arguments)]
    pub(crate) async fn create(
        setting_type: SettingType,
        handler_factory: Arc<Mutex<dyn SettingHandlerFactory + Send + Sync>>,
        delegate: service::message::Delegate,
        max_attempts: u64,
        teardown_timeout: Duration,
        request_timeout: Option<Duration>,
        retry_on_timeout: bool,
        node: fuchsia_inspect::Node,
        listener_logger: Arc<Mutex<ListenerInspectLogger>>,
    ) -> Result<service::message::Signature, Error> {
        let (messenger, receptor) = delegate
            .create(MessengerType::Addressable(service::Address::Handler(setting_type)))
            .await
            .map_err(Error::new)?;
        let service_signature = receptor.get_signature();

        // TODO(fxbug.dev/67536): Remove receptors below as their logic is
        // migrated to the MessageHub defined above.

        let event_publisher = event::Publisher::create(
            &delegate,
            MessengerType::Addressable(service::Address::EventSource(
                event::Address::SettingProxy(setting_type),
            )),
        )
        .await;

        let (proxy_request_sender, proxy_request_receiver) =
            futures::channel::mpsc::unbounded::<ProxyRequest>();

        let error_node = node.create_child("errors");

        // We must create handle here rather than return back the value as we
        // reference the proxy in the async tasks below.
        let mut proxy = Self {
            setting_type,
            handler_factory,
            next_request_id: 0,
            client_signature: None,
            active_request: None,
            pending_requests: VecDeque::new(),
            listen_requests: Vec::new(),
            delegate,
            messenger,
            signature: service_signature,
            event_publisher,
            proxy_request_sender,
            max_attempts,
            teardown_timeout,
            request_timeout,
            retry_on_timeout,
            teardown_cancellation: None,
            _node: node,
            error_node,
            node_errors: VecDeque::new(),
            error_count: 0,
            listener_logger,
        };

        // Main task loop for receiving and processing incoming messages.
        fasync::Task::spawn(async move {
            let id = ftrace::Id::new();
            trace!(
                id,

                "setting_proxy",
                "setting_type" => format!("{:?}", setting_type).as_str()
            );
            let receptor_fuse = receptor.fuse();
            let proxy_fuse = proxy_request_receiver.fuse();

            futures::pin_mut!(receptor_fuse, proxy_fuse);

            loop {
                futures::select! {
                    // Handles requests from the service MessageHub and
                    // communication from the setting controller.
                    event = receptor_fuse.select_next_some() => {
                        trace!(
                            id,

                            "service event"
                        );
                        proxy.process_service_event(id, event).await;
                    }

                    // Handles messages for enqueueing requests and processing
                    // results on the main event loop for proxy.
                    request = proxy_fuse.select_next_some() => {
                        trace!(
                            id,

                            "proxy request"
                        );
                        proxy.process_proxy_request(id, request).await;
                    }
                }
            }
        })
        .detach();
        Ok(service_signature)
    }

    async fn process_service_event(
        &mut self,
        id: ftrace::Id,
        event: service::message::MessageEvent,
    ) {
        if let MessageEvent::Message(payload, client) = event {
            match payload {
                service::Payload::Setting(Payload::Request(request)) => {
                    trace!(id, "process_service_request");
                    self.process_service_request(request, client).await;
                }
                service::Payload::Controller(setting_handler::Payload::Event(event)) => {
                    // Messages received after the client signature
                    // has been changed will be ignored.
                    let guard = trace_guard!(id, "get author");
                    if Some(client.get_author()) != self.client_signature {
                        return;
                    }
                    drop(guard);

                    match event {
                        Event::Changed(setting_info) => {
                            trace!(id, "change notification");
                            self.notify(setting_info);
                        }
                        Event::Exited(result) => {
                            trace!(id, "process exit");
                            self.process_exit(result);
                        }
                    }
                }
                _ => {
                    panic!("Unexpected message");
                }
            }
        }
    }

    async fn process_proxy_request(&mut self, id: ftrace::Id, request: ProxyRequest) {
        match request {
            ProxyRequest::Add(request) => {
                trace!(id, "add request");
                self.add_request(request);
            }
            ProxyRequest::Execute(recreate_handler) => {
                trace!(id, "execute");
                self.execute_next_request(id, recreate_handler).await;
            }
            ProxyRequest::RemoveActive => {
                trace!(id, "remove active");
                self.remove_active_request();
            }
            ProxyRequest::TeardownTimeout => {
                trace!(id, "teardown timeout");
                self.start_teardown_timeout().await;
            }
            ProxyRequest::Teardown => {
                trace!(id, "teardown");
                self.teardown_if_needed().await;
            }
            ProxyRequest::Retry => {
                trace!(id, "retry");
                self.retry();
            }
            ProxyRequest::HandleResult(result) => {
                trace!(id, "handle result");
                self.handle_result(result);
            }
            ProxyRequest::Listen(event) => {
                trace!(id, "handle listen");
                self.handle_listen(event).await;
            }
            ProxyRequest::EndListen(request_info) => {
                trace!(id, "handle end listen");
                self.listener_logger.lock().await.remove_listener(self.setting_type);
                self.handle_end_listen(request_info).await;
            }
        }
    }

    async fn process_service_request(
        &mut self,
        request: HandlerRequest,
        message_client: service::message::MessageClient,
    ) {
        let id = self.next_request_id;
        self.next_request_id += 1;
        self.process_request(RequestInfo {
            setting_request: request,
            id,
            client: Client::Service(message_client),
        })
        .await;
    }

    async fn get_handler_signature(
        &mut self,
        force_create: bool,
    ) -> Option<service::message::Signature> {
        if force_create || self.client_signature.is_none() {
            self.client_signature = match self
                .handler_factory
                .lock()
                .await
                .generate(self.setting_type, self.delegate.clone(), self.signature)
                .await
            {
                Ok(signature) => Some(signature),
                Err(e) => {
                    let node = self.error_node.create_child(format!("{:020}", self.error_count));
                    let timestamp = node.create_string("timestamp", clock::inspect_format_now());
                    let value = node.create_string("value", format!("{:?}", e));
                    self.node_errors.push_back(NodeError {
                        _node: node,
                        _timestamp: timestamp,
                        _value: value,
                    });
                    self.error_count += 1;
                    if self.node_errors.len() > MAX_NODE_ERRORS {
                        let _ = self.node_errors.pop_front();
                    }
                    None
                }
            };
        }

        self.client_signature
    }

    /// Returns whether there is an active listener across the various
    /// listening clients.
    fn is_listening(&self) -> bool {
        !self.listen_requests.is_empty()
    }

    /// Informs listeners when the controller has indicated the setting has changed.
    fn notify(&self, setting_info: SettingInfo) {
        if !self.is_listening() {
            return;
        }

        // Notify each listener on the service MessageHub.
        for request in &self.listen_requests {
            request.reply(Ok(Some(setting_info.clone())));
        }
    }

    fn process_exit(&mut self, result: ExitResult) {
        // Log the exit
        self.event_publisher.send_event(event::Event::Handler(
            self.setting_type,
            event::handler::Event::Exit(result),
        ));

        // Clear the setting handler client signature
        self.client_signature = None;

        // If there is an active request, process the error. Panic if we couldn't process it.
        if self.active_request.is_some() {
            self.proxy_request_sender
                .unbounded_send(ProxyRequest::HandleResult(Err(ControllerError::ExitError)))
                .expect(
                    "SettingProxy::process_exit, proxy_request_sender failed to send ExitError\
                     proxy request",
                );
        }

        // If there is an active listener, forefully refetch
        if self.is_listening() {
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
        if let Some(teardown_cancellation) = self.teardown_cancellation.take() {
            let _ = teardown_cancellation.send(());
        }
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
    fn request(&self, request: ProxyRequest) {
        self.proxy_request_sender
            .unbounded_send(request)
            .expect("SettingProxy::request, proxy_request_sender cannot send requests anymore");
    }

    /// Sends an update to the controller about whether or not it should be
    /// listening.
    ///
    /// # Arguments
    ///
    /// * `force_recreate_controller` - a bool representing whether the
    /// controller should be recreated regardless if it is currently running.
    async fn send_listen_update(&mut self, force_recreate_controller: bool) {
        let optional_handler_signature =
            self.get_handler_signature(force_recreate_controller).await;
        if optional_handler_signature.is_none() {
            return;
        }

        let handler_signature =
            optional_handler_signature.expect("handler signature should be present");

        self.messenger
            .message(
                setting_handler::Payload::Command(Command::ChangeState(if self.is_listening() {
                    State::Listen
                } else {
                    State::EndListen
                }))
                .into(),
                Audience::Messenger(handler_signature),
            )
            .send()
            .ack();

        self.request(ProxyRequest::TeardownTimeout);
    }

    // TODO(fxbug.dev/67536): Remove this method once no more communication
    // happens over the core MessageHub.
    /// Notifies handler in the case the notification listener count is
    /// non-zero and we aren't already listening for changes or there
    /// are no more listeners and we are actively listening.
    async fn handle_listen(&mut self, event: ListenEvent) {
        self.send_listen_update(ListenEvent::Restart == event).await;
    }

    /// Notifies handler in the case the notification listener count is
    /// non-zero and we aren't already listening for changes or there
    /// are no more listeners and we are actively listening.
    async fn handle_end_listen(&mut self, request: RequestInfo) {
        let was_listening = self.is_listening();

        if let Some(pos) = self.listen_requests.iter().position(|target| *target == request) {
            let _ = self.listen_requests.remove(pos);
        } else {
            return;
        }

        if was_listening != self.is_listening() {
            self.send_listen_update(false).await;
        }
    }

    /// Evaluates the supplied result for the current active request. Based
    /// on the return result, also determines whether the request should be
    /// retried. Based on this determination, the function will request from
    /// the proxy whether to retry the request or remove the request (and send
    /// response).
    fn handle_result(&mut self, result: SettingHandlerResult) {
        let active_request = self.active_request.as_mut().expect("request should be present");
        let mut retry = false;

        if matches!(result, Err(ControllerError::ExternalFailure(..)))
            || matches!(result, Err(ControllerError::ExitError))
        {
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
            self.request(ProxyRequest::TeardownTimeout);
        }
    }

    /// Processes the next request in the queue of pending requests.
    ///
    /// If the queue is empty, nothing happens.
    ///
    /// Should only be called on the main task spawned in [SettingProxy::create](#method.create).
    async fn execute_next_request(&mut self, id: ftrace::Id, recreate_handler: bool) {
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

        // since we borrow self as mutable for active_request, we must retrieve
        // the listening state (which borrows immutable) before.
        let was_listening = self.is_listening();

        let active_request =
            self.active_request.as_mut().expect("active request should be present");

        active_request.attempts += 1;

        // Note that we must copy these values as we are borrowing self for
        // active_requests and self is needed to remove active below.
        let request = active_request.get_request();

        if matches!(request, Request::Listen) {
            let info = active_request.get_info();

            // Increment the active listener count in inspect.
            self.listener_logger.lock().await.add_listener(self.setting_type);

            // Add a callback when the client side goes out of scope. Panic if the unbounded_send
            // failed, which indicates the channel got dropped and requests cannot be processed
            // anymore.
            let proxy_request_sender = self.proxy_request_sender.clone();
            info.bind_to_scope(Box::new(move |request_info| {
                proxy_request_sender.unbounded_send(ProxyRequest::EndListen(request_info)).expect(
                    "SettingProxy::execute_next_request, proxy_request_sender failed to send \
                    EndListen proxy request with info",
                );
            }))
            .await;

            // Add the request to tracked listen requests.
            self.listen_requests.push(info.clone());

            // Listening requests must be acknowledged as they are long-living.
            info.acknowledge().await;

            // If listening state has changed, update state.
            if was_listening != self.is_listening() {
                self.send_listen_update(false).await;
            }

            // Request the active request be removed as it is now tracked
            // elsewhere.
            self.request(ProxyRequest::RemoveActive);
            return;
        }

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
            .messenger
            .message(
                setting_handler::Payload::Command(Command::HandleRequest(request.clone())).into(),
                Audience::Messenger(signature),
            )
            .set_timeout(self.request_timeout)
            .send();

        let proxy_request_sender_clone = self.proxy_request_sender.clone();

        fasync::Task::spawn(async move {
            trace!(id, "response");
            while let Some(message_event) = receptor.next().await {
                let handler_result = match message_event {
                    MessageEvent::Message(
                        service::Payload::Controller(setting_handler::Payload::Result(result)),
                        _,
                    ) => Some(result),
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
                    // attempted and the client has been notified. Panic if the unbounded_send
                    // failed, which indicates the channel got dropped and requests cannot be
                    // processed anymore.
                    proxy_request_sender_clone
                        .unbounded_send(ProxyRequest::HandleResult(result))
                        .expect(
                            "SettingProxy::execute_next_request, proxy_request_sender failed to \
                            send proxy request",
                        );
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

    fn has_active_work(&self) -> bool {
        self.active_request.is_some()
            || !self.pending_requests.is_empty()
            || self.is_listening()
            || self.client_signature.is_none()
    }

    async fn start_teardown_timeout(&mut self) {
        if self.has_active_work() {
            return;
        }

        let (cancellation_tx, cancellation_rx) = futures::channel::oneshot::channel();
        if self.teardown_cancellation.is_some() {
            // Do not overwrite the cancellation. We do not want to extend it if it's already
            // counting down.
            return;
        }

        self.teardown_cancellation = Some(cancellation_tx);
        let sender = self.proxy_request_sender.clone();
        let teardown_timeout = self.teardown_timeout;
        fasync::Task::spawn(async move {
            let timeout = fuchsia_async::Timer::new(crate::clock::now() + teardown_timeout).fuse();
            futures::pin_mut!(cancellation_rx, timeout);
            futures::select! {
                _ = cancellation_rx => {
                    // Exit the loop and do not send teardown message when cancellation received.
                    return;
                }
                _ = timeout => {}, // no-op
            }

            // Panic if the unbounded_send failed, which indicates the channel got dropped and
            // requests cannot be processed anymore.
            sender.unbounded_send(ProxyRequest::Teardown).expect(
                "SettingProxy::start_teardown_timeout, proxy_request_sender failed to send Teardown\
                 proxy request",
            );
        })
        .detach();
    }

    /// Transitions the controller for the `setting_type` to the Teardown phase
    /// and removes it from the active_controllers.
    async fn teardown_if_needed(&mut self) {
        if self.has_active_work() {
            return;
        }

        let signature = self.client_signature.take().expect("signature should be set");

        let mut controller_receptor = self
            .messenger
            .message(
                setting_handler::Payload::Command(Command::ChangeState(State::Teardown)).into(),
                Audience::Messenger(signature),
            )
            .send();

        // Wait for the teardown phase to be over before continuing.
        if controller_receptor.next().await != Some(MessageEvent::Status(Status::Received)) {
            fx_log_err!("Failed to tear down {:?} controller", self.setting_type);
        }

        // This ensures that the client event loop for the corresponding controller is
        // properly stopped. Without this, the client event loop will run forever.
        self.delegate.delete(signature);

        publish!(self, event::handler::Event::Teardown);
    }
}
