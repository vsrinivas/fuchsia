// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    async_trait::async_trait,
    fidl::endpoints::{create_proxy, create_request_stream, ClientEnd, ServerEnd, ServiceMarker},
    fidl::Channel,
    fidl_fuchsia_test_breakpoints as fbreak, fuchsia_async as fasync,
    fuchsia_component::client::*,
    futures::future::BoxFuture,
    futures::lock::Mutex,
    futures::StreamExt,
    std::sync::Arc,
};

/// A wrapper over the BreakpointSystem FIDL proxy.
/// Provides all of the FIDL methods with a cleaner, simpler interface.
/// Refer to breakpoints.fidl for a detailed description of this protocol.
pub struct BreakpointSystemClient {
    proxy: fbreak::BreakpointSystemProxy,
}

impl BreakpointSystemClient {
    /// Connects to the BreakpointSystem service at its default location
    /// The default location is presumably "/svc/fuchsia.test.breakpoints.BreakpointSystem"
    pub fn new() -> Result<Self, Error> {
        let proxy = connect_to_service::<fbreak::BreakpointSystemMarker>()
            .context("could not connect to BreakpointSystem service")?;
        Ok(BreakpointSystemClient::from_proxy(proxy))
    }

    /// Wraps a provided BreakpointSystem proxy
    pub fn from_proxy(proxy: fbreak::BreakpointSystemProxy) -> Self {
        Self { proxy }
    }

    pub async fn set_breakpoints(
        &self,
        event_types: Vec<fbreak::EventType>,
    ) -> Result<InvocationReceiverClient, Error> {
        let (proxy, server_end) = create_proxy::<fbreak::InvocationReceiverMarker>()?;
        self.proxy
            .set_breakpoints(&mut event_types.into_iter(), server_end)
            .await
            .context("could not register breakpoints")?;
        Ok(InvocationReceiverClient::new(proxy))
    }

    pub async fn soak_events(
        &self,
        event_types: Vec<fbreak::EventType>,
    ) -> Result<EventSink, Error> {
        let receiver = self.set_breakpoints(event_types).await?;
        Ok(EventSink::soak_async(receiver))
    }

    // This is a convenience method that sets a breakpoint on the the `RouteCapability`,
    // spawns a new task, and injects the service provided by the injector if requested
    // by the invocation.
    pub async fn install_injector<I: 'static>(&self, injector: Arc<I>) -> Result<(), Error>
    where
        I: Injector,
    {
        let receiver = self.set_breakpoints(vec![RouteCapability::TYPE]).await?;
        fasync::spawn(async move {
            loop {
                let invocation =
                    receiver.wait_until_type::<RouteCapability>().await.expect("Type mismatch");

                if invocation.capability_id == injector.capability_path() {
                    invocation.inject(injector.clone()).await.expect("injection failed");
                }

                invocation.resume().await.expect("resumption failed");
            }
        });
        Ok(())
    }

    // This is a convenience method that sets a breakpoint on the the `RouteCapability`,
    // spawns a new task, and interposes the service provided by the interposer if requested
    // by the invocation.
    pub async fn install_interposer<I: 'static>(&self, interposer: Arc<I>) -> Result<(), Error>
    where
        I: Interposer,
    {
        let receiver = self.set_breakpoints(vec![RouteCapability::TYPE]).await?;
        fasync::spawn(async move {
            loop {
                let invocation =
                    receiver.wait_until_type::<RouteCapability>().await.expect("Type mismatch");

                if invocation.capability_id == interposer.capability_path() {
                    invocation.interpose(interposer.clone()).await.expect("injection failed");
                }

                invocation.resume().await.expect("resumption failed");
            }
        });
        Ok(())
    }

    pub async fn start_component_manager(&self) -> Result<(), Error> {
        self.proxy.start_component_tree().await.context("could not start component tree")?;
        Ok(())
    }
}

/// A wrapper over the InvocationReceiver FIDL proxy.
/// Provides convenience methods that build on InvocationReceiver::Next
pub struct InvocationReceiverClient {
    proxy: fbreak::InvocationReceiverProxy,
}

impl InvocationReceiverClient {
    fn new(proxy: fbreak::InvocationReceiverProxy) -> Self {
        Self { proxy }
    }

    pub async fn next(&self) -> Result<fbreak::Invocation, Error> {
        let invocation = self.proxy.next().await.context("could not get next breakpoint")?;
        Ok(invocation)
    }

    /// Expects the next invocation to be of a particular type.
    /// Returns the casted type if successful and an error otherwise.
    pub async fn expect_type<T: Invocation>(&self) -> Result<T, Error> {
        let invocation = self.next().await?;
        T::from_fidl(invocation)
    }

    /// Expects the next invocation to be of a particular type and moniker.
    /// Returns the casted type if successful and an error otherwise.
    pub async fn expect_exact<T: Invocation>(&self, expected_moniker: &str) -> Result<T, Error> {
        let invocation = self.expect_type::<T>().await?;
        if expected_moniker == invocation.target_moniker() {
            Ok(invocation)
        } else {
            Err(format_err!("Incorrect moniker"))
        }
    }

    /// Waits for an invocation of a particular type.
    /// Implicitly resumes all other invocations.
    /// Returns the casted type if successful and an error otherwise.
    pub async fn wait_until_type<T: Invocation>(&self) -> Result<T, Error> {
        loop {
            let invocation = self.next().await?;
            if let Ok(invocation) = T::from_fidl(invocation) {
                return Ok(invocation);
            }
        }
    }

    /// Waits for an invocation of a particular type and target moniker.
    /// Implicitly resumes all other invocations.
    /// Returns the casted type if successful and an error otherwise.
    pub async fn wait_until_exact<T: Invocation>(
        &self,
        expected_target_moniker: &str,
    ) -> Result<T, Error> {
        loop {
            let invocation = self.wait_until_type::<T>().await?;
            if invocation.target_moniker() == expected_target_moniker {
                return Ok(invocation);
            }
            invocation.resume().await?;
        }
    }

    /// Waits for a component capability to be routed matching a particular
    /// target moniker and capability. Implicitly resumes all other invocations.
    /// Returns the casted type if successful and an error otherwise.
    pub async fn wait_until_component_capability(
        &self,
        expected_target_moniker: &str,
        expected_capability_id: &str,
    ) -> Result<RouteCapability, Error> {
        loop {
            let invocation =
                self.wait_until_exact::<RouteCapability>(expected_target_moniker).await?;
            if expected_capability_id == invocation.capability_id {
                match invocation.source {
                    fbreak::CapabilitySource::Component(_) => return Ok(invocation),
                    _ => {}
                }
            }
            invocation.resume().await?;
        }
    }

    /// Waits for a framework capability to be routed matching a particular
    /// target moniker, scope moniker and capability. Implicitly resumes all other invocations.
    /// Returns the casted type if successful and an error otherwise.
    pub async fn wait_until_framework_capability(
        &self,
        expected_target_moniker: &str,
        expected_capability_id: &str,
        expected_scope_moniker: Option<&str>,
    ) -> Result<RouteCapability, Error> {
        loop {
            let invocation =
                self.wait_until_exact::<RouteCapability>(expected_target_moniker).await?;

            // If the capability ID matches and the capability source is framework
            // with a matching optional scope moniker, then return the invocation.
            if expected_capability_id == invocation.capability_id {
                match &invocation.source {
                    fbreak::CapabilitySource::Framework(fbreak::FrameworkCapability {
                        scope_moniker,
                        ..
                    }) if scope_moniker.as_ref().map(|s| s.as_str()) == expected_scope_moniker => {
                        return Ok(invocation)
                    }
                    _ => {}
                }
            }

            invocation.resume().await?;
        }
    }
}

/// Common features of any invocation - event type, target moniker, conversion function
pub trait Invocation: Handler {
    const TYPE: fbreak::EventType;
    fn target_moniker(&self) -> &str;
    fn from_fidl(inv: fbreak::Invocation) -> Result<Self, Error>;
}

/// Basic handler that resumes/unblocks from an Invocation
#[must_use = "invoke resume() otherwise component manager will be halted indefinitely!"]
pub trait Handler: Sized {
    fn handler_proxy(self) -> fbreak::HandlerProxy;

    #[must_use = "futures do nothing unless you await on them!"]
    fn resume<'a>(self) -> BoxFuture<'a, Result<(), fidl::Error>> {
        let proxy = self.handler_proxy();
        Box::pin(async move { proxy.resume().await })
    }
}

/// Implemented on fbreak::Invocation for resuming a generic invocation
impl Handler for fbreak::Invocation {
    fn handler_proxy(self) -> fbreak::HandlerProxy {
        self.handler
            .expect("Could not find handler in Invocation object")
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
    );

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
    );

    fn capability_path(&self) -> String {
        format!("/svc/{}", Self::Marker::NAME)
    }
}

/// A protocol that allows routing capabilities over FIDL.
#[async_trait]
pub trait RoutingProtocol {
    fn protocol_proxy(&self) -> fbreak::RoutingProtocolProxy;

    #[must_use = "futures do nothing unless you await on them!"]
    async fn set_provider(
        &self,
        client_end: ClientEnd<fbreak::CapabilityProviderMarker>,
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
            create_request_stream::<fbreak::CapabilityProviderMarker>()
                .expect("Could not create request stream for CapabilityProvider");

        // Wait for an Open request on the CapabilityProvider channel
        fasync::spawn(async move {
            if let Some(Ok(fbreak::CapabilityProviderRequest::Open { server_end, responder })) =
                provider_capability_stream.next().await
            {
                // Unblock component manager from the open request
                responder.send().expect("Could not respond to CapabilityProvider::Open");

                // Create the stream for the Client <---> Interposer connection
                let stream = ServerEnd::<I::Marker>::new(server_end)
                    .into_stream()
                    .expect("could not convert channel into stream");

                injector.serve(stream).await;
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
            create_request_stream::<fbreak::CapabilityProviderMarker>()
                .expect("Could not create request stream for CapabilityProvider");

        // Wait for an Open request on the CapabilityProvider channel
        fasync::spawn(async move {
            if let Some(Ok(fbreak::CapabilityProviderRequest::Open { server_end, responder })) =
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
                interposer.interpose(stream, proxy).await;
            } else {
                panic!("Failed to interpose! CapabilityProvider was not able to invoke Open");
            }
        });

        // Replace the existing provider and open it with the
        // server end of the Interposer <---> Server channel.
        self.protocol_proxy().replace_and_open(provider_client_end, server_end).await
    }
}

/// Describes an event drained out by the EventSink
#[derive(Eq, PartialEq, PartialOrd, Ord, Debug)]
pub struct DrainedEvent {
    pub event_type: fbreak::EventType,
    pub target_moniker: String,
}

/// Soaks events from an InvocationReceiverClient, allowing them to be
/// drained at a later point in time.
pub struct EventSink {
    drained_events: Arc<Mutex<Vec<DrainedEvent>>>,
}

impl EventSink {
    fn soak_async(receiver: InvocationReceiverClient) -> Self {
        let drained_events = Arc::new(Mutex::new(vec![]));
        {
            // Start an async task that soaks up events from the receiver
            let drained_events = drained_events.clone();
            fasync::spawn(async move {
                // TODO(xbhatnag): Terminate this infinite loop when EventSink is dropped.
                // Or pass in a Weak and terminate if it can't be upgraded.
                loop {
                    // Get the next invocation from the receiver
                    let inv = receiver
                        .next()
                        .await
                        .expect("Failed to get next event from InvocationReceiver");

                    // Construct the DrainedEvent from the Invocation
                    let event_type =
                        inv.event_type.expect("Failed to get event type from Invocation");
                    let target_moniker = inv
                        .target_moniker
                        .as_ref()
                        .expect("Failed to get target moniker from Invocation")
                        .clone();
                    let event = DrainedEvent { event_type, target_moniker };

                    // Insert the event into the list
                    {
                        let mut drained_events = drained_events.lock().await;
                        drained_events.push(event);
                    }

                    // Resume from the invocation
                    inv.resume().await.expect("Could not resume from invocation");
                }
            });
        }
        Self { drained_events }
    }

    pub async fn drain(&self) -> Vec<DrainedEvent> {
        // Lock and drain out all events from the vector
        let mut drained_events = self.drained_events.lock().await;
        drained_events.drain(..).collect()
    }
}

/// The macro defined below will automatically create event classes corresponding
/// to their breakpoints.fidl and hooks.rs counterparts. Every event class implements
/// the Invocation and Handler traits. These minimum requirements allow every event to
/// be handled by the breakpoints client library.

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
            handler: fbreak::HandlerProxy,
            $($protocol_name: $protocol_ty,)*
            $(pub $data_name: $data_ty,)*
        }

        impl Invocation for $event_type {
            const TYPE: fbreak::EventType = fbreak::EventType::$event_type;

            fn target_moniker(&self) -> &str {
                &self.target_moniker
            }

            fn from_fidl(inv: fbreak::Invocation) -> Result<Self, Error> {
                // Event type in invocation must match what is expected
                let event_type = inv.event_type.ok_or(
                    format_err!("Missing event_type from Invocation object")
                )?;
                if event_type != Self::TYPE {
                    return Err(format_err!("Incorrect event type"));
                }
                let target_moniker = inv.target_moniker.ok_or(
                    format_err!("Missing target_moniker from Invocation object")
                )?;
                let handler = inv.handler.ok_or(
                    format_err!("Missing handler from Invocation object")
                )?.into_proxy()?;

                // Extract the payload from the Invocation object.
                let event_payload = inv.event_payload.ok_or(
                    format_err!("Missing event_payload from Invocation object")
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
            fn handler_proxy(self) -> fbreak::HandlerProxy {
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
            handler: fbreak::HandlerProxy,
        }

        impl Invocation for $event_type {
            const TYPE: fbreak::EventType = fbreak::EventType::$event_type;

            fn target_moniker(&self) -> &str {
                &self.target_moniker
            }

            fn from_fidl(inv: fbreak::Invocation) -> Result<Self, Error> {
                // Event type in invocation must match what is expected
                let event_type = inv.event_type.ok_or(
                    format_err!("Missing event_type from Invocation object")
                )?;
                if event_type != Self::TYPE {
                    return Err(format_err!("Incorrect event type"));
                }
                let target_moniker = inv.target_moniker.ok_or(
                    format_err!("Missing target_moniker from Invocation object")
                )?;
                let handler = inv.handler.ok_or(
                    format_err!("Missing handler from Invocation object")
                )?.into_proxy()?;

                // There should be no payload for this event
                if inv.event_payload.is_some() {
                    return Err(format_err!("Unexpected event payload"));
                }

                Ok($event_type { target_moniker, handler, })
            }
        }

        impl Handler for $event_type {
            fn handler_proxy(self) -> fbreak::HandlerProxy {
                self.handler
            }
        }
    };
}

// To create a class for an event, use the above macro here.
create_event!(AddDynamicChild);
create_event!(BeforeStartInstance);
create_event!(PostDestroyInstance);
create_event!(PreDestroyInstance);
create_event!(ResolveInstance);
create_event!(StopInstance);
create_event!(
    event_type: RouteCapability,
    payload: {
        name: routing_payload,
        data: {
            {
                name: source,
                ty: fbreak::CapabilitySource,
            }
            {
                name: capability_id,
                ty: String,
            }
        },
        protocols: {
            {
                name: routing_protocol,
                ty: fbreak::RoutingProtocolProxy,
                trait_name: RoutingProtocol,
            }
        },
    }
);
