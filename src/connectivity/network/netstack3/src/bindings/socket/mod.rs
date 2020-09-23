// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Socket features exposed by netstack3.

pub(crate) mod udp;

use std::convert::{TryFrom, TryInto};
use std::num::NonZeroU16;

use fidl::endpoints::{ClientEnd, RequestStream};
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_posix::Errno;
use fidl_fuchsia_posix_socket as psocket;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{TryFutureExt, TryStreamExt};
use log::debug;
use net_types::ip::{Ip, IpAddress, IpVersion, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
use net_types::{SpecifiedAddr, Witness};
use netstack3_core::{
    LocalAddressError, NetstackError, RemoteAddressError, SocketError, UdpSendError,
};

use super::{context::InnerValue, StackContext};
use crate::bindings::util::{IntoCore, IntoFidl};

// Socket constants defined in FDIO in
// `//sdk/lib/fdio/private-socket.h`
// TODO(brunodalbo) Come back to this, see if we can have those definitions in a
// public header from FDIO somehow so we don't need to redefine.
const ZXSIO_SIGNAL_INCOMING: zx::Signals = zx::Signals::USER_0;
const ZXSIO_SIGNAL_OUTGOING: zx::Signals = zx::Signals::USER_1;

/// Supported transport protocols.
#[derive(Debug)]
pub enum TransProto {
    Udp,
    Tcp,
}

impl TryFrom<psocket::DatagramSocketProtocol> for TransProto {
    type Error = Errno;

    fn try_from(value: psocket::DatagramSocketProtocol) -> Result<Self, Self::Error> {
        match value {
            psocket::DatagramSocketProtocol::Udp => Ok(TransProto::Udp),
            psocket::DatagramSocketProtocol::IcmpEcho => Err(Errno::Eprotonosupport),
        }
    }
}

impl TryFrom<psocket::StreamSocketProtocol> for TransProto {
    type Error = Errno;

    fn try_from(value: psocket::StreamSocketProtocol) -> Result<Self, Self::Error> {
        match value {
            psocket::StreamSocketProtocol::Tcp => Ok(TransProto::Tcp),
        }
    }
}

/// Common properties for socket workers.
#[derive(Debug)]
struct SocketWorkerProperties {}

pub(crate) trait SocketStackDispatcher:
    InnerValue<udp::UdpSocketCollection>
    + netstack3_core::UdpEventDispatcher<Ipv4>
    + netstack3_core::UdpEventDispatcher<Ipv6>
{
}
impl<T> SocketStackDispatcher for T where
    T: InnerValue<udp::UdpSocketCollection>
        + netstack3_core::UdpEventDispatcher<Ipv4>
        + netstack3_core::UdpEventDispatcher<Ipv6>
{
}

pub(crate) trait SocketStackContext:
    StackContext + udp::UdpStackContext<Ipv4> + udp::UdpStackContext<Ipv6>
where
    <Self as StackContext>::Dispatcher: SocketStackDispatcher,
{
}
impl<T> SocketStackContext for T
where
    T: StackContext + udp::UdpStackContext<Ipv4> + udp::UdpStackContext<Ipv6>,
    T::Dispatcher: SocketStackDispatcher,
{
}

pub(crate) struct SocketProviderWorker<C> {
    ctx: C,
}

impl<C> SocketProviderWorker<C>
where
    C: SocketStackContext,
    C::Dispatcher: SocketStackDispatcher,
{
    pub(crate) fn spawn(ctx: C, mut rs: psocket::ProviderRequestStream) {
        fasync::Task::spawn(
            async move {
                let worker = SocketProviderWorker { ctx };
                while let Some(req) = rs.try_next().await? {
                    worker.handle_fidl_socket_provider_request(req).await;
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| {
                debug!("SocketProviderWorker finished with error {:?}", e)
            }),
        )
        .detach()
    }

    /// Spawns a socket worker for the given `transport`.
    ///
    /// The created worker will serve the appropriate FIDL protocol over `channel`
    /// and send [`Event::SocketEvent`] variants to the `event_loop`'s main mpsc
    /// channel.
    fn spawn_worker(
        &self,
        net_proto: IpVersion,
        transport: TransProto,
        channel: fasync::Channel,
        properties: SocketWorkerProperties,
    ) -> Result<(), Errno> {
        match transport {
            TransProto::Udp => udp::spawn_worker(
                net_proto,
                self.ctx.clone(),
                psocket::DatagramSocketRequestStream::from_channel(channel),
                properties,
            ),
            _ => Err(Errno::Eafnosupport),
        }
    }

    /// Handles a `fuchsia.posix.socket.Provider` FIDL request in `req`.
    async fn handle_fidl_socket_provider_request(&self, req: psocket::ProviderRequest) {
        match req {
            psocket::ProviderRequest::InterfaceIndexToName { index: _, responder } => {
                // TODO(https://fxbug.dev/48969): implement this method.
                responder_send!(responder, &mut Err(zx::Status::NOT_FOUND.into_raw()));
            }
            psocket::ProviderRequest::InterfaceNameToIndex { name: _, responder } => {
                // TODO(https://fxbug.dev/48969): implement this method.
                responder_send!(responder, &mut Err(zx::Status::NOT_FOUND.into_raw()));
            }
            psocket::ProviderRequest::StreamSocket { domain: _, proto: _, responder } => {
                responder_send!(responder, &mut Err(Errno::Eprotonosupport));
            }
            psocket::ProviderRequest::DatagramSocket { domain, proto, responder } => {
                responder_send!(
                    responder,
                    &mut self.socket::<psocket::DatagramSocketMarker, _>(domain, proto)
                );
            }
            psocket::ProviderRequest::GetInterfaceAddresses { responder } => {
                // TODO(https://fxbug.dev/54162): implement this method.
                responder_send!(responder, &mut std::iter::empty());
            }
        }
    }

    fn socket<T, P: TryInto<TransProto, Error = Errno>>(
        &self,
        domain: psocket::Domain,
        proto: P,
    ) -> Result<ClientEnd<T>, Errno> {
        let net_proto = match domain {
            psocket::Domain::Ipv4 => IpVersion::V4,
            psocket::Domain::Ipv6 => IpVersion::V6,
        };

        let (server, client) = zx::Channel::create().map_err(|_: zx::Status| Errno::Enobufs)?;
        self.spawn_worker(
            net_proto,
            proto.try_into()?,
            // we can safely unwrap here because we just created
            // this channel above.
            fasync::Channel::from_channel(server).unwrap(),
            SocketWorkerProperties {},
        )
        .map(|()| ClientEnd::<T>::new(client))
    }
}

/// A trait generalizing the data structures passed as arguments to POSIX socket
/// calls.
///
/// `SockAddr` implementers are typically passed to POSIX socket calls as a blob
/// of bytes. It represents a type that can be parsed from a C API `struct
/// sockaddr`, expressed as a stream of bytes.
pub(crate) trait SockAddr: std::fmt::Debug + Sized {
    /// The concrete address type for this `SockAddr`.
    type AddrType: IpAddress;
    /// The socket's domain.
    const DOMAIN: psocket::Domain;

    /// Creates a new `SockAddr`.
    ///
    /// Implementations must set their family field to `Self::FAMILY`.
    fn new(addr: Self::AddrType, port: u16) -> Self;

    /// Gets this `SockAddr`'s address.
    fn addr(&self) -> Self::AddrType;

    /// Set this [`SockAddr`]'s address.
    fn set_addr(&mut self, addr: Self::AddrType);

    /// Gets this `SockAddr`'s port.
    fn port(&self) -> u16;

    /// Set this [`SockAddr`]'s port.
    fn set_port(&mut self, port: u16);

    /// Gets a `SpecifiedAddr` witness type for this `SockAddr`'s address.
    fn get_specified_addr(&self) -> Option<SpecifiedAddr<Self::AddrType>> {
        SpecifiedAddr::<Self::AddrType>::new(self.addr())
    }

    /// Gets a `NonZeroU16` witness type for this `SockAddr`'s port.
    fn get_specified_port(&self) -> Option<NonZeroU16> {
        NonZeroU16::new(self.port())
    }

    /// Converts this `SockAddr` into an [`fnet::SocketAddress`].
    fn into_sock_addr(self) -> fnet::SocketAddress;

    /// Converts an [`fnet::SocketAddress`] into a `SockAddr`.
    fn from_sock_addr(addr: fnet::SocketAddress) -> Result<Self, Errno>;
}

impl SockAddr for fnet::Ipv6SocketAddress {
    type AddrType = Ipv6Addr;
    const DOMAIN: psocket::Domain = psocket::Domain::Ipv6;

    /// Creates a new `SockAddr6`.
    fn new(addr: Ipv6Addr, port: u16) -> Self {
        fnet::Ipv6SocketAddress { address: addr.into_fidl(), port, zone_index: 0 }
    }

    fn addr(&self) -> Ipv6Addr {
        self.address.into_core()
    }

    fn set_addr(&mut self, addr: Ipv6Addr) {
        self.address = addr.into_fidl();
    }

    fn port(&self) -> u16 {
        self.port
    }

    fn set_port(&mut self, port: u16) {
        self.port = port
    }

    fn into_sock_addr(self) -> fnet::SocketAddress {
        fnet::SocketAddress::Ipv6(self)
    }

    fn from_sock_addr(addr: fnet::SocketAddress) -> Result<Self, Errno> {
        match addr {
            fnet::SocketAddress::Ipv6(a) => Ok(a),
            fnet::SocketAddress::Ipv4(_) => Err(Errno::Eafnosupport),
        }
    }
}

impl SockAddr for fnet::Ipv4SocketAddress {
    type AddrType = Ipv4Addr;
    const DOMAIN: psocket::Domain = psocket::Domain::Ipv4;

    /// Creates a new `SockAddr4`.
    fn new(addr: Ipv4Addr, port: u16) -> Self {
        fnet::Ipv4SocketAddress { address: addr.into_fidl(), port }
    }

    fn addr(&self) -> Ipv4Addr {
        self.address.into_core()
    }

    fn set_addr(&mut self, addr: Ipv4Addr) {
        self.address = addr.into_fidl();
    }

    fn port(&self) -> u16 {
        self.port
    }

    fn set_port(&mut self, port: u16) {
        self.port = port
    }

    fn into_sock_addr(self) -> fnet::SocketAddress {
        fnet::SocketAddress::Ipv4(self)
    }

    fn from_sock_addr(addr: fnet::SocketAddress) -> Result<Self, Errno> {
        match addr {
            fnet::SocketAddress::Ipv4(a) => Ok(a),
            fnet::SocketAddress::Ipv6(_) => Err(Errno::Eafnosupport),
        }
    }
}

/// Extension trait that associates a [`SockAddr`] implementation to an IP
/// version. We provide implementations for [`Ipv4`] and [`Ipv6`].
pub(crate) trait IpSockAddrExt: Ip {
    type SocketAddress: SockAddr<AddrType = Self::Addr>;
}

impl IpSockAddrExt for Ipv4 {
    type SocketAddress = fnet::Ipv4SocketAddress;
}

impl IpSockAddrExt for Ipv6 {
    type SocketAddress = fnet::Ipv6SocketAddress;
}

#[cfg(test)]
mod testutil {
    use net_types::ip::{AddrSubnetEither, IpAddr};

    use super::*;

    /// A trait that exposes common test behavior to implementers of
    /// [`SockAddr`].
    pub(crate) trait TestSockAddr: SockAddr {
        /// A different domain.
        ///
        /// `Ipv4SocketAddress` defines it as `Ipv6SocketAddress` and
        /// vice-versa.
        type DifferentDomain: TestSockAddr;
        /// The local address used for tests.
        const LOCAL_ADDR: Self::AddrType;
        /// The remote address used for tests.
        const REMOTE_ADDR: Self::AddrType;
        /// An alternate remote address used for tests.
        const REMOTE_ADDR_2: Self::AddrType;
        /// An non-local address which is unreachable, used for tests.
        const UNREACHABLE_ADDR: Self::AddrType;

        /// The default subnet prefix used for tests.
        const DEFAULT_PREFIX: u8;

        /// Creates an [`fnet::SocketAddress`] with the given `addr` and `port`.
        fn create(addr: Self::AddrType, port: u16) -> fnet::SocketAddress {
            Self::new(addr, port).into_sock_addr()
        }

        /// Gets the local address and prefix configured for the test
        /// [`SockAddr`].
        fn config_addr_subnet() -> AddrSubnetEither {
            AddrSubnetEither::new(IpAddr::from(Self::LOCAL_ADDR), Self::DEFAULT_PREFIX).unwrap()
        }

        /// Gets the remote address and prefix to use for the test [`SockAddr`].
        fn config_addr_subnet_remote() -> AddrSubnetEither {
            AddrSubnetEither::new(IpAddr::from(Self::REMOTE_ADDR), Self::DEFAULT_PREFIX).unwrap()
        }
    }

    impl TestSockAddr for fnet::Ipv6SocketAddress {
        type DifferentDomain = fnet::Ipv4SocketAddress;

        const LOCAL_ADDR: Ipv6Addr =
            Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 1]);
        const REMOTE_ADDR: Ipv6Addr =
            Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 2]);
        const REMOTE_ADDR_2: Ipv6Addr =
            Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 3]);
        const UNREACHABLE_ADDR: Ipv6Addr =
            Ipv6Addr::new([0, 0, 0, 0, 0, 0, 0, 42, 0, 0, 0, 0, 192, 168, 0, 1]);
        const DEFAULT_PREFIX: u8 = 64;
    }

    impl TestSockAddr for fnet::Ipv4SocketAddress {
        type DifferentDomain = fnet::Ipv6SocketAddress;

        const LOCAL_ADDR: Ipv4Addr = Ipv4Addr::new([192, 168, 0, 1]);
        const REMOTE_ADDR: Ipv4Addr = Ipv4Addr::new([192, 168, 0, 2]);
        const REMOTE_ADDR_2: Ipv4Addr = Ipv4Addr::new([192, 168, 0, 3]);
        const UNREACHABLE_ADDR: Ipv4Addr = Ipv4Addr::new([192, 168, 42, 1]);
        const DEFAULT_PREFIX: u8 = 24;
    }
}

/// Trait expressing the conversion of error types into
/// [`fidl_fuchsia_posix::Errno`] errors for the POSIX-lite wrappers.
trait IntoErrno {
    /// Returns the most equivalent POSIX error code for `self`.
    fn into_errno(self) -> Errno;
}

impl IntoErrno for LocalAddressError {
    fn into_errno(self) -> Errno {
        match self {
            LocalAddressError::CannotBindToAddress
            | LocalAddressError::FailedToAllocateLocalPort => Errno::Eaddrnotavail,
            LocalAddressError::AddressMismatch => Errno::Einval,
            LocalAddressError::AddressInUse => Errno::Eaddrinuse,
        }
    }
}

impl IntoErrno for RemoteAddressError {
    fn into_errno(self) -> Errno {
        match self {
            RemoteAddressError::NoRoute => Errno::Enetunreach,
        }
    }
}

impl IntoErrno for SocketError {
    fn into_errno(self) -> Errno {
        match self {
            SocketError::Remote(e) => e.into_errno(),
            SocketError::Local(e) => e.into_errno(),
        }
    }
}

impl IntoErrno for UdpSendError {
    fn into_errno(self) -> Errno {
        match self {
            UdpSendError::Unknown => Errno::Eio,
            UdpSendError::Local(l) => l.into_errno(),
            UdpSendError::Remote(r) => r.into_errno(),
        }
    }
}

impl IntoErrno for NetstackError {
    fn into_errno(self) -> Errno {
        match self {
            NetstackError::Parse(_) => Errno::Einval,
            NetstackError::Exists => Errno::Ealready,
            NetstackError::NotFound => Errno::Efault,
            NetstackError::SendUdp(s) => s.into_errno(),
            NetstackError::Connect(c) => c.into_errno(),
            NetstackError::NoRoute => Errno::Ehostunreach,
            NetstackError::Mtu => Errno::Emsgsize,
        }
    }
}
