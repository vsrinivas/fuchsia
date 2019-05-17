// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Wrapper types for the endpoints of a connection.

use {
    crate::{Error, ServeInner},
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    futures::{self, Future, FutureExt, Stream, TryFutureExt, TryStream, TryStreamExt},
    std::convert::From,
    std::marker::{PhantomData, Unpin},
    std::sync::Arc,
};

/// A marker for a particular FIDL service.
///
/// Implementations of this trait can be used to manufacture instances of a FIDL service
/// and get metadata about a particular service.
pub trait ServiceMarker: Sized + Send + Sync + 'static {
    /// The type of the structure against which FIDL requests are made.
    /// Queries made against the proxy are sent to the paired `ServerEnd`.
    type Proxy: Proxy<Service = Self>;

    /// The type of the stream of requests coming into a server.
    type RequestStream: RequestStream<Service = Self>;

    /// The name of the service (to be used for service lookup and discovery).
    const NAME: &'static str;
}

/// A type which allows querying a remote FIDL server over a channel.
pub trait Proxy: Sized + Send + Sync {
    /// The service which this `Proxy` controls.
    type Service: ServiceMarker<Proxy = Self>;

    /// Create a proxy over the given channel.
    fn from_channel(inner: fasync::Channel) -> Self;
}

/// A stream of requests coming into a FIDL server over a channel.
pub trait RequestStream: Sized + Send + Stream + TryStream<Error = crate::Error> + Unpin {
    /// The service which this `RequestStream` serves.
    type Service: ServiceMarker<RequestStream = Self>;

    /// A type that can be used to send events and shut down the request stream.
    type ControlHandle;

    /// Returns a copy of the `ControlHandle` for the given stream.
    /// This handle can be used to send events and shut down the request stream.
    fn control_handle(&self) -> Self::ControlHandle;

    /// Create a request stream from the given channel.
    fn from_channel(inner: fasync::Channel) -> Self;

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
pub type Request<Marker> = <<Marker as ServiceMarker>::RequestStream as futures::TryStream>::Ok;

/// Utility that spawns a new task to handle requests of a particular type, requiring a
/// singlethreaded executor. The requests are handled one at a time.
pub fn spawn_local_stream_handler<P, F, Fut>(f: F) -> Result<P, Error>
where
    P: Proxy,
    F: FnMut(Request<P::Service>) -> Fut + 'static,
    Fut: Future<Output = ()> + 'static,
{
    let (proxy, stream) = create_proxy_and_stream::<P::Service>()?;
    fasync::spawn_local(for_each_or_log(stream, f));
    Ok(proxy)
}

/// Utility that spawns a new task to handle requests of a particular type. The request handler
/// must be threadsafe. The requests are handled one at a time.
pub fn spawn_stream_handler<P, F, Fut>(f: F) -> Result<P, Error>
where
    P: Proxy,
    F: FnMut(Request<P::Service>) -> Fut + 'static + Send,
    Fut: Future<Output = ()> + 'static + Send,
{
    let (proxy, stream) = create_proxy_and_stream::<P::Service>()?;
    fasync::spawn(for_each_or_log(stream, f));
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
        .unwrap_or_else(|e| fx_log_err!("FIDL stream handler failed: {}", e))
}

/// The `Client` end of a FIDL connection.
#[derive(Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct ClientEnd<T> {
    inner: zx::Channel,
    phantom: PhantomData<T>,
}

impl<T> ClientEnd<T> {
    /// Create a new client from the provided channel.
    pub fn new(inner: zx::Channel) -> Self {
        ClientEnd { inner, phantom: PhantomData }
    }

    /// Get a reference to the undelrying channel
    pub fn channel(&self) -> &zx::Channel {
        &self.inner
    }

    /// Extract the inner channel.
    pub fn into_channel(self) -> zx::Channel {
        self.inner
    }
}

impl<T: ServiceMarker> ClientEnd<T> {
    /// Convert the `ClientEnd` into a `Proxy` through which FIDL calls may be made.
    pub fn into_proxy(self) -> Result<T::Proxy, Error> {
        Ok(T::Proxy::from_channel(
            fasync::Channel::from_channel(self.inner).map_err(Error::AsyncChannel)?,
        ))
    }
}

impl<T> zx::AsHandleRef for ClientEnd<T> {
    fn as_handle_ref(&self) -> zx::HandleRef {
        self.inner.as_handle_ref()
    }
}

impl<T> From<ClientEnd<T>> for zx::Handle {
    fn from(client: ClientEnd<T>) -> zx::Handle {
        client.into_channel().into()
    }
}

impl<T> From<zx::Handle> for ClientEnd<T> {
    fn from(handle: zx::Handle) -> Self {
        ClientEnd { inner: handle.into(), phantom: PhantomData }
    }
}

impl<T> From<zx::Channel> for ClientEnd<T> {
    fn from(chan: zx::Channel) -> Self {
        ClientEnd { inner: chan, phantom: PhantomData }
    }
}

impl<T: ServiceMarker> ::std::fmt::Debug for ClientEnd<T> {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        write!(f, "ClientEnd(name={}, channel={:?})", T::NAME, self.inner)
    }
}

impl<T> zx::HandleBased for ClientEnd<T> {}

/// The `Server` end of a FIDL connection.
#[derive(Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct ServerEnd<T> {
    inner: zx::Channel,
    phantom: PhantomData<T>,
}

impl<T> ServerEnd<T> {
    /// Create a new `ServerEnd` from the provided channel.
    pub fn new(inner: zx::Channel) -> ServerEnd<T> {
        ServerEnd { inner, phantom: PhantomData }
    }

    /// Get a reference to the undelrying channel
    pub fn channel(&self) -> &zx::Channel {
        &self.inner
    }

    /// Extract the inner channel.
    pub fn into_channel(self) -> zx::Channel {
        self.inner
    }

    /// Create a stream of requests off of the channel.
    pub fn into_stream(self) -> Result<T::RequestStream, Error>
    where
        T: ServiceMarker,
    {
        Ok(T::RequestStream::from_channel(
            fasync::Channel::from_channel(self.inner).map_err(Error::AsyncChannel)?,
        ))
    }

    /// Create a stream of requests and an event-sending handle
    /// from the channel.
    pub fn into_stream_and_control_handle(
        self,
    ) -> Result<(T::RequestStream, <T::RequestStream as RequestStream>::ControlHandle), Error>
    where
        T: ServiceMarker,
    {
        let stream = self.into_stream()?;
        let control_handle = stream.control_handle();
        Ok((stream, control_handle))
    }
}

impl<T> zx::AsHandleRef for ServerEnd<T> {
    fn as_handle_ref(&self) -> zx::HandleRef {
        self.inner.as_handle_ref()
    }
}

impl<T> From<ServerEnd<T>> for zx::Handle {
    fn from(server: ServerEnd<T>) -> zx::Handle {
        server.into_channel().into()
    }
}

impl<T> From<zx::Handle> for ServerEnd<T> {
    fn from(handle: zx::Handle) -> Self {
        ServerEnd { inner: handle.into(), phantom: PhantomData }
    }
}

impl<T> From<zx::Channel> for ServerEnd<T> {
    fn from(chan: zx::Channel) -> Self {
        ServerEnd { inner: chan, phantom: PhantomData }
    }
}

impl<T: ServiceMarker> ::std::fmt::Debug for ServerEnd<T> {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        write!(f, "ServerEnd(name={}, channel={:?})", T::NAME, self.inner)
    }
}

impl<T> zx::HandleBased for ServerEnd<T> {}

handle_based_codable![ClientEnd :- <T,>, ServerEnd :- <T,>,];

/// Creates client and server endpoints connected to by a channel.
pub fn create_endpoints<T: ServiceMarker>() -> Result<(ClientEnd<T>, ServerEnd<T>), Error> {
    let (client, server) = zx::Channel::create().map_err(Error::ChannelPairCreate)?;
    let client_end = ClientEnd::<T>::new(client);
    let server_end = ServerEnd::new(server);
    Ok((client_end, server_end))
}

/// Create a client proxy and a server endpoint connected to it by a channel.
///
/// Useful for sending channel handles to calls that take arguments
/// of type `request<SomeInterface>`
pub fn create_proxy<T: ServiceMarker>() -> Result<(T::Proxy, ServerEnd<T>), Error> {
    let (client, server) = create_endpoints()?;
    Ok((client.into_proxy()?, server))
}

/// Create a request stream and a client endpoint connected to it by a channel.
///
/// Useful for sending channel handles to calls that take arguments
/// of type `SomeInterface`
pub fn create_request_stream<T: ServiceMarker>() -> Result<(ClientEnd<T>, T::RequestStream), Error>
{
    let (client, server) = create_endpoints()?;
    Ok((client, server.into_stream()?))
}

/// Create a request stream and proxy connected to one another.
///
/// Useful for testing where both the request stream and proxy are
/// used in the same process.
pub fn create_proxy_and_stream<T: ServiceMarker>() -> Result<(T::Proxy, T::RequestStream), Error> {
    let (client, server) = create_endpoints::<T>()?;
    Ok((client.into_proxy()?, server.into_stream()?))
}
