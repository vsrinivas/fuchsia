// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    async_trait::async_trait,
    fidl::endpoints::{create_request_stream, ClientEnd, ServerEnd, ServiceMarker},
    fidl::Channel,
    fidl_fuchsia_test_events as fevents, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    futures::{
        channel::oneshot,
        future::{AbortHandle, Abortable, BoxFuture, TryFutureExt},
        lock::Mutex,
        StreamExt,
    },
    std::{convert::TryFrom, sync::Arc},
};

/// Ordering is used by `EventSource::expect_events`to determine if it should allow events to be
/// verified in any order or only in the order specified by the test.
pub enum Ordering {
    Ordered,
    Unordered,
}

/// A wrapper over the EventSourceSync FIDL proxy.
/// Provides all of the FIDL methods with a cleaner, simpler interface.
/// Refer to events.fidl for a detailed description of this protocol.
pub struct EventSource {
    proxy: fevents::EventSourceSyncProxy,
}

impl EventSource {
    /// Connects to the EventSourceSync service at its default location
    /// The default location is presumably "/svc/fuchsia.test.events.EventSourceSync"
    pub fn new() -> Result<Self, Error> {
        let proxy = connect_to_service::<fevents::EventSourceSyncMarker>()
            .context("could not connect to EventSourceSync service")?;
        Ok(EventSource::from_proxy(proxy))
    }

    /// Wraps a provided EventSourceSync proxy
    pub fn from_proxy(proxy: fevents::EventSourceSyncProxy) -> Self {
        Self { proxy }
    }

    pub async fn subscribe(
        &self,
        event_types: Vec<fevents::EventType>,
    ) -> Result<EventStream, Error> {
        let (client_end, stream) = create_request_stream::<fevents::EventStreamMarker>()?;
        self.proxy
            .subscribe(&mut event_types.into_iter(), client_end)
            .await?
            .map_err(|error| format_err!("Error: {:?}", error))?;
        Ok(EventStream::new(stream))
    }

    pub async fn record_events(
        &self,
        event_types: Vec<fevents::EventType>,
    ) -> Result<EventLog, Error> {
        let event_stream = self.subscribe(event_types).await?;
        Ok(EventLog::new(event_stream))
    }

    // This is a convenience method that subscribes to the `CapabilityRouted` event,
    // spawns a new task, and injects the service provided by the injector if requested
    // by the event.
    pub async fn install_injector<I: 'static>(&self, injector: Arc<I>) -> Result<AbortHandle, Error>
    where
        I: Injector,
    {
        let mut event_stream = self.subscribe(vec![CapabilityRouted::TYPE]).await?;
        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        fasync::spawn(
            Abortable::new(
                async move {
                    loop {
                        let event = event_stream
                            .wait_until_type::<CapabilityRouted>()
                            .await
                            .expect("Type mismatch");

                        if event.capability_id == injector.capability_path() {
                            event.inject(injector.clone()).await.expect("injection failed");
                        }

                        event.resume().await.expect("resumption failed");
                    }
                },
                abort_registration,
            )
            .unwrap_or_else(|_| ()),
        );
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
        let mut event_stream = self.subscribe(vec![CapabilityRouted::TYPE]).await?;
        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        fasync::spawn(
            Abortable::new(
                async move {
                    loop {
                        let event = event_stream
                            .wait_until_type::<CapabilityRouted>()
                            .await
                            .expect("Type mismatch");

                        if event.capability_id == interposer.capability_path() {
                            event.interpose(interposer.clone()).await.expect("injection failed");
                        }

                        event.resume().await.expect("resumption failed");
                    }
                },
                abort_registration,
            )
            .unwrap_or_else(|_| (())),
        );
        Ok(abort_handle)
    }

    pub async fn start_component_tree(&self) -> Result<(), Error> {
        self.proxy.start_component_tree().await.context("could not start component tree")?;
        Ok(())
    }

    /// Verifies that the given events are received from the event system. Based on the vector of
    /// events passed in this function will subscribe to an event stream with the relevant event
    /// types and verify that the correct events for the component are received.
    ///
    /// # Parameters
    /// - `ordering`: Tells `expect_events` whether it should verify that the events match the order
    ///               provided in the vector or if they can be in any order.
    /// - `expected_events`: A Vector of `RecordedEvent`s that represent which events are expected
    ///
    /// # Notes
    /// This function only listens for events directed at a component, not its Realm so any events
    /// with a target_moniker of "." are ignored.
    pub async fn expect_events<'a>(
        &'a self,
        ordering: Ordering,
        expected_events: Vec<RecordedEvent>,
    ) -> Result<BoxFuture<'a, Result<(), Error>>, Error> {
        let mut event_types = vec![];
        for event in &expected_events {
            event_types.push(event.event_type);
        }
        event_types.dedup();

        let mut event_stream = self.subscribe(event_types).await?;
        let (tx, rx) = oneshot::channel();
        fasync::spawn(async move {
            let res = event_stream.validate(ordering, expected_events).await;
            tx.send(res).expect("Unable to send result");
        });

        Ok(Box::pin(async move { rx.await? }))
    }
}

pub struct EventStream {
    stream: fevents::EventStreamRequestStream,
}

impl EventStream {
    pub fn new(stream: fevents::EventStreamRequestStream) -> Self {
        Self { stream }
    }

    pub async fn next(&mut self) -> Result<fevents::Event, Error> {
        if let Some(Ok(fevents::EventStreamRequest::OnEvent { event, .. })) =
            self.stream.next().await
        {
            Ok(event)
        } else {
            Err(format_err!("Stream terminated unexpectedly"))
        }
    }

    /// Expects the next event to be of a particular type.
    /// Returns the casted type if successful and an error otherwise.
    pub async fn expect_type<T: Event>(&mut self) -> Result<T, Error> {
        let event = self.next().await?;
        T::from_fidl(event)
    }

    /// Expects the next event to be of a particular type and moniker.
    /// Returns the casted type if successful and an error otherwise.
    pub async fn expect_exact<T: Event>(&mut self, expected_moniker: &str) -> Result<T, Error> {
        let event = self.expect_type::<T>().await?;
        if expected_moniker == event.target_moniker() {
            Ok(event)
        } else {
            Err(format_err!(
                "Incorrect moniker for {:?}. Expected: {}, Got: {}.",
                T::TYPE,
                expected_moniker,
                event.target_moniker()
            ))
        }
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
        expected_target_moniker: &str,
    ) -> Result<T, Error> {
        loop {
            let event = self.wait_until_type::<T>().await?;
            if event.target_moniker() == expected_target_moniker {
                return Ok(event);
            }
            event.resume().await?;
        }
    }

    /// Waits for a component capability to be routed matching a particular
    /// target moniker and capability. Implicitly resumes all other events.
    /// Returns the casted type if successful and an error otherwise.
    pub async fn wait_until_component_capability(
        &mut self,
        expected_target_moniker: &str,
        expected_capability_id: &str,
    ) -> Result<CapabilityRouted, Error> {
        loop {
            let event = self.wait_until_exact::<CapabilityRouted>(expected_target_moniker).await?;
            if expected_capability_id == event.capability_id {
                match event.source {
                    fevents::CapabilitySource::Component(_) => return Ok(event),
                    _ => {}
                }
            }
            event.resume().await?;
        }
    }

    /// Waits for a framework capability to be routed matching a particular
    /// target moniker, scope moniker and capability. Implicitly resumes all other events.
    /// Returns the casted type if successful and an error otherwise.
    pub async fn wait_until_framework_capability(
        &mut self,
        expected_target_moniker: &str,
        expected_capability_id: &str,
        expected_scope_moniker: Option<&str>,
    ) -> Result<CapabilityRouted, Error> {
        loop {
            let event = self.wait_until_exact::<CapabilityRouted>(expected_target_moniker).await?;

            // If the capability ID matches and the capability source is framework
            // with a matching optional scope moniker, then return the event.
            if expected_capability_id == event.capability_id {
                match &event.source {
                    fevents::CapabilitySource::Framework(fevents::FrameworkCapability {
                        scope_moniker,
                        ..
                    }) if scope_moniker.as_ref().map(|s| s.as_str()) == expected_scope_moniker => {
                        return Ok(event)
                    }
                    _ => {}
                }
            }

            event.resume().await?;
        }
    }

    /// Validate that the events in `expected_events` are received.
    /// # Parameters
    /// - `ordering`: Determines whether events must arrive in the same order they appear in the
    ///               Vec or may arrive in any order
    /// - `expected_events`: A Vector of `RecordedEvent`s that represent which events are expected
    ///
    /// # Notes
    /// This function only listens for events directed at a component, not its Realm so any events
    /// with a target_moniker of "." are ignored.
    pub async fn validate(
        &mut self,
        ordering: Ordering,
        mut expected_events: Vec<RecordedEvent>,
    ) -> Result<(), Error> {
        while let Ok(event) = self.next().await {
            let recorded_event = RecordedEvent::try_from(&event)?;
            // Skip events directed at the Realm insted of the component.
            if recorded_event.target_moniker != "." {
                let expected_event;
                match ordering {
                    Ordering::Ordered => expected_event = expected_events.remove(0),
                    Ordering::Unordered => {
                        if let Some((index, _)) = expected_events
                            .iter()
                            .enumerate()
                            .find(|&event| event.1 == &recorded_event)
                        {
                            expected_event = expected_events.remove(index);
                        } else {
                            panic!("Failed to find event: {:?}", recorded_event);
                        }
                    }
                }

                assert_eq!(expected_event, recorded_event);
                event.resume().await?;
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
    const TYPE: fevents::EventType;
    fn target_moniker(&self) -> &str;
    fn from_fidl(event: fevents::Event) -> Result<Self, Error>;
}

/// Basic handler that resumes/unblocks from an Event
#[must_use = "invoke resume() otherwise component manager will be halted indefinitely!"]
pub trait Handler: Sized {
    fn handler_proxy(self) -> fevents::HandlerProxy;

    #[must_use = "futures do nothing unless you await on them!"]
    fn resume<'a>(self) -> BoxFuture<'a, Result<(), fidl::Error>> {
        let proxy = self.handler_proxy();
        Box::pin(async move { proxy.resume().await })
    }
}

/// Implemented on fevents::Event for resuming a generic event
impl Handler for fevents::Event {
    fn handler_proxy(self) -> fevents::HandlerProxy {
        self.handler
            .expect("Could not find handler in Event object")
            .into_proxy()
            .expect("Could not convert into proxy")
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
    fn protocol_proxy(&self) -> fevents::RoutingProtocolProxy;

    #[must_use = "futures do nothing unless you await on them!"]
    async fn set_provider(
        &self,
        client_end: ClientEnd<fevents::CapabilityProviderMarker>,
    ) -> Result<(), fidl::Error> {
        let proxy = self.protocol_proxy();
        proxy.set_provider(client_end).await
    }

    /// Set an Injector for the given capability.
    #[must_use = "futures do nothing unless you await on them!"]
    async fn inject<I: 'static>(&self, injector: Arc<I>) -> Result<(), fidl::Error>
    where
        I: Injector,
    {
        // Create the CapabilityProvider channel
        let (provider_client_end, mut provider_capability_stream) =
            create_request_stream::<fevents::CapabilityProviderMarker>()
                .expect("Could not create request stream for CapabilityProvider");

        // Wait for an Open request on the CapabilityProvider channel
        fasync::spawn(async move {
            if let Some(Ok(fevents::CapabilityProviderRequest::Open { server_end, responder })) =
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
        });

        // Send the client end of the CapabilityProvider protocol
        self.protocol_proxy().set_provider(provider_client_end).await
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
            create_request_stream::<fevents::CapabilityProviderMarker>()
                .expect("Could not create request stream for CapabilityProvider");

        // Wait for an Open request on the CapabilityProvider channel
        fasync::spawn(async move {
            if let Some(Ok(fevents::CapabilityProviderRequest::Open { server_end, responder })) =
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
        });

        // Replace the existing provider and open it with the
        // server end of the Interposer <---> Server channel.
        self.protocol_proxy().replace_and_open(provider_client_end, server_end).await
    }
}

/// Describes an event recorded by the EventLog
#[derive(Clone, Eq, PartialOrd, Ord, Debug)]
pub struct RecordedEvent {
    pub event_type: fevents::EventType,
    pub target_moniker: String,
    pub capability_id: Option<String>,
}

/// This implementation of PartialEq allows comparison between `RecordedEvent`s when the order in
/// which components are launched can't be guaranteed. `target_moniker` can end in a wild card in
/// `target_moniker` to match by prefix instead of requiring a full match. For example, a test can
/// use the the following `RecordedEvent`
///
/// ```
/// RecordedEvent {
///    event_type: EventType::CapabilityRouted,
///    target_moniker: "./session:session:*".to_string(),
///    capability_id: Some("elf".to_string()),
/// },
/// ```
///
/// to match another `RecordedEvent` with the target_moniker of "./session:session:1" or
/// "./session:session:2". If both target_monikers have instance ids they are still compared as
/// expected.
///
/// This also works (wild card for name and id):
///
/// ```
/// RecordedEvent {
///    event_type: EventType::RouteCapability,
///    target_moniker: "./session:*".to_string(),
///    capability_id: Some("elf".to_string()),
/// },
/// ```
impl PartialEq<RecordedEvent> for RecordedEvent {
    fn eq(&self, other: &RecordedEvent) -> bool {
        let targets_match = if self.target_moniker.ends_with("*") {
            let index = self.target_moniker.rfind('*').unwrap();
            let prefix = &self.target_moniker[..index];
            other.target_moniker.starts_with(prefix)
        } else {
            self.target_moniker == other.target_moniker
        };

        self.event_type == other.event_type
            && self.capability_id == other.capability_id
            && targets_match
    }
}

impl TryFrom<&fevents::Event> for RecordedEvent {
    type Error = anyhow::Error;

    fn try_from(event: &fevents::Event) -> Result<Self, Self::Error> {
        // Construct the RecordedEvent from the Event
        let event_type = event.event_type.ok_or_else(|| format_err!("No event type"))?;
        let target_moniker =
            event.target_moniker.as_ref().ok_or_else(|| format_err!("No target moniker"))?.clone();
        let capability_id = if let Some(event_payload) = event.event_payload.as_ref() {
            event_payload
                .routing_payload
                .as_ref()
                .ok_or_else(|| format_err!("No event payload"))?
                .capability_id
                .to_owned()
        } else {
            None
        };

        Ok(RecordedEvent { event_type, target_moniker, capability_id })
    }
}

/// Records events from an EventStream, allowing them to be
/// flushed out into a vector at a later point in time.
pub struct EventLog {
    recorded_events: Arc<Mutex<Vec<RecordedEvent>>>,
    abort_handle: AbortHandle,
}

impl EventLog {
    fn new(mut event_stream: EventStream) -> Self {
        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        let recorded_events = Arc::new(Mutex::new(vec![]));
        {
            // Start an async task that records events from the event_stream
            let recorded_events = recorded_events.clone();
            fasync::spawn(
                Abortable::new(
                    async move {
                        loop {
                            // Get the next event from the event_stream
                            let event = event_stream
                                .next()
                                .await
                                .expect("Failed to get next event from EventStreamSync");

                            // Construct the RecordedEvent from the Event
                            let recorded_event = RecordedEvent::try_from(&event)
                                .expect("Failed to convert Event to RecordedEvent");

                            // Insert the event into the list
                            {
                                let mut recorded_events = recorded_events.lock().await;
                                recorded_events.push(recorded_event);
                            }

                            // Resume from the event
                            event.resume().await.expect("Could not resume from event");
                        }
                    },
                    abort_registration,
                )
                .unwrap_or_else(|_| ()),
            );
        }
        Self { recorded_events, abort_handle }
    }

    pub async fn flush(&self) -> Vec<RecordedEvent> {
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
///   * protocol -> If a payload contains protocols, describe the additional params:
///     * name -> FIDL name for the protocol
///     * ty -> Rust type for the protocol proxy
///     * trait_name -> Rust name for the trait implementing this protocol
macro_rules! create_event {
    (
        event_type: $event_type:ident,
        payload: {
            name: $payload_name:ident,
            data: {$(
                {
                    name: $data_name:ident,
                    ty: $data_ty:ty,
                }
            )*},
            protocols: {$(
                {
                    name: $protocol_name:ident,
                    ty: $protocol_ty:ty,
                    trait_name: $protocol_trait_name:ident,
                }
            )*},
        }
    ) => {
        pub struct $event_type {
            target_moniker: String,
            handler: fevents::HandlerProxy,
            $($protocol_name: $protocol_ty,)*
            $(pub $data_name: $data_ty,)*
        }

        impl Event for $event_type {
            const TYPE: fevents::EventType = fevents::EventType::$event_type;

            fn target_moniker(&self) -> &str {
                &self.target_moniker
            }

            fn from_fidl(event: fevents::Event) -> Result<Self, Error> {
                // Event type in event must match what is expected
                let event_type = event.event_type.ok_or(
                    format_err!("Missing event_type from Event object")
                )?;
                if event_type != Self::TYPE {
                    return Err(format_err!("Incorrect event type"));
                }
                let target_moniker = event.target_moniker.ok_or(
                    format_err!("Missing target_moniker from Event object")
                )?;
                let handler = event.handler.ok_or(
                    format_err!("Missing handler from Event object")
                )?.into_proxy()?;

                // Extract the payload from the Event object.
                let event_payload = event.event_payload.ok_or(
                    format_err!("Missing event_payload from Event object")
                )?;
                let $payload_name = event_payload.$payload_name.ok_or(
                    format_err!("Missing $payload_name from EventPayload object")
                )?;

                // Extract the additional data from the Payload object.
                $(
                    let $data_name: $data_ty = $payload_name.$data_name.ok_or(
                        format_err!("Missing $data_name from $payload_name object")
                    )?;
                )*

                // Extract the additional protocols from the Payload object.
                $(
                    let $protocol_name: $protocol_ty = $payload_name.$protocol_name.ok_or(
                        format_err!("Missing $protocol_name from $payload_name object")
                    )?.into_proxy()?;
                )*

                Ok($event_type { target_moniker, handler, $($data_name,)* $($protocol_name,)* })
            }
        }

        impl Handler for $event_type {
            fn handler_proxy(self) -> fevents::HandlerProxy {
                self.handler
            }
        }

        $(
            impl $protocol_trait_name for $event_type {
                fn protocol_proxy(&self) -> $protocol_ty {
                    self.$protocol_name.clone()
                }
            }
        )*
    };
    ($event_type:ident) => {
        pub struct $event_type {
            target_moniker: String,
            handler: fevents::HandlerProxy,
        }

        impl Event for $event_type {
            const TYPE: fevents::EventType = fevents::EventType::$event_type;

            fn target_moniker(&self) -> &str {
                &self.target_moniker
            }

            fn from_fidl(event: fevents::Event) -> Result<Self, Error> {
                // Event type in event must match what is expected
                let event_type = event.event_type.ok_or(
                    format_err!("Missing event_type from Event object")
                )?;
                if event_type != Self::TYPE {
                    return Err(format_err!("Incorrect event type. Expected: {:?}. Got: {:?}", Self::TYPE, event_type));
                }
                let target_moniker = event.target_moniker.ok_or(
                    format_err!("Missing target_moniker from Event object")
                )?;
                let handler = event.handler.ok_or(
                    format_err!("Missing handler from Event object")
                )?.into_proxy()?;

                // There should be no payload for this event
                if event.event_payload.is_some() {
                    return Err(format_err!("Unexpected event payload"));
                }

                Ok($event_type { target_moniker, handler, })
            }
        }

        impl Handler for $event_type {
            fn handler_proxy(self) -> fevents::HandlerProxy {
                self.handler
            }
        }
    };
}

// To create a class for an event, use the above macro here.
create_event!(Destroyed);
create_event!(Discovered);
create_event!(MarkedForDestruction);
create_event!(Resolved);
create_event!(Started);
create_event!(Stopped);
create_event!(
    event_type: CapabilityRouted,
    payload: {
        name: routing_payload,
        data: {
            {
                name: source,
                ty: fevents::CapabilitySource,
            }
            {
                name: capability_id,
                ty: String,
            }
        },
        protocols: {
            {
                name: routing_protocol,
                ty: fevents::RoutingProtocolProxy,
                trait_name: RoutingProtocol,
            }
        },
    }
);
