// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Wrapper types for the endpoints of a connection.

use {
    crate::{
        epitaph::ChannelEpitaphExt, AsHandleRef, AsyncChannel, Channel, Error, Handle, HandleBased,
        HandleRef, ServeInner,
    },
    fuchsia_async as fasync, fuchsia_zircon_status as zx_status,
    futures::{self, Future, FutureExt, Stream, TryFutureExt, TryStream, TryStreamExt},
    log::error,
    std::convert::From,
    std::marker::{PhantomData, Unpin},
    std::sync::Arc,
};

#[cfg(target_os = "fuchsia")]
use fuchsia_zircon as zx;

/// A marker for a particular FIDL protocol.
///
/// Implementations of this trait can be used to manufacture instances of a FIDL
/// protocol and get metadata about a particular protocol.
pub trait ProtocolMarker: Sized + Send + Sync + 'static {
    /// The type of the structure against which FIDL requests are made.
    /// Queries made against the proxy are sent to the paired `ServerEnd`.
    type Proxy: Proxy<Protocol = Self>;

    /// The type of the stream of requests coming into a server.
    type RequestStream: RequestStream<Protocol = Self>;

    /// The name of the protocol suitable for debug purposes.
    ///
    /// This will be removed-- users should switch to either
    /// `DEBUG_NAME` or `DiscoverableProtocolMarker::PROTOCOL_NAME`.
    const NAME: &'static str = Self::DEBUG_NAME;

    /// The name of the protocol suitable for debug purposes.
    ///
    /// For discoverable protocols, this should be identical to
    /// `<Self as DiscoverableProtocolMarker>::PROTOCOL_NAME`.
    const DEBUG_NAME: &'static str;
}

/// A marker for a particular FIDL protocol that is also discoverable.
///
/// Discoverable protocols may be referred to by a string name, and can be
/// conveniently exported in a service directory via an entry of that name.
///
/// If you get an error about this trait not being implemented, you probably
/// need to add the `@discoverable` attribute to the FIDL protocol, like this:
///
/// ```fidl
/// @discoverable
/// protocol MyProtocol { ... };
/// ```
pub trait DiscoverableProtocolMarker: ProtocolMarker {
    /// The name of the protocol (to be used for service lookup and discovery).
    const PROTOCOL_NAME: &'static str = <Self as ProtocolMarker>::DEBUG_NAME;
}

/// A type which allows querying a remote FIDL server over a channel.
pub trait Proxy: Sized + Send + Sync {
    /// The protocol which this `Proxy` controls.
    type Protocol: ProtocolMarker<Proxy = Self>;

    /// Create a proxy over the given channel.
    fn from_channel(inner: AsyncChannel) -> Self;

    /// Attempt to convert the proxy back into a channel.
    ///
    /// This will only succeed if there are no active clones of this proxy
    /// and no currently-alive `EventStream` or response futures that came from
    /// this proxy.
    fn into_channel(self) -> Result<AsyncChannel, Self>;

    /// Get a reference to the proxy's underlying channel.
    ///
    /// This should only be used for non-effectful operations. Reading or
    /// writing to the channel is unsafe because the proxy assumes it has
    /// exclusive control over these operations.
    fn as_channel(&self) -> &AsyncChannel;

    /// Returns true if the proxy has received the `PEER_CLOSED` signal.
    #[cfg(target_os = "fuchsia")]
    fn is_closed(&self) -> bool {
        self.as_channel().is_closed()
    }

    /// Returns a future that completes when the proxy receives the
    /// `PEER_CLOSED` signal.
    #[cfg(target_os = "fuchsia")]
    fn on_closed<'a>(&'a self) -> fasync::OnSignals<'a> {
        fasync::OnSignals::new(self.as_channel(), zx::Signals::CHANNEL_PEER_CLOSED)
    }
}

/// A stream of requests coming into a FIDL server over a channel.
pub trait RequestStream: Sized + Send + Stream + TryStream<Error = crate::Error> + Unpin {
    /// The protocol which this `RequestStream` serves.
    type Protocol: ProtocolMarker<RequestStream = Self>;

    /// The control handle for this `RequestStream`.
    type ControlHandle: ControlHandle;

    /// Returns a copy of the `ControlHandle` for the given stream.
    /// This handle can be used to send events or shut down the request stream.
    fn control_handle(&self) -> Self::ControlHandle;

    /// Create a request stream from the given channel.
    fn from_channel(inner: AsyncChannel) -> Self;

    /// Convert this channel into its underlying components.
    fn into_inner(self) -> (Arc<ServeInner>, bool);

    /// Create this channel from its underlying components.
    fn from_inner(inner: Arc<ServeInner>, is_terminated: bool) -> Self;

    /// Convert this FIDL request stream into a request stream of another FIDL protocol.
    fn cast_stream<T: RequestStream>(self) -> T {
        let inner = self.into_inner();
        T::from_inner(inner.0, inner.1)
    }
}

/// The Request type associated with a Marker.
pub type Request<Marker> = <<Marker as ProtocolMarker>::RequestStream as futures::TryStream>::Ok;

/// A type associated with a `RequestStream` that can be used to send FIDL
/// events or to shut down the request stream.
pub trait ControlHandle {
    /// Set the server to shutdown. The underlying channel is only closed the
    /// next time the stream is polled.
    // TODO(fxbug.dev/81036): Fix behavior or above docs.
    fn shutdown(&self);

    /// Sets the server to shutdown with an epitaph. The underlying channel is
    /// only closed the next time the stream is polled.
    // TODO(fxbug.dev/81036): Fix behavior or above docs.
    fn shutdown_with_epitaph(&self, status: zx_status::Status);
}

/// A type associated with a particular two-way FIDL method, used by servers to
/// send a response to the client.
pub trait Responder {
    /// The control handle for this protocol.
    type ControlHandle: ControlHandle;

    /// Returns the `ControlHandle` for this protocol.
    fn control_handle(&self) -> &Self::ControlHandle;

    /// Drops the responder without setting the channel to shutdown.
    ///
    /// This method shouldn't normally be used. Instead, send a response to
    /// prevent the channel from shutting down.
    fn drop_without_shutdown(self);
}

/// A marker for a particular FIDL service.
#[cfg(target_os = "fuchsia")]
pub trait ServiceMarker: Sized + Send + Sync + 'static {
    /// The type of the proxy object upon which calls are made to a remote FIDL service.
    type Proxy: ServiceProxy<Service = Self>;

    /// The request type for this particular FIDL service.
    type Request: ServiceRequest<Service = Self>;

    /// The name of the service. Used for service lookup and discovery.
    const SERVICE_NAME: &'static str;
}

/// A request to initiate a connection to a FIDL service.
#[cfg(target_os = "fuchsia")]
pub trait ServiceRequest: Sized + Send + Sync {
    /// The FIDL service for which this request is destined.
    type Service: ServiceMarker<Request = Self>;

    /// Dispatches a connection attempt to this FIDL service's member protocol
    /// identified by `name`, producing an instance of this trait.
    fn dispatch(name: &str, channel: AsyncChannel) -> Self;

    /// Returns an array of the service members' names.
    fn member_names() -> &'static [&'static str];
}

/// Proxy by which a client sends messages to a FIDL service.
#[cfg(target_os = "fuchsia")]
pub trait ServiceProxy: Sized {
    /// The FIDL service this proxy represents.
    type Service: ServiceMarker<Proxy = Self>;

    /// Create a proxy from a MemberOpener implementation.
    #[doc(hidden)]
    fn from_member_opener(opener: Box<dyn MemberOpener>) -> Self;
}

/// Used to create an indirection between the fuchsia.io.Directory protocol
/// and this library, which cannot depend on fuchsia.io.
#[doc(hidden)]
#[cfg(target_os = "fuchsia")]
pub trait MemberOpener {
    /// Opens a member protocol of a FIDL service by name, serving that protocol
    /// on the given channel.
    fn open_member(&self, member: &str, server_end: Channel) -> Result<(), Error>;
}

/// Utility that spawns a new task to handle requests of a particular type, requiring a
/// singlethreaded executor. The requests are handled one at a time.
pub fn spawn_local_stream_handler<P, F, Fut>(f: F) -> Result<P, Error>
where
    P: Proxy,
    F: FnMut(Request<P::Protocol>) -> Fut + 'static,
    Fut: Future<Output = ()> + 'static,
{
    let (proxy, stream) = create_proxy_and_stream::<P::Protocol>()?;
    fasync::Task::local(for_each_or_log(stream, f)).detach();
    Ok(proxy)
}

/// Utility that spawns a new task to handle requests of a particular type. The request handler
/// must be threadsafe. The requests are handled one at a time.
pub fn spawn_stream_handler<P, F, Fut>(f: F) -> Result<P, Error>
where
    P: Proxy,
    F: FnMut(Request<P::Protocol>) -> Fut + 'static + Send,
    Fut: Future<Output = ()> + 'static + Send,
{
    let (proxy, stream) = create_proxy_and_stream::<P::Protocol>()?;
    fasync::Task::spawn(for_each_or_log(stream, f)).detach();
    Ok(proxy)
}

fn for_each_or_log<St, F, Fut>(stream: St, mut f: F) -> impl Future<Output = ()>
where
    St: RequestStream,
    F: FnMut(St::Ok) -> Fut,
    Fut: Future<Output = ()>,
{
    stream
        .try_for_each(move |r| f(r).map(Ok))
        .unwrap_or_else(|e| error!("FIDL stream handler failed: {}", e))
}

/// The `Client` end of a FIDL connection.
#[derive(Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct ClientEnd<T> {
    inner: Channel,
    phantom: PhantomData<T>,
}

impl<T> ClientEnd<T> {
    /// Create a new client from the provided channel.
    pub fn new(inner: Channel) -> Self {
        ClientEnd { inner, phantom: PhantomData }
    }

    /// Get a reference to the underlying channel
    pub fn channel(&self) -> &Channel {
        &self.inner
    }

    /// Extract the underlying channel.
    pub fn into_channel(self) -> Channel {
        self.inner
    }
}

impl<T: ProtocolMarker> ClientEnd<T> {
    /// Convert the `ClientEnd` into a `Proxy` through which FIDL calls may be made.
    pub fn into_proxy(self) -> Result<T::Proxy, Error> {
        Ok(T::Proxy::from_channel(
            AsyncChannel::from_channel(self.inner).map_err(Error::AsyncChannel)?,
        ))
    }
}

impl<T> AsHandleRef for ClientEnd<T> {
    fn as_handle_ref(&self) -> HandleRef {
        self.inner.as_handle_ref()
    }
}

impl<T> From<ClientEnd<T>> for Handle {
    fn from(client: ClientEnd<T>) -> Handle {
        client.into_channel().into()
    }
}

impl<T> From<Handle> for ClientEnd<T> {
    fn from(handle: Handle) -> Self {
        ClientEnd { inner: handle.into(), phantom: PhantomData }
    }
}

impl<T> From<Channel> for ClientEnd<T> {
    fn from(chan: Channel) -> Self {
        ClientEnd { inner: chan, phantom: PhantomData }
    }
}

impl<T: ProtocolMarker> ::std::fmt::Debug for ClientEnd<T> {
    fn fmt(&self, f: &mut ::std::fmt::Formatter<'_>) -> ::std::fmt::Result {
        write!(f, "ClientEnd(name={}, channel={:?})", T::DEBUG_NAME, self.inner)
    }
}

impl<T> HandleBased for ClientEnd<T> {}

/// The `Server` end of a FIDL connection.
#[derive(Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct ServerEnd<T> {
    inner: Channel,
    phantom: PhantomData<T>,
}

impl<T> ServerEnd<T> {
    /// Create a new `ServerEnd` from the provided channel.
    pub fn new(inner: Channel) -> ServerEnd<T> {
        ServerEnd { inner, phantom: PhantomData }
    }

    /// Get a reference to the undelrying channel
    pub fn channel(&self) -> &Channel {
        &self.inner
    }

    /// Extract the inner channel.
    pub fn into_channel(self) -> Channel {
        self.inner
    }

    /// Create a stream of requests off of the channel.
    pub fn into_stream(self) -> Result<T::RequestStream, Error>
    where
        T: ProtocolMarker,
    {
        Ok(T::RequestStream::from_channel(
            AsyncChannel::from_channel(self.inner).map_err(Error::AsyncChannel)?,
        ))
    }

    /// Create a stream of requests and an event-sending handle
    /// from the channel.
    pub fn into_stream_and_control_handle(
        self,
    ) -> Result<(T::RequestStream, <T::RequestStream as RequestStream>::ControlHandle), Error>
    where
        T: ProtocolMarker,
    {
        let stream = self.into_stream()?;
        let control_handle = stream.control_handle();
        Ok((stream, control_handle))
    }

    /// Writes an epitaph into the underlying channel before closing it.
    pub fn close_with_epitaph(self, status: zx_status::Status) -> Result<(), Error> {
        self.inner.close_with_epitaph(status)
    }
}

impl<T> AsHandleRef for ServerEnd<T> {
    fn as_handle_ref(&self) -> HandleRef {
        self.inner.as_handle_ref()
    }
}

impl<T> From<ServerEnd<T>> for Handle {
    fn from(server: ServerEnd<T>) -> Handle {
        server.into_channel().into()
    }
}

impl<T> From<Handle> for ServerEnd<T> {
    fn from(handle: Handle) -> Self {
        ServerEnd { inner: handle.into(), phantom: PhantomData }
    }
}

impl<T> From<Channel> for ServerEnd<T> {
    fn from(chan: Channel) -> Self {
        ServerEnd { inner: chan, phantom: PhantomData }
    }
}

impl<T: ProtocolMarker> ::std::fmt::Debug for ServerEnd<T> {
    fn fmt(&self, f: &mut ::std::fmt::Formatter<'_>) -> ::std::fmt::Result {
        write!(f, "ServerEnd(name={}, channel={:?})", T::DEBUG_NAME, self.inner)
    }
}

impl<T> HandleBased for ServerEnd<T> {}

handle_based_codable![ClientEnd :- <T,>, ServerEnd :- <T,>,];

/// Creates client and server endpoints connected to by a channel.
pub fn create_endpoints<T: ProtocolMarker>() -> Result<(ClientEnd<T>, ServerEnd<T>), Error> {
    let (client, server) = Channel::create().map_err(|e| Error::ChannelPairCreate(e.into()))?;
    let client_end = ClientEnd::<T>::new(client);
    let server_end = ServerEnd::new(server);
    Ok((client_end, server_end))
}

/// Create a client proxy and a server endpoint connected to it by a channel.
///
/// Useful for sending channel handles to calls that take arguments
/// of type `request<SomeInterface>`
pub fn create_proxy<T: ProtocolMarker>() -> Result<(T::Proxy, ServerEnd<T>), Error> {
    let (client, server) = create_endpoints()?;
    Ok((client.into_proxy()?, server))
}

/// Create a request stream and a client endpoint connected to it by a channel.
///
/// Useful for sending channel handles to calls that take arguments
/// of type `SomeInterface`
pub fn create_request_stream<T: ProtocolMarker>() -> Result<(ClientEnd<T>, T::RequestStream), Error>
{
    let (client, server) = create_endpoints()?;
    Ok((client, server.into_stream()?))
}

/// Create a request stream and proxy connected to one another.
///
/// Useful for testing where both the request stream and proxy are
/// used in the same process.
pub fn create_proxy_and_stream<T: ProtocolMarker>() -> Result<(T::Proxy, T::RequestStream), Error> {
    let (client, server) = create_endpoints::<T>()?;
    Ok((client.into_proxy()?, server.into_stream()?))
}
