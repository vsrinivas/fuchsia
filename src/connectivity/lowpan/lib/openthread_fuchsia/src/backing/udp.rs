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

                // SAFETY: This method (`poll`) is guaranteed to only called from the same
                //         thread that OpenThread is being serviced on, which is ultimately
                //         the sole requirement for `PlatformBacking::as_ref()` to be
                //         considered safe.
                if let Some(thread_iface) = unsafe { PlatformBacking::as_ref().netif_index_thread }
                {
                    let scope_id = sock_addr.scope_id();
                    debug!("inbound scope_id = {}", scope_id);
                    info.set_host_interface(scope_id != thread_iface);
                }

                // TODO(fxbug.dev/93438): Set hop count. Figure out how to get this info.
                // TODO(fxbug.dev/93438): Set ECN. Need to figure out how to get this info.
                ot_udp_socket.handle_receive(message, &info);
            }
            Poll::Ready(Err(err)) => {
                return Poll::Ready(Err(err.into()));
            }
            Poll::Pending => {}
        }
    }
    Poll::Pending
}

trait UdpSocketHelpers {
    /// Gets a copy of the underlying ref-counted UdpSocket.
    fn get_async_udp_socket(&self) -> Option<&fasync_net::UdpSocket>;

    /// Sets the UdpSocket.
    fn set_async_udp_socket(&mut self, socket: fasync_net::UdpSocket);

    /// Drops the underlying UDP socket
    fn drop_async_udp_socket(&mut self);

    fn open(&mut self) -> ot::Result;
    fn close(&mut self) -> ot::Result;
    fn bind(&mut self) -> ot::Result;
    fn bind_to_netif(&mut self, netif: ot::NetifIdentifier) -> ot::Result;
    fn connect(&mut self) -> ot::Result;
    fn send(
        &mut self,
        message: OtMessageBox<'_>,
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
    fn get_async_udp_socket(&self) -> Option<&fasync_net::UdpSocket> {
        self.get_handle().map(|handle| {
            // SAFETY: The handle pointer always comes from the result from Box::leak(),
            //         so it is safe to cast back into a reference.
            unsafe { &*(handle.as_ptr() as *mut fasync_net::UdpSocket) }
        })
    }

    fn set_async_udp_socket(&mut self, socket: fasync_net::UdpSocket) {
        assert!(self.get_handle().is_none());

        let boxed = Box::new(socket);

        // Get a reference to our socket while "leaking" the containing box, and
        // then convert it to a pointer.
        // We will reconstitute our box to free the memory in `drop_async_udp_socket()`.
        let socket_ptr = Box::leak(boxed) as *mut fasync_net::UdpSocket;

        self.set_handle(Some(NonNull::new(socket_ptr as *mut c_void).unwrap()));
    }

    fn drop_async_udp_socket(&mut self) {
        if let Some(handle) = self.get_handle() {
            // Reconstitute our box from the pointer.
            // SAFETY: The pointer we are passing into `Box::from_raw` came from `Box::leak`.
            let boxed = unsafe {
                Box::<fasync_net::UdpSocket>::from_raw(handle.as_ptr() as *mut fasync_net::UdpSocket)
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
            socket2::Domain::IPV4,
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

        assert!(self.get_handle().is_some(), "Closing UDP socket that wasn't open");

        self.drop_async_udp_socket();

        Ok(())
    }

    fn bind(&mut self) -> ot::Result {
        debug!("otPlatUdp:{:?}: Bind to {:?}", self.as_ot_ptr(), self.sock_name());

        let socket = self.get_async_udp_socket().ok_or(ot::Error::Failed)?;
        let sockaddr: std::net::SocketAddr = self.sock_name().into();

        socket.as_ref().bind(&sockaddr.into()).map_err(move |err| {
            error!("Error: {:?}", err);
            ot::Error::Failed
        })?;

        socket.as_ref().set_only_v6(true).map_err(move |err| {
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
        debug!(
            "otPlatUdp:{:?}: Bind to netif={:?} (NOT YET IMPLEMENTED)",
            self.as_ot_ptr(),
            net_if_id
        );

        let _socket = self.get_async_udp_socket().ok_or(ot::Error::Failed)?;

        // TODO(fxbug.dev/93438): Actually bind to netif! IPV6_BOUND_IF, SO_BINDTODEVICE, etc..
        //       Would be nice if we could just set the scope id, but this gets
        //       called before our call to bind, so that does us no good.

        Ok(())
    }

    fn connect(&mut self) -> ot::Result {
        debug!(
            "otPlatUdp:{:?}: Connect to {:?} (NOT YET IMPLEMENTED)",
            self.as_ot_ptr(),
            self.peer_name()
        );

        // TODO(fxbug.dev/93438): Investigate implications of leaving this unimplemented.
        //                        It's not entirely clear why we have this call to connect
        //                        when we always specify a destination for `send`.

        Ok(())
    }

    fn send(&mut self, message: OtMessageBox<'_>, info: &'_ ot::message::Info) -> ot::Result {
        let data = message.to_vec();

        debug!(
            "otPlatUdp:{:?}: Sending {} byte packet to {:?}. {:?}",
            self.as_ot_ptr(),
            data.len(),
            self.peer_name(),
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

        let mut sockaddr: std::net::SocketAddrV6 = self.peer_name().into();

        if info.is_host_interface() {
            // SAFETY: Must only be called from the same thread that OpenThread is running on.
            //         This is guaranteed by the only caller of this method.
            let platform_backing = unsafe { PlatformBacking::as_ref() };
            let netif: ot::NetifIndex = platform_backing
                .lookup_netif_index(ot::NetifIdentifier::Backbone)
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
                warn!("otPlatUdpSend:{:?}: send_to failed: {:?}", self.as_ot_ptr(), err);
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
        debug!(
            "otPlatUdp:{:?}: JoinMulticastGroup {:?} on netif {:?} (Unimplemented)",
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
        debug!(
            "otPlatUdp:{:?}: LeaveMulticastGroup {:?} on netif {:?} (Unimplemented)",
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
            OtMessageBox::from_ot_ptr(message).unwrap(),
            ot::message::Info::ref_from_ot_ptr(message_info).unwrap(),
        )
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
