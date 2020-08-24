// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    async_trait::async_trait,
    fidl::endpoints::{create_request_stream, ClientEnd, ServerEnd, ServiceMarker},
    fidl::Channel,
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client::{connect_channel_to_service, connect_to_service},
    fuchsia_zircon as zx,
    futures::{
        channel::oneshot,
        future::{AbortHandle, Abortable, BoxFuture, TryFutureExt},
        lock::Mutex,
        StreamExt,
    },
    std::{convert::TryFrom, sync::Arc},
    thiserror::Error,
};

/// Ordering is used by `EventSource::expect_events`to determine if it should allow events to be
/// verified in any order or only in the order specified by the test.
pub enum Ordering {
    Ordered,
    Unordered,
}

/// Returns the string name for the given `event_type`
fn event_name(event_type: &fsys::EventType) -> String {
    match event_type {
        fsys::EventType::CapabilityReady => "capability_ready",
        fsys::EventType::CapabilityRequested => "capability_requested",
        fsys::EventType::CapabilityRouted => "capability_routed",
        fsys::EventType::Destroyed => "destroyed",
        fsys::EventType::Discovered => "discovered",
        fsys::EventType::MarkedForDestruction => "",
        fsys::EventType::Resolved => "resolved",
        fsys::EventType::Started => "started",
        fsys::EventType::Stopped => "stopped",
        fsys::EventType::Running => "running",
    }
    .to_string()
}

/// A wrapper over the BlockingEventSource FIDL proxy.
/// Provides all of the FIDL methods with a cleaner, simpler interface.
/// Refer to events.fidl for a detailed description of this protocol.
pub struct EventSource {
    proxy: fsys::BlockingEventSourceProxy,
}

impl EventSource {
    /// Connects to the BlockingEventSource service at its default location
    /// The default location is presumably "/svc/fuchsia.sys2.BlockingEventSource"
    pub fn new_sync() -> Result<Self, Error> {
        let proxy = connect_to_service::<fsys::BlockingEventSourceMarker>()
            .context("could not connect to BlockingEventSource service")?;
        Ok(EventSource::from_proxy(proxy))
    }

    pub fn new_async() -> Result<Self, Error> {
        let (proxy, server_end) =
            fidl::endpoints::create_proxy::<fsys::BlockingEventSourceMarker>()?;
        connect_channel_to_service::<fsys::EventSourceMarker>(server_end.into_channel())
            .context("could not connect to EventSource service")?;
        Ok(EventSource::from_proxy(proxy))
    }

    /// Wraps a provided BlockingEventSource proxy
    pub fn from_proxy(proxy: fsys::BlockingEventSourceProxy) -> Self {
        Self { proxy }
    }

    pub async fn subscribe(&self, event_names: Vec<impl AsRef<str>>) -> Result<EventStream, Error> {
        let (client_end, stream) = create_request_stream::<fsys::EventStreamMarker>()?;
        self.proxy
            .subscribe(&mut event_names.iter().map(|e| e.as_ref()), client_end)
            .await?
            .map_err(|error| format_err!("Error: {:?}", error))?;
        Ok(EventStream::new(stream))
    }

    pub async fn record_events(
        &self,
        event_names: Vec<impl AsRef<str>>,
    ) -> Result<EventLog, Error> {
        let event_stream = self.subscribe(event_names).await?;
        Ok(EventLog::new(event_stream))
    }

    /// This is a convenience method that subscribes to the `CapabilityRouted`
    /// event, spawns a new task, and injects the service provided by the
    /// injector if requested by the event. A `matcher` can be optionally
    /// supplied allowing the caller to choose the target that gets the injected
    /// capability. Otherwise, the injector will be used for all targets using a
    /// matching capability.
    pub async fn install_injector<I: 'static>(
        &self,
        injector: Arc<I>,
        matcher: Option<EventMatcher>,
    ) -> Result<AbortHandle, Error>
    where
        I: Injector,
    {
        let matcher = matcher.unwrap_or(EventMatcher::new());
        if matcher.capability_id.is_some()
            && matcher.capability_id != Some(injector.capability_path())
        {
            return Err(format_err!("Unexpected Capability Id provided"));
        }
        let matcher = matcher.expect_capability_id(injector.capability_path());
        let mut event_stream = self.subscribe(vec![CapabilityRouted::NAME]).await?;
        let (abort_handle, abort_registration) = AbortHandle::new_pair();

        fasync::Task::spawn(
            Abortable::new(
                async move {
                    loop {
                        let event = match event_stream
                            .wait_until_exact::<CapabilityRouted>(matcher.clone())
                            .await
                        {
                            Ok(e) => e,
                            Err(e) => match e.downcast::<EventStreamError>() {
                                Ok(EventStreamError::StreamClosed) => return,
                                Err(e) => panic!("Unknown error! {:?}", e),
                            },
                        };

                        if event.result.is_ok() {
                            event.inject(injector.clone()).await.expect("injection failed");
                        }
                    }
                },
                abort_registration,
            )
            .unwrap_or_else(|_| ()),
        )
        .detach();
        Ok(abort_handle)
    }

    // This is a convenience method that subscribes to the `CapabilityRouted` event,
    // spawns a new task, and interposes the service provided by the interposer if requested
    // by the event.
    pub async fn install_interposer<I: 'static>(
        &self,
        interposer: Arc<I>,
    ) -> Result<AbortHandle, Error>
    where
        I: Interposer,
    {
        let mut event_stream = self.subscribe(vec![CapabilityRouted::NAME]).await?;
        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        fasync::Task::spawn(
            Abortable::new(
                async move {
                    loop {
                        let event = match event_stream.wait_until_type::<CapabilityRouted>().await {
                            Ok(e) => e,
                            Err(e) => match e.downcast::<EventStreamError>() {
                                Ok(EventStreamError::StreamClosed) => return,
                                Err(e) => panic!("Unknown error! {:?}", e),
                            },
                        };

                        if let Ok(payload) = &event.result {
                            if payload.capability_id == interposer.capability_path() {
                                event
                                    .interpose(interposer.clone())
                                    .await
                                    .expect("injection failed");
                            }
                        }
                    }
                },
                abort_registration,
            )
            .unwrap_or_else(|_| (())),
        )
        .detach();
        Ok(abort_handle)
    }

    pub async fn start_component_tree(&self) {
        self.proxy.start_component_tree().await.unwrap();
    }

    /// Verifies that the given events are received from the event system. Based on the vector of
    /// events passed in this function will subscribe to an event stream with the relevant event
    /// types and verify that the correct events for the component are received.
    ///
    /// # Parameters
    /// - `ordering`: Tells `expect_events` whether it should verify that the events match the order
    ///               provided in the vector or if they can be in any order.
    /// - `expected_events`: A Vector of `EventDescriptor`s that represent which events are expected
    ///
    /// # Notes
    /// This function only listens for events directed at a component, not its Realm so any events
    /// with a target_moniker of "." are ignored.
    pub async fn expect_events<'a>(
        &'a self,
        ordering: Ordering,
        expected_events: Vec<EventMatcher>,
    ) -> Result<BoxFuture<'a, Result<(), Error>>, Error> {
        let mut event_names = vec![];
        for event in &expected_events {
            if let Some(event_type) = &event.event_type {
                event_names.push(event_name(&event_type));
            } else {
                return Err(format_err!("No event name or type set for matcher {:?}", event));
            }
        }
        event_names.dedup();

        let mut event_stream = self.subscribe(event_names).await?;
        let (tx, rx) = oneshot::channel();
        fasync::Task::spawn(async move {
            let res = event_stream.validate(ordering, expected_events).await;
            tx.send(res).expect("Unable to send result");
        })
        .detach();

        Ok(Box::pin(async move { rx.await? }))
    }
}

pub struct EventStream {
    stream: fsys::EventStreamRequestStream,
}

#[derive(Debug, Error, Clone)]
pub enum EventStreamError {
    #[error("Stream terminated unexpectedly")]
    StreamClosed,
}

impl EventStream {
    pub fn new(stream: fsys::EventStreamRequestStream) -> Self {
        Self { stream }
    }

    pub async fn next(&mut self) -> Result<fsys::Event, EventStreamError> {
        match self.stream.next().await {
            Some(Ok(fsys::EventStreamRequest::OnEvent { event, .. })) => Ok(event),
            Some(_) => Err(EventStreamError::StreamClosed),
            None => Err(EventStreamError::StreamClosed),
        }
    }

    /// Expects the next event to be of a particular type.
    /// Returns the casted type if successful and an error otherwise.
    pub async fn expect_type<T: Event>(&mut self) -> Result<T, Error> {
        let event = self.next().await?;
        T::from_fidl(event)
    }

    /// Expects the next event to be of a particular type and moniker and
    /// have a payload.  Returns the casted type if an error event is received.
    /// Otherwise, it crashes.
    pub async fn expect_error<T: Event>(&mut self, expected_event_matcher: EventMatcher) -> T {
        let event = self.next().await.unwrap();
        let descriptor = EventDescriptor::try_from(&event).unwrap();
        let event = T::from_fidl(event).unwrap();
        assert!(expected_event_matcher.matches(&descriptor));
        assert!(event.is_err());
        event
    }

    /// Expects the next event to be of a particular type and moniker.
    /// Returns the casted type if successful and an error otherwise.
    pub async fn expect_exact<T: Event>(&mut self, expected_event_matcher: EventMatcher) -> T {
        let event = self.next().await.unwrap();
        let descriptor = EventDescriptor::try_from(&event).unwrap();
        let event = T::from_fidl(event).unwrap();
        assert!(expected_event_matcher.matches(&descriptor));
        assert!(event.is_ok());
        event
    }

    /// Waits for an event of a particular type.
    /// Implicitly resumes all other events.
    /// Returns the casted type if successful and an error otherwise.
    pub async fn wait_until_type<T: Event>(&mut self) -> Result<T, Error> {
        loop {
            let event = self.next().await?;
            if let Ok(event) = T::from_fidl(event) {
                return Ok(event);
            }
        }
    }

    /// Waits for an event of a particular type and target moniker.
    /// Implicitly resumes all other events.
    /// Returns the casted type if successful and an error otherwise.
    pub async fn wait_until_exact<T: Event>(
        &mut self,
        expected_event_matcher: EventMatcher,
    ) -> Result<T, Error> {
        let expected_event_matcher = expected_event_matcher.expect_type::<T>();
        loop {
            let event = self.next().await?;
            let descriptor = EventDescriptor::try_from(&event)?;
            eprintln!("Received descriptor {:?}", descriptor);
            if let Ok(event) = T::from_fidl(event) {
                if expected_event_matcher.matches(&descriptor) {
                    return Ok(event);
                }
            }
        }
    }

    /// Validate that the events in `expected_events` are received.
    /// # Parameters
    /// - `ordering`: Determines whether events must arrive in the same order they appear in the
    ///               Vec or may arrive in any order
    /// - `expected_events`: A Vector of `EventDescriptor`s that represent which events are expected
    ///
    /// # Notes
    /// This function only listens for events directed at a component, not its Realm so any events
    /// with a target_moniker of "." are ignored.
    pub async fn validate(
        &mut self,
        ordering: Ordering,
        mut expected_events: Vec<EventMatcher>,
    ) -> Result<(), Error> {
        while let Ok(event) = self.next().await {
            let actual_event = EventDescriptor::try_from(&event)?;
            // Skip events directed at the Realm insted of the component.
            if actual_event.target_moniker != Some(".".to_string()) {
                let expected_event;
                match ordering {
                    Ordering::Ordered => expected_event = expected_events.remove(0),
                    Ordering::Unordered => {
                        if let Some((index, _)) = expected_events
                            .iter()
                            .enumerate()
                            .find(|&event| event.1.matches(&actual_event))
                        {
                            expected_event = expected_events.remove(index);
                        } else {
                            panic!("Failed to find event: {:?}", actual_event);
                        }
                    }
                }

                assert!(expected_event.matches(&actual_event));
                if expected_events.is_empty() {
                    break;
                }
            }
        }

        Ok(())
    }
}

/// Common features of any event - event type, target moniker, conversion function
pub trait Event: Handler {
    const TYPE: fsys::EventType;
    const NAME: &'static str;

    fn target_moniker(&self) -> &str;
    fn component_url(&self) -> &str;
    fn timestamp(&self) -> zx::Time;
    fn is_ok(&self) -> bool;
    fn is_err(&self) -> bool;
    fn from_fidl(event: fsys::Event) -> Result<Self, Error>;
}

/// Basic handler that resumes/unblocks from an Event
#[must_use = "invoke resume() otherwise component manager will be halted indefinitely!"]
#[async_trait]
pub trait Handler: Sized + Send {
    /// Returns a proxy to unblock the associated component manager task.
    /// If this is an asynchronous event, then this will return none.
    fn handler_proxy(self) -> Option<fsys::HandlerProxy>;

    #[must_use = "futures do nothing unless you await on them!"]
    async fn resume<'a>(self) -> Result<(), fidl::Error> {
        if let Some(proxy) = self.handler_proxy() {
            return proxy.resume().await;
        }
        Ok(())
    }
}

/// Implemented on fsys::Event for resuming a generic event
impl Handler for fsys::Event {
    fn handler_proxy(self) -> Option<fsys::HandlerProxy> {
        if let Some(handler) = self.handler {
            handler.into_proxy().ok()
        } else {
            None
        }
    }
}

/// An Injector allows a test to implement a protocol to be used by a component.
///
/// Client <---> Injector
#[async_trait]
pub trait Injector: Send + Sync {
    type Marker: ServiceMarker;

    /// This function will be run in a spawned task when a client attempts
    /// to connect to the service being injected. `request_stream` is a stream of
    /// requests coming in from the client.
    async fn serve(
        self: Arc<Self>,
        mut request_stream: <<Self as Injector>::Marker as ServiceMarker>::RequestStream,
    ) -> Result<(), Error>;

    fn capability_path(&self) -> String {
        format!("/svc/{}", Self::Marker::NAME)
    }
}

/// An Interposer allows a test to sit between a service and a client
/// and mutate or silently observe messages being passed between them.
///
/// Client <---> Interposer <---> Service
#[async_trait]
pub trait Interposer: Send + Sync {
    type Marker: ServiceMarker;

    /// This function will be run asynchronously when a client attempts
    /// to connect to the service being interposed. `request_stream` is a stream of
    /// requests coming in from the client and `to_service` is a proxy to the
    /// real service.
    async fn interpose(
        self: Arc<Self>,
        mut request_stream: <<Self as Interposer>::Marker as ServiceMarker>::RequestStream,
        to_service: <<Self as Interposer>::Marker as ServiceMarker>::Proxy,
    ) -> Result<(), Error>;

    fn capability_path(&self) -> String {
        format!("/svc/{}", Self::Marker::NAME)
    }
}

/// A protocol that allows routing capabilities over FIDL.
#[async_trait]
pub trait RoutingProtocol {
    fn protocol_proxy(&self) -> Option<fsys::RoutingProtocolProxy>;

    #[must_use = "futures do nothing unless you await on them!"]
    async fn set_provider(
        &self,
        client_end: ClientEnd<fsys::CapabilityProviderMarker>,
    ) -> Result<(), fidl::Error> {
        if let Some(proxy) = self.protocol_proxy() {
            return proxy.set_provider(client_end).await;
        }
        Ok(())
    }

    /// Set an Injector for the given capability.
    #[must_use = "futures do nothing unless you await on them!"]
    async fn inject<I: 'static>(&self, injector: Arc<I>) -> Result<(), fidl::Error>
    where
        I: Injector,
    {
        // Create the CapabilityProvider channel
        let (provider_client_end, mut provider_capability_stream) =
            create_request_stream::<fsys::CapabilityProviderMarker>()
                .expect("Could not create request stream for CapabilityProvider");

        // Wait for an Open request on the CapabilityProvider channel
        fasync::Task::spawn(async move {
            if let Some(Ok(fsys::CapabilityProviderRequest::Open { server_end, responder })) =
                provider_capability_stream.next().await
            {
                // Unblock component manager from the open request
                responder.send().expect("Could not respond to CapabilityProvider::Open");

                // Create the stream for the Client <---> Interposer connection
                let stream = ServerEnd::<I::Marker>::new(server_end)
                    .into_stream()
                    .expect("could not convert channel into stream");

                injector.serve(stream).await.expect("Injection failed");
            } else {
                panic!(
                    "Failed to inject capability! CapabilityProvider was not able to invoke Open"
                );
            }
        })
        .detach();

        // Send the client end of the CapabilityProvider protocol
        if let Some(protocol_proxy) = self.protocol_proxy() {
            return protocol_proxy.set_provider(provider_client_end).await;
        }
        Ok(())
    }

    /// Set an Interposer for the given capability.
    #[must_use = "futures do nothing unless you await on them!"]
    async fn interpose<I: 'static>(&self, interposer: Arc<I>) -> Result<(), fidl::Error>
    where
        I: Interposer,
    {
        // Create the Interposer <---> Server channel
        let (client_end, server_end) = Channel::create().expect("could not create channel");

        // Create the CapabilityProvider channel
        let (provider_client_end, mut provider_capability_stream) =
            create_request_stream::<fsys::CapabilityProviderMarker>()
                .expect("Could not create request stream for CapabilityProvider");

        // Wait for an Open request on the CapabilityProvider channel
        fasync::Task::spawn(async move {
            if let Some(Ok(fsys::CapabilityProviderRequest::Open { server_end, responder })) =
                provider_capability_stream.next().await
            {
                // Unblock component manager from the open request
                responder.send().expect("Could not respond to CapabilityProvider::Open");

                // Create the proxy for the Interposer <---> Server connection
                let proxy = ClientEnd::<I::Marker>::new(client_end)
                    .into_proxy()
                    .expect("could not convert into proxy");

                // Create the stream for the Client <---> Interposer connection
                let stream = ServerEnd::<I::Marker>::new(server_end)
                    .into_stream()
                    .expect("could not convert channel into stream");

                // Start interposing!
                interposer.interpose(stream, proxy).await.expect("Interposition failed");
            } else {
                panic!("Failed to interpose! CapabilityProvider was not able to invoke Open");
            }
        })
        .detach();

        // Replace the existing provider and open it with the
        // server end of the Interposer <---> Server channel.
        if let Some(protocol_proxy) = self.protocol_proxy() {
            return protocol_proxy.replace_and_open(provider_client_end, server_end).await;
        }
        Ok(())
    }
}

#[derive(Clone, Debug)]
pub struct EventMatcher {
    event_type: Option<fsys::EventType>,
    capability_id: Option<String>,
    target_moniker: Option<String>,
}

impl EventMatcher {
    /// Creates a new `EventDescriptor` with the given `event_type`.
    /// The rest of the fields are unset.
    pub fn new() -> Self {
        Self { event_type: None, target_moniker: None, capability_id: None }
    }

    pub fn expect_type<T: Event>(mut self) -> Self {
        self.event_type = Some(T::TYPE);
        self
    }

    /// The expected target moniker.
    pub fn expect_moniker(mut self, moniker: impl Into<String>) -> Self {
        self.target_moniker = Some(moniker.into());
        self
    }

    /// The expected capability id.
    pub fn expect_capability_id(mut self, capability_id: impl Into<String>) -> Self {
        self.capability_id = Some(capability_id.into());
        self
    }

    pub fn matches(&self, other: &EventDescriptor) -> bool {
        let matches_event_type = match (&self.event_type, &other.event_type) {
            (Some(event_type), Some(other_event_type)) => event_type == other_event_type,
            (Some(_), None) => false,
            (None, _) => true,
        };

        let matches_moniker = match (&self.target_moniker, &other.target_moniker) {
            (Some(moniker), Some(other_moniker)) => {
                if moniker.ends_with("*") {
                    let index = moniker.rfind('*').unwrap();
                    let prefix = &moniker[..index];
                    other_moniker.starts_with(prefix)
                } else {
                    moniker == other_moniker
                }
            }
            (Some(_), None) => false,
            (None, _) => true,
        };

        let matches_capability_id = match (&self.capability_id, &other.capability_id) {
            (Some(capability_id), Some(other_capability_id)) => {
                capability_id == other_capability_id
            }
            (Some(_), None) => false,
            (None, _) => true,
        };

        matches_event_type && matches_moniker && matches_capability_id
    }
}

#[derive(Clone, Eq, PartialOrd, Ord, Debug)]
pub struct EventDescriptor {
    event_type: Option<fsys::EventType>,
    capability_id: Option<String>,
    target_moniker: Option<String>,
}

/// This implementation of PartialEq allows comparison between `EventDescriptor`s when the order in
/// which components are launched can't be guaranteed. `target_moniker` can end in a wild card in
/// `target_moniker` to match by prefix instead of requiring a full match. For example, a test can
/// use the the following `EventDescriptor`
///
/// ```
/// EventDescriptor::new<CapabilityRouted>()
///     .expect_moniker("./session:session:*")
///     .expect_capability_id("elf")
/// ```
///
/// to match another `EventDescriptor` with the target_moniker of "./session:session:1" or
/// "./session:session:2". If both target_monikers have instance ids they are still compared as
/// expected.
///
/// This also works (wild card for name and id):
///
/// ```
/// EventDescriptor::new<CapabilityRouted>()
///     .expect_moniker("./session:*")
///     .expect_capability_id("elf")
/// ```
impl PartialEq<EventDescriptor> for EventDescriptor {
    fn eq(&self, other: &EventDescriptor) -> bool {
        let targets_match = match (&self.target_moniker, &other.target_moniker) {
            (Some(self_moniker), Some(other_moniker)) => {
                if self_moniker.ends_with("*") {
                    let index = self_moniker.rfind('*').unwrap();
                    let prefix = &self_moniker[..index];
                    other_moniker.starts_with(prefix)
                } else {
                    self_moniker == other_moniker
                }
            }
            (self_moniker, other_moniker) => self_moniker == other_moniker,
        };

        self.event_type == other.event_type
            && self.capability_id == other.capability_id
            && targets_match
    }
}

#[derive(Debug)]
struct ComponentDescriptor {
    component_url: String,
    moniker: String,
}

impl TryFrom<fsys::ComponentDescriptor> for ComponentDescriptor {
    type Error = anyhow::Error;

    fn try_from(descriptor: fsys::ComponentDescriptor) -> Result<Self, Self::Error> {
        let component_url = descriptor.component_url.ok_or(format_err!("No component url"))?;
        let moniker = descriptor.moniker.ok_or(format_err!("No moniker"))?;
        Ok(ComponentDescriptor { component_url, moniker })
    }
}

impl TryFrom<&fsys::Event> for EventDescriptor {
    type Error = anyhow::Error;

    fn try_from(event: &fsys::Event) -> Result<Self, Self::Error> {
        // Construct the EventDescriptor from the Event
        let event_type = Some(event.event_type.ok_or(format_err!("No event type"))?);
        let target_moniker =
            event.descriptor.as_ref().and_then(|descriptor| descriptor.moniker.clone());
        let capability_id = match &event.event_result {
            Some(fsys::EventResult::Payload(fsys::EventPayload::CapabilityReady(
                fsys::CapabilityReadyPayload { path, .. },
            ))) => path.clone(),
            Some(fsys::EventResult::Payload(fsys::EventPayload::CapabilityRequested(
                fsys::CapabilityRequestedPayload { path, .. },
            ))) => path.clone(),
            Some(fsys::EventResult::Payload(fsys::EventPayload::CapabilityRouted(
                fsys::CapabilityRoutedPayload { capability_id, .. },
            ))) => capability_id.clone(),
            Some(fsys::EventResult::Error(fsys::EventError {
                error_payload:
                    Some(fsys::EventErrorPayload::CapabilityReady(fsys::CapabilityReadyError {
                        path,
                        ..
                    })),
                ..
            })) => path.clone(),
            Some(fsys::EventResult::Error(fsys::EventError {
                error_payload:
                    Some(fsys::EventErrorPayload::CapabilityRequested(fsys::CapabilityRequestedError {
                        path,
                        ..
                    })),
                ..
            })) => path.clone(),
            Some(fsys::EventResult::Error(fsys::EventError {
                error_payload:
                    Some(fsys::EventErrorPayload::CapabilityRouted(fsys::CapabilityRoutedError {
                        capability_id,
                        ..
                    })),
                ..
            })) => capability_id.clone(),
            _ => None,
        };

        Ok(EventDescriptor { event_type, target_moniker, capability_id })
    }
}

/// Records events from an EventStream, allowing them to be
/// flushed out into a vector at a later point in time.
pub struct EventLog {
    recorded_events: Arc<Mutex<Vec<EventDescriptor>>>,
    abort_handle: AbortHandle,
}

impl EventLog {
    fn new(mut event_stream: EventStream) -> Self {
        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        let recorded_events = Arc::new(Mutex::new(vec![]));
        {
            // Start an async task that records events from the event_stream
            let recorded_events = recorded_events.clone();
            fasync::Task::spawn(
                Abortable::new(
                    async move {
                        loop {
                            // Get the next event from the event_stream
                            let event = event_stream
                                .next()
                                .await
                                .expect("Failed to get next event from EventStreamSync");

                            // Construct the EventDescriptor from the Event
                            let recorded_event = EventDescriptor::try_from(&event)
                                .expect("Failed to convert Event to EventDescriptor");

                            // Insert the event into the list
                            {
                                let mut recorded_events = recorded_events.lock().await;
                                recorded_events.push(recorded_event);
                            }
                        }
                    },
                    abort_registration,
                )
                .unwrap_or_else(|_| ()),
            )
            .detach();
        }
        Self { recorded_events, abort_handle }
    }

    pub async fn flush(&self) -> Vec<EventDescriptor> {
        // Lock and flush out all events from the vector
        let mut recorded_events = self.recorded_events.lock().await;
        recorded_events.drain(..).collect()
    }
}

impl Drop for EventLog {
    fn drop(&mut self) {
        self.abort_handle.abort();
    }
}

#[derive(Debug, PartialEq, Eq)]
pub struct EventError {
    pub description: String,
}

/// The macro defined below will automatically create event classes corresponding
/// to their events.fidl and hooks.rs counterparts. Every event class implements
/// the Event and Handler traits. These minimum requirements allow every event to
/// be handled by the events client library.

/// Creates an event class based on event type and an optional payload
/// * event_type -> FIDL name for event type
/// * payload -> If an event has a payload, describe the additional params:
///   * name -> FIDL name for the payload
///   * data -> If a payload contains data items, describe the additional params:
///     * name -> FIDL name for the data item
///     * ty -> Rust type for the data item
///   * client_protocols -> If a payload contains client-side protocols, describe
///     the additional params:
///     * name -> FIDL name for the protocol
///     * ty -> Rust type for the protocol proxy
///   * server_protocols -> If a payload contains server-side protocols, describe
///     the additional params:
///     * name -> FIDL name for the protocol
// TODO(fxb/53937): This marco is getting complicated. Consider replacing it
//                  with a procedural macro.
macro_rules! create_event {
    // Entry points
    (
        event_type: $event_type:ident,
        event_name: $event_name:ident,
        payload: {
            data: {$(
                {
                    name: $data_name:ident,
                    ty: $data_ty:ty,
                }
            )*},
            client_protocols: {$(
                {
                    name: $client_protocol_name:ident,
                    ty: $client_protocol_ty:ty,
                }
            )*},
            server_protocols: {$(
                {
                    name: $server_protocol_name:ident,
                }
            )*},
        },
        error_payload: {
            $(
                {
                    name: $error_data_name:ident,
                    ty: $error_data_ty:ty,
                }
            )*
        }
    ) => {
        paste::item! {
            pub struct [<$event_type Payload>] {
                $(pub $client_protocol_name: $client_protocol_ty,)*
                $(pub $server_protocol_name: Option<zx::Channel>,)*
                $(pub $data_name: $data_ty,)*
            }

            #[derive(Debug)]
            pub struct [<$event_type Error>] {
                $(pub $error_data_name: $error_data_ty,)*
                pub description: String,
            }

            pub struct $event_type {
                descriptor: ComponentDescriptor,
                handler: Option<fsys::HandlerProxy>,
                timestamp: zx::Time,
                pub result: Result<[<$event_type Payload>], [<$event_type Error>]>,
            }

            impl $event_type {
                pub fn unwrap_payload<'a>(&'a self) -> &'a [<$event_type Payload>] {
                    self.result.as_ref().unwrap()
                }

                $(
                    pub fn [<take_ $server_protocol_name>]<T: ServiceMarker>(&mut self)
                            -> Option<T::RequestStream> {
                        self.result.as_mut()
                            .ok()
                            .and_then(|payload| payload.$server_protocol_name.take())
                            .and_then(|channel| {
                                let server_end = ServerEnd::<T>::new(channel);
                                server_end.into_stream().ok()
                            })
                    }
                )*
            }

            impl Event for $event_type {
                const TYPE: fsys::EventType = fsys::EventType::$event_type;
                const NAME: &'static str = stringify!($event_name);

                fn target_moniker(&self) -> &str {
                    &self.descriptor.moniker
                }

                fn component_url(&self) -> &str {
                    &self.descriptor.component_url
                }

                fn timestamp(&self) -> zx::Time {
                    self.timestamp
                }

                fn is_ok(&self) -> bool {
                    self.result.is_ok()
                }

                fn is_err(&self) -> bool {
                    self.result.is_err()
                }

                fn from_fidl(event: fsys::Event) -> Result<Self, Error> {
                    // Extract the payload from the Event object.
                    let result = match event.event_result {
                        Some(fsys::EventResult::Payload(payload)) => {
                            // This payload will be unused for event types that have no additional
                            // fields.
                            #[allow(unused)]
                            let payload = match payload {
                                fsys::EventPayload::$event_type(payload) => Ok(payload),
                                _ => Err(format_err!("Incorrect payload type")),
                            }?;

                            // Extract the additional data from the Payload object.
                            $(
                                let $data_name: $data_ty = payload.$data_name.ok_or(
                                    format_err!("Missing $data_name from $event_type object")
                                )?;
                            )*

                            // Extract the additional protocols from the Payload object.
                            $(
                                let $client_protocol_name: $client_protocol_ty = payload.$client_protocol_name.ok_or(
                                    format_err!("Missing $client_protocol_name from $event_type object")
                                )?.into_proxy()?;
                            )*
                            $(
                                let $server_protocol_name: Option<zx::Channel> =
                                    Some(payload.$server_protocol_name.ok_or(
                                        format_err!("Missing $server_protocol_name from $event_type object")
                                    )?);
                            )*

                            #[allow(dead_code)]
                            let payload = paste::expr! {
                                [<$event_type Payload>] {
                                    $($data_name,)*
                                    $($client_protocol_name,)*
                                    $($server_protocol_name,)*
                                }
                            };

                            Ok(Ok(payload))
                        },
                        Some(fsys::EventResult::Error(err)) => {
                            let description = err.description.ok_or(
                                format_err!("Missing error description")
                                )?;

                            let error_payload = err.error_payload.ok_or(
                                format_err!("Missing error_payload from EventError object")
                                )?;

                            // This error payload will be unused for event types that have no
                            // additional fields.
                            #[allow(unused)]
                            let err = match error_payload {
                                fsys::EventErrorPayload::$event_type(err) => Ok(err),
                                _ => Err(format_err!("Incorrect payload type")),
                            }?;

                            // Extract the additional data from the Payload object.
                            $(
                                let $error_data_name: $error_data_ty =
                                    err.$error_data_name.ok_or(
                                        format_err!("Missing $error_data_name from $error_payload_name object")
                                    )?;
                            )*

                            let error_payload =
                                [<$event_type Error>] { $($error_data_name,)* description };
                            Ok(Err(error_payload))
                        },
                        None => Err(format_err!("Missing event_result from Event object")),
                        _ => Err(format_err!("Unexpected event result")),
                    }?;

                    let event = {
                        // Event type in event must match what is expected
                        let event_type = event.event_type.ok_or(
                            format_err!("Missing event_type from Event object")
                        )?;

                        if event_type != Self::TYPE {
                            return Err(format_err!("Incorrect event type"));
                        }

                        let descriptor = event.descriptor
                            .ok_or(format_err!("Missing descriptor Event object"))
                            .and_then(|descriptor| ComponentDescriptor::try_from(descriptor))?;

                        let timestamp = zx::Time::from_nanos(event.timestamp.ok_or(
                            format_err!("Missing timestamp from the Event object")
                        )?);

                        let handler = event.handler.map(|h| h.into_proxy()).transpose()?;

                        $event_type { descriptor,  handler,  timestamp, result }
                    };
                    Ok(event)
                }
            }

            impl Handler for $event_type {
                fn handler_proxy(self) -> Option<fsys::HandlerProxy> {
                    self.handler
                }
            }
        }
    };
    ($event_type:ident, $event_name:ident) => {
        create_event!(event_type: $event_type, event_name: $event_name,
                      payload: {
                          data: {},
                          client_protocols: {},
                          server_protocols: {},
                      },
                      error_payload: {});
    };
}

// To create a class for an event, use the above macro here.
create_event!(Destroyed, destroyed);
create_event!(Discovered, discovered);
create_event!(MarkedForDestruction, marked_for_destruction);
create_event!(Resolved, resolved);
create_event!(Started, started);
create_event!(Stopped, stopped);
create_event!(
    event_type: Running,
    event_name: running,
    payload: {
        data: {
            {
                name: started_timestamp,
                ty: i64,
            }
        },
        client_protocols: {},
        server_protocols: {},
    },
    error_payload: {
        {
            name: started_timestamp,
            ty: i64,
        }
    }
);
create_event!(
    event_type: CapabilityReady,
    event_name: capability_ready,
    payload: {
        data: {
            {
                name: path,
                ty: String,
            }
        },
        client_protocols: {
            {
                name: node,
                ty: fio::NodeProxy,
            }
        },
        server_protocols: {},
    },
    error_payload: {
        {
            name: path,
            ty: String,
        }
    }
);
create_event!(
    event_type: CapabilityRequested,
    event_name: capability_requested,
    payload: {
        data: {
            {
                name: path,
                ty: String,
            }
        },
        client_protocols: {},
        server_protocols: {
            {
                name: capability,
            }
        },
    },
    error_payload: {
        {
            name: path,
            ty: String,
        }
    }
);
create_event!(
    event_type: CapabilityRouted,
    event_name: capability_routed,
    payload: {
        data: {
            {
                name: source,
                ty: fsys::CapabilitySource,
            }
            {
                name: capability_id,
                ty: String,
            }
        },
        client_protocols: {
            {
                name: routing_protocol,
                ty: fsys::RoutingProtocolProxy,
            }
        },
        server_protocols: {},
    },
    error_payload: {
        {
            name: capability_id,
            ty: String,
        }
    }
);

impl RoutingProtocol for CapabilityRouted {
    fn protocol_proxy(&self) -> Option<fsys::RoutingProtocolProxy> {
        self.result.as_ref().ok().map(|payload| payload.routing_protocol.clone())
    }
}
