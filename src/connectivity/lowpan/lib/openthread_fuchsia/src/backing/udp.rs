// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use fuchsia_async::net as fasync_net;
use futures::never::Never;
use openthread::ot::OtCastable;
use openthread_sys::*;
use std::ffi::c_void;
use std::ptr::NonNull;
use std::task::Poll;

// TODO(fxbug.dev/93438): At some point plumb this to OPENTHREAD_CONFIG_IP6_HOP_LIMIT_DEFAULT
const DEFAULT_HOP_LIMIT: u8 = 64;

pub(crate) fn poll_ot_udp_socket(
    ot_udp_socket: &ot::UdpSocket<'_>,
    instance: &ot::Instance,
    cx: &mut std::task::Context<'_>,
) -> Poll<Result<Never, anyhow::Error>> {
    let socket = ot_udp_socket.get_async_udp_socket();
    if let Some(socket) = socket {
        let mut buffer = [0u8; crate::UDP_PACKET_MAX_LENGTH];
        match socket.async_recv_from(&mut buffer, cx) {
            Poll::Ready(Ok((len, sock_addr))) => {
                let sock_addr = sock_addr.as_socket_ipv6().ok_or_else(|| {
                    anyhow::format_err!("Expected IPv6 sockaddr, got something else")
                })?;
                let ot_sockaddr: ot::SockAddr = sock_addr.into();

                debug!(
                    "otPlatUdp:{:?}: Incoming {} byte packet from {:?}",
                    ot_udp_socket.as_ot_ptr(),
                    len,
                    ot_sockaddr
                );

                let mut message = ot::Message::udp_new(instance, None)?;
                message.append(&buffer[..len])?;
                let mut info = ot::message::Info::new(ot_udp_socket.sock_name(), ot_sockaddr);

                if ot_udp_socket.get_netif_id() == ot::NetifIdentifier::Backbone {
                    info.set_host_interface(true);
                } else if ot_udp_socket.get_netif_id() == ot::NetifIdentifier::Thread {
                    info.set_host_interface(false);
                } else if let Some(host_iface) = unsafe {
                    // SAFETY: This method (`poll`) is guaranteed to only be called from the same
                    //         thread that OpenThread is being serviced on, which is ultimately
                    //         the sole requirement for `PlatformBacking::as_ref()` to be
                    //         considered safe.
                    PlatformBacking::as_ref().lookup_netif_index(ot::NetifIdentifier::Backbone)
                } {
                    let scope_id = sock_addr.scope_id();
                    debug!("inbound scope_id = {}, host_iface = {}", scope_id, host_iface);
                    info.set_host_interface(scope_id == host_iface);
                }

                // TODO(fxbug.dev/93438): Set hop count. Figure out how to get this info.
                // TODO(fxbug.dev/93438): Set ECN. Need to figure out how to get this info.
                ot_udp_socket.handle_receive(&message, &info);
            }
            Poll::Ready(Err(err)) => {
                return Poll::Ready(Err(err.into()));
            }
            Poll::Pending => {}
        }
    }
    Poll::Pending
}

struct UdpSocketBacking {
    socket: fasync_net::UdpSocket,
    netif_id: ot::NetifIdentifier,
}

/// Returns true if this sockaddr needs a scope.
fn dest_needs_scope(sockaddr: &std::net::SocketAddrV6) -> bool {
    let dest_is_unicast_link_local = (sockaddr.ip().segments()[0] & 0xffc0) == 0xfe80;
    let dest_is_multicast_link_local = sockaddr.ip().segments()[0] == 0xff02;
    let dest_is_multicast_realm_local = sockaddr.ip().segments()[0] == 0xff03;

    sockaddr.scope_id() == 0
        && (dest_is_unicast_link_local
            | dest_is_multicast_link_local
            | dest_is_multicast_realm_local)
}

trait UdpSocketHelpers {
    /// Gets a copy of the underlying ref-counted UdpSocketBacking.
    fn get_async_udp_socket_backing(&self) -> Option<&UdpSocketBacking>;

    /// Gets a copy of the underlying ref-counted UdpSocket.
    fn get_async_udp_socket(&self) -> Option<&fasync_net::UdpSocket>;

    /// Sets the UdpSocket.
    fn set_async_udp_socket(&mut self, socket: fasync_net::UdpSocket);

    /// Drops the underlying UDP socket
    fn drop_async_udp_socket(&mut self);

    fn get_netif_id(&self) -> ot::NetifIdentifier;
    fn set_netif_id(&mut self, netif_id: ot::NetifIdentifier);

    fn open(&mut self) -> ot::Result;
    fn close(&mut self) -> ot::Result;
    fn bind(&mut self) -> ot::Result;
    fn bind_to_netif(&mut self, netif: ot::NetifIdentifier) -> ot::Result;
    fn connect(&mut self) -> ot::Result;
    fn send(
        &mut self,
        message: &'_ ot::Message<'_>,
        message_info: &'_ ot::message::Info,
    ) -> ot::Result;
    fn join_mcast_group(&mut self, netif: ot::NetifIdentifier, addr: &ot::Ip6Address)
        -> ot::Result;
    fn leave_mcast_group(
        &mut self,
        netif: ot::NetifIdentifier,
        addr: &ot::Ip6Address,
    ) -> ot::Result;
}

impl UdpSocketHelpers for ot::UdpSocket<'_> {
    fn get_async_udp_socket_backing(&self) -> Option<&UdpSocketBacking> {
        self.get_handle().map(|handle| {
            // SAFETY: The handle pointer always comes from the result from Box::leak(),
            //         so it is safe to cast back into a reference.
            unsafe { &*(handle.as_ptr() as *mut UdpSocketBacking) }
        })
    }

    fn get_netif_id(&self) -> ot::NetifIdentifier {
        self.get_async_udp_socket_backing()
            .map(|x| x.netif_id)
            .unwrap_or(ot::NetifIdentifier::Unspecified)
    }

    fn set_netif_id(&mut self, netif_id: ot::NetifIdentifier) {
        self.get_handle()
            .map(|handle| {
                // SAFETY: The handle pointer always comes from the result from Box::leak(),
                //         so it is safe to cast back into a reference.
                unsafe { &mut *(handle.as_ptr() as *mut UdpSocketBacking) }
            })
            .unwrap()
            .netif_id = netif_id;
    }

    fn get_async_udp_socket(&self) -> Option<&fasync_net::UdpSocket> {
        self.get_async_udp_socket_backing().map(|x| &x.socket)
    }

    fn set_async_udp_socket(&mut self, socket: fasync_net::UdpSocket) {
        assert!(self.get_handle().is_none());

        let socket_backing =
            UdpSocketBacking { socket, netif_id: ot::NetifIdentifier::Unspecified };

        let boxed = Box::new(socket_backing);

        // Get a reference to our socket while "leaking" the containing box, and
        // then convert it to a pointer.
        // We will reconstitute our box to free the memory in `drop_async_udp_socket()`.
        let socket_ptr = Box::leak(boxed) as *mut UdpSocketBacking;

        self.set_handle(Some(NonNull::new(socket_ptr as *mut c_void).unwrap()));
    }

    fn drop_async_udp_socket(&mut self) {
        if let Some(handle) = self.get_handle() {
            // Reconstitute our box from the pointer.
            // SAFETY: The pointer we are passing into `Box::from_raw` came from `Box::leak`.
            let boxed = unsafe {
                Box::<UdpSocketBacking>::from_raw(handle.as_ptr() as *mut UdpSocketBacking)
            };

            // Explicitly drop the box for clarity.
            std::mem::drop(boxed);

            self.set_handle(None);
        }
    }

    fn open(&mut self) -> ot::Result {
        debug!("otPlatUdp:{:?}: Opening", self.as_ot_ptr());

        if self.get_handle().is_some() {
            warn!("otPlatUdp:{:?}: Tried to open already open socket", self.as_ot_ptr());
            return Err(ot::Error::Already);
        }

        let socket = socket2::Socket::new(
            socket2::Domain::IPV6,
            socket2::Type::DGRAM,
            Some(socket2::Protocol::UDP),
        )
        .map_err(|err| {
            error!("Error: {:?}", err);
            Err(ot::Error::Failed)
        })?;

        let socket = fasync_net::UdpSocket::from_socket(socket.into()).map_err(|err| {
            error!("Error: {:?}", err);
            Err(ot::Error::Failed)
        })?;

        self.set_async_udp_socket(socket);

        Ok(())
    }

    fn close(&mut self) -> ot::Result {
        debug!("otPlatUdp:{:?}: Closing", self.as_ot_ptr());

        if self.get_handle().is_none() {
            warn!("otPlatUdp:{:?}: Tried to close already closed socket", self.as_ot_ptr());
            return Err(ot::Error::Already);
        }

        self.drop_async_udp_socket();

        Ok(())
    }

    fn bind(&mut self) -> ot::Result {
        if self.get_handle().is_none() {
            warn!("otPlatUdp:{:?}: Cannot bind, socket is closed.", self.as_ot_ptr());
            return Err(ot::Error::InvalidState);
        }

        let mut sockaddr: std::net::SocketAddrV6 = self.sock_name().into();

        // SAFETY: Must only be called from the same thread that OpenThread is running on.
        //         This is guaranteed by the only caller of this method.
        let platform_backing = unsafe { PlatformBacking::as_ref() };

        if let Some(netif) = platform_backing.lookup_netif_index(self.get_netif_id()) {
            sockaddr.set_scope_id(netif);
        }

        debug!("otPlatUdp:{:?}: Bind to {}", self.as_ot_ptr(), sockaddr);

        let socket = self.get_async_udp_socket().ok_or(ot::Error::Failed)?;
        socket.as_ref().bind(&sockaddr.into()).map_err(move |err| {
            error!("Error: {:?}", err);
            ot::Error::Failed
        })?;

        socket.as_ref().set_ttl(DEFAULT_HOP_LIMIT.into()).map_err(move |err| {
            error!("Error: {:?}", err);
            ot::Error::Failed
        })?;

        Ok(())
    }

    fn bind_to_netif(&mut self, net_if_id: ot::NetifIdentifier) -> ot::Result {
        if self.get_handle().is_none() {
            warn!("otPlatUdp:{:?}: Cannot bind_to_netif, socket is closed.", self.as_ot_ptr());
            return Err(ot::Error::InvalidState);
        }

        debug!("otPlatUdp:{:?}: Bind to netif={:?}", self.as_ot_ptr(), net_if_id);

        self.set_netif_id(net_if_id);

        Ok(())
    }

    fn connect(&mut self) -> ot::Result {
        if self.get_handle().is_none() {
            warn!("otPlatUdp:{:?}: Cannot connect, socket is closed.", self.as_ot_ptr());
            return Err(ot::Error::InvalidState);
        }

        debug!("otPlatUdp:{:?}: Connect to {:?}", self.as_ot_ptr(), self.peer_name());

        // TODO(fxbug.dev/93438): Investigate implications of leaving this unimplemented.
        //                        It's not entirely clear why we have this call to connect
        //                        when we always specify a destination for `send`.

        Ok(())
    }

    fn send(&mut self, message: &ot::Message<'_>, info: &'_ ot::message::Info) -> ot::Result {
        if self.get_handle().is_none() {
            warn!("otPlatUdp:{:?}: Cannot send, socket is closed.", self.as_ot_ptr());
            return Err(ot::Error::InvalidState);
        }

        let data = message.to_vec();

        debug!(
            "otPlatUdp:{:?}: Sending {} byte packet to {:?}. {:?}",
            self.as_ot_ptr(),
            data.len(),
            info.peer_name(),
            info
        );

        let socket = self.get_async_udp_socket().ok_or(ot::Error::Failed)?;

        // Set the multicast loop flag.
        if info.multicast_loop() {
            socket.as_ref().set_multicast_loop_v6(true).map_err(move |err| {
                error!("Error: {:?}", err);
                ot::Error::Failed
            })?;
        }

        let should_set_hop_limit = info.hop_limit() > 0 || info.allow_zero_hop_limit();

        if should_set_hop_limit {
            socket.as_ref().set_ttl(info.hop_limit().into()).map_err(move |err| {
                error!("Error: {:?}", err);
                ot::Error::Failed
            })?;
        }

        let mut sockaddr: std::net::SocketAddrV6 = info.peer_name().into();

        if self.get_netif_id() == ot::NetifIdentifier::Unspecified && dest_needs_scope(&sockaddr) {
            let netif_id = if info.is_host_interface() {
                ot::NetifIdentifier::Backbone
            } else {
                ot::NetifIdentifier::Thread
            };

            // SAFETY: Must only be called from the same thread that OpenThread is running on.
            //         This is guaranteed by the only caller of this method.
            let platform_backing = unsafe { PlatformBacking::as_ref() };

            let netif: ot::NetifIndex = platform_backing
                .lookup_netif_index(netif_id)
                .unwrap_or(ot::NETIF_INDEX_UNSPECIFIED);
            sockaddr.set_scope_id(netif);
        }

        let ret = match socket.as_ref().send_to(&data, &sockaddr.into()) {
            Ok(sent) if data.len() == sent => Ok(()),
            Ok(sent) => {
                warn!(
                    "otPlatUdpSend:{:?}: send_to did not send whole packet, only sent {} bytes",
                    self.as_ot_ptr(),
                    sent
                );
                Err(ot::Error::Failed)
            }
            Err(err) => {
                warn!(
                    "otPlatUdpSend:{:?}: send_to({:?}) failed: {:?}",
                    self.as_ot_ptr(),
                    sockaddr,
                    err
                );
                Err(ot::Error::Failed)
            }
        };

        // Restore hop limit
        if should_set_hop_limit {
            socket.as_ref().set_ttl(DEFAULT_HOP_LIMIT.into()).map_err(move |err| {
                error!("Error: {:?}", err);
                ot::Error::Failed
            })?;
        }

        // Reset the multicast loop flag.
        if info.multicast_loop() {
            socket.as_ref().set_multicast_loop_v6(false).map_err(move |err| {
                error!("Error: {:?}", err);
                ot::Error::Failed
            })?;
        }

        ret
    }

    fn join_mcast_group(
        &mut self,
        netif: ot::NetifIdentifier,
        addr: &ot::Ip6Address,
    ) -> ot::Result {
        if self.get_handle().is_none() {
            warn!("otPlatUdp:{:?}: Cannot join_mcast_group, socket is closed.", self.as_ot_ptr());
            return Err(ot::Error::InvalidState);
        }

        debug!(
            "otPlatUdp:{:?}: JoinMulticastGroup {:?} on netif {:?}",
            self.as_ot_ptr(),
            addr,
            netif
        );

        let socket = self.get_async_udp_socket().ok_or(ot::Error::Failed)?;

        // SAFETY: Must only be called from the same thread that OpenThread is running on.
        //         This is guaranteed by the only caller of this method.
        let platform_backing = unsafe { PlatformBacking::as_ref() };
        let netif: ot::NetifIndex =
            platform_backing.lookup_netif_index(netif).unwrap_or(ot::NETIF_INDEX_UNSPECIFIED);

        socket.as_ref().join_multicast_v6(addr, netif).map_err(move |err| {
            error!("Error: {:?}", err);
            ot::Error::Failed
        })?;

        Ok(())
    }

    fn leave_mcast_group(
        &mut self,
        netif: ot::NetifIdentifier,
        addr: &ot::Ip6Address,
    ) -> ot::Result {
        if self.get_handle().is_none() {
            warn!("otPlatUdp:{:?}: Cannot leave_mcast_group, socket is closed.", self.as_ot_ptr());
            return Err(ot::Error::InvalidState);
        }

        debug!(
            "otPlatUdp:{:?}: LeaveMulticastGroup {:?} on netif {:?}",
            self.as_ot_ptr(),
            addr,
            netif
        );
        let socket = self.get_async_udp_socket().ok_or(ot::Error::Failed)?;

        // SAFETY: Must only be called from the same thread that OpenThread is running on.
        //         This is guaranteed by the only caller of this method.
        let platform_backing = unsafe { PlatformBacking::as_ref() };
        let netif: ot::NetifIndex =
            platform_backing.lookup_netif_index(netif).unwrap_or(ot::NETIF_INDEX_UNSPECIFIED);

        socket.as_ref().leave_multicast_v6(addr, netif).map_err(move |err| {
            error!("Error: {:?}", err);
            ot::Error::Failed
        })?;

        Ok(())
    }
}

#[no_mangle]
unsafe extern "C" fn otPlatUdpSocket(ot_socket_ptr: *mut otUdpSocket) -> otError {
    ot::UdpSocket::mut_from_ot_mut_ptr(ot_socket_ptr).unwrap().open().into_ot_error()
}

#[no_mangle]
unsafe extern "C" fn otPlatUdpClose(ot_socket_ptr: *mut otUdpSocket) -> otError {
    ot::UdpSocket::mut_from_ot_mut_ptr(ot_socket_ptr).unwrap().close().into_ot_error()
}

#[no_mangle]
unsafe extern "C" fn otPlatUdpBind(ot_socket_ptr: *mut otUdpSocket) -> otError {
    ot::UdpSocket::mut_from_ot_mut_ptr(ot_socket_ptr).unwrap().bind().into_ot_error()
}

#[no_mangle]
unsafe extern "C" fn otPlatUdpBindToNetif(
    ot_socket_ptr: *mut otUdpSocket,
    net_if_id: otNetifIdentifier,
) -> otError {
    ot::UdpSocket::mut_from_ot_mut_ptr(ot_socket_ptr)
        .unwrap()
        .bind_to_netif(ot::NetifIdentifier::from(net_if_id))
        .into_ot_error()
}

#[no_mangle]
unsafe extern "C" fn otPlatUdpConnect(ot_socket_ptr: *mut otUdpSocket) -> otError {
    ot::UdpSocket::mut_from_ot_mut_ptr(ot_socket_ptr).unwrap().connect().into_ot_error()
}

#[no_mangle]
unsafe extern "C" fn otPlatUdpSend(
    ot_socket_ptr: *mut otUdpSocket,
    message: *mut otMessage,
    message_info: *const otMessageInfo,
) -> otError {
    ot::UdpSocket::mut_from_ot_mut_ptr(ot_socket_ptr)
        .unwrap()
        .send(
            ot::Message::ref_from_ot_ptr(message).unwrap(),
            ot::message::Info::ref_from_ot_ptr(message_info).unwrap(),
        )
        .map(move |_| otMessageFree(message)) // Only free on success
        .into_ot_error()
}

#[no_mangle]
unsafe extern "C" fn otPlatUdpJoinMulticastGroup(
    ot_socket_ptr: *mut otUdpSocket,
    net_if_id: otNetifIdentifier,
    addr: *const otIp6Address,
) -> otError {
    ot::UdpSocket::mut_from_ot_mut_ptr(ot_socket_ptr)
        .unwrap()
        .join_mcast_group(net_if_id.into(), ot::Ip6Address::ref_from_ot_ptr(addr).unwrap())
        .into_ot_error()
}

#[no_mangle]
unsafe extern "C" fn otPlatUdpLeaveMulticastGroup(
    ot_socket_ptr: *mut otUdpSocket,
    net_if_id: otNetifIdentifier,
    addr: *const otIp6Address,
) -> otError {
    ot::UdpSocket::mut_from_ot_mut_ptr(ot_socket_ptr)
        .unwrap()
        .leave_mcast_group(net_if_id.into(), ot::Ip6Address::ref_from_ot_ptr(addr).unwrap())
        .into_ot_error()
}

#[cfg(test)]
mod test {
    use super::*;
    use std::net::Ipv6Addr;
    use std::net::SocketAddrV6;

    #[test]
    fn test_dest_needs_scope() {
        assert_eq!(
            dest_needs_scope(&SocketAddrV6::new(
                Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 1),
                8080,
                0,
                0
            )),
            false
        );
        assert_eq!(
            dest_needs_scope(&SocketAddrV6::new(
                Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 1),
                8080,
                0,
                1
            )),
            false
        );

        assert_eq!(
            dest_needs_scope(&SocketAddrV6::new(
                Ipv6Addr::new(0xfe80, 0xdb8, 0, 0, 0, 0, 0, 1),
                8080,
                0,
                0
            )),
            true
        );
        assert_eq!(
            dest_needs_scope(&SocketAddrV6::new(
                Ipv6Addr::new(0xfe80, 0xdb8, 0, 0, 0, 0, 0, 1),
                8080,
                0,
                1
            )),
            false
        );

        assert_eq!(
            dest_needs_scope(&SocketAddrV6::new(
                Ipv6Addr::new(0xff05, 0xdb8, 0, 0, 0, 0, 0, 1),
                8080,
                0,
                0
            )),
            false
        );
        assert_eq!(
            dest_needs_scope(&SocketAddrV6::new(
                Ipv6Addr::new(0xff05, 0xdb8, 0, 0, 0, 0, 0, 1),
                8080,
                0,
                1
            )),
            false
        );

        assert_eq!(
            dest_needs_scope(&SocketAddrV6::new(
                Ipv6Addr::new(0xff03, 0xdb8, 0, 0, 0, 0, 0, 1),
                8080,
                0,
                0
            )),
            true
        );
        assert_eq!(
            dest_needs_scope(&SocketAddrV6::new(
                Ipv6Addr::new(0xff03, 0xdb8, 0, 0, 0, 0, 0, 1),
                8080,
                0,
                1
            )),
            false
        );

        assert_eq!(
            dest_needs_scope(&SocketAddrV6::new(
                Ipv6Addr::new(0xff02, 0xdb8, 0, 0, 0, 0, 0, 1),
                8080,
                0,
                0
            )),
            true
        );
        assert_eq!(
            dest_needs_scope(&SocketAddrV6::new(
                Ipv6Addr::new(0xff02, 0xdb8, 0, 0, 0, 0, 0, 1),
                8080,
                0,
                1
            )),
            false
        );

        assert_eq!(
            dest_needs_scope(&SocketAddrV6::new(
                Ipv6Addr::new(0xff01, 0xdb8, 0, 0, 0, 0, 0, 1),
                8080,
                0,
                0
            )),
            false
        );
        assert_eq!(
            dest_needs_scope(&SocketAddrV6::new(
                Ipv6Addr::new(0xff01, 0xdb8, 0, 0, 0, 0, 0, 1),
                8080,
                0,
                1
            )),
            false
        );
    }
}
