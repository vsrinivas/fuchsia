// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! UDP socket bindings.

use std::sync::{Arc, Mutex};

use failure::{format_err, Error};
use fidl_fuchsia_posix_socket as psocket;
use fuchsia_async as fasync;
use fuchsia_zircon::{self as zx, prelude::HandleBased};
use futures::{channel::mpsc, TryFutureExt, TryStreamExt};
use log::{debug, error, trace};
use net_types::ip::{Ip, IpVersion, Ipv4, Ipv6};
use netstack3_core::{
    connect_udp, listen_udp, remove_udp_conn, remove_udp_listener, UdpConnId, UdpListenerId,
};

use super::{IpSockAddrExt, SockAddr, SocketEventInner, SocketWorkerProperties};
use crate::{eventloop::Event, EventLoop};

/// A socket worker for either an IPv4 or IPv6 socket.
#[derive(Debug, Clone)]
enum SocketWorkerEither {
    V4(Arc<Mutex<SocketWorkerInner<Ipv4>>>),
    V6(Arc<Mutex<SocketWorkerInner<Ipv6>>>),
}

/// Worker that serves the fuchsia.posix.socket.Control FIDL.
pub(super) struct UdpSocketWorker {
    events: psocket::ControlRequestStream,
    inner: SocketWorkerEither,
}

/// Internal state of a [`UdpSocketWorker`].
#[derive(Debug)]
struct SocketWorkerInner<I: IpSockAddrExt> {
    local_socket: zx::Socket,
    peer_socket: zx::Socket,
    info: SocketControlInfo<I>,
}

/// A socket event for UDP sockets
#[derive(Debug)]
pub(super) struct UdpSocketEvent {
    worker: SocketWorkerEither,
    request: psocket::ControlRequest,
}

impl UdpSocketEvent {
    /// Handles this `UdpSocketEvent` on the provided `event_loop`
    /// context.
    pub(super) fn handle_event(self, event_loop: &mut EventLoop) {
        match self.worker {
            SocketWorkerEither::V4(sw) => {
                sw.lock().unwrap().handle_request(event_loop, self.request)
            }
            SocketWorkerEither::V6(sw) => {
                sw.lock().unwrap().handle_request(event_loop, self.request)
            }
        }
    }
}

impl From<UdpSocketEvent> for Event {
    fn from(req: UdpSocketEvent) -> Self {
        SocketEventInner::Udp(req).into()
    }
}

/// Information on socket control plane.
#[derive(Debug)]
pub struct SocketControlInfo<I: Ip> {
    properties: SocketWorkerProperties,
    state: SocketState<I>,
}

/// Possible states for a UDP socket.
#[derive(Debug)]
enum SocketState<I: Ip> {
    Unbound,
    BoundListen { listener_id: UdpListenerId<I> },
    BoundConnect { conn_id: UdpConnId<I> },
}

impl<I: Ip> SocketState<I> {
    fn is_bound(&self) -> bool {
        match self {
            SocketState::Unbound => false,
            SocketState::BoundListen { .. } | SocketState::BoundConnect { .. } => true,
        }
    }
}

impl UdpSocketWorker {
    /// Creates a new `UdpSocketWorker` with the provided arguments.
    ///
    /// The `UdpSocketWorker` takes control of the event stream in
    /// `events`. To start servicing the events, see
    /// [`UdpSocketWorker::spawn`].
    pub fn new(
        ip_version: IpVersion,
        events: psocket::ControlRequestStream,
        properties: SocketWorkerProperties,
    ) -> Result<Self, libc::c_int> {
        let (local_socket, peer_socket) =
            zx::Socket::create(zx::SocketOpts::DATAGRAM).map_err(|_| libc::ENOBUFS)?;
        Ok(Self {
            events,
            inner: match ip_version {
                IpVersion::V4 => SocketWorkerEither::V4(Arc::new(Mutex::new(
                    SocketWorkerInner::new(local_socket, peer_socket, properties),
                ))),
                IpVersion::V6 => SocketWorkerEither::V6(Arc::new(Mutex::new(
                    SocketWorkerInner::new(local_socket, peer_socket, properties),
                ))),
            },
        })
    }

    /// Starts servicing events from the event stream this
    /// `UdpSocketWorker` was created with.
    ///
    /// Socket control events will be sent to the receiving end of `sender` as
    /// [`Event::SocketEvent`] variants.
    pub fn spawn(mut self, sender: mpsc::UnboundedSender<Event>) {
        fasync::spawn_local(
            async move {
                while let Some(request) = self.events.try_next().await? {
                    let () = sender
                        .unbounded_send(
                            UdpSocketEvent { worker: self.inner.clone(), request }.into(),
                        )
                        .map_err(|e| format_err!("Failed to send socket ControlRequest {:?}", e))?;
                }
                Ok(())
            }
                // When the closure above finishes, that means `self` goes out
                // of scope and is dropped, meaning that the event stream's
                // underlying channel is closed.
                // If any errors occured as a result of the closure, we just log
                // them.
                .unwrap_or_else(|e: Error| error!("UDP socket control request error: {:?}", e)),
        );
    }
}

impl<I: IpSockAddrExt> SocketWorkerInner<I> {
    /// Creates a new `SocketWorkerInner` with the provided socket pair and
    /// `properties`.
    fn new(
        local_socket: zx::Socket,
        peer_socket: zx::Socket,
        properties: SocketWorkerProperties,
    ) -> Self {
        Self {
            local_socket,
            peer_socket,
            info: SocketControlInfo { properties, state: SocketState::Unbound },
        }
    }

    /// Handles a [POSIX socket connect request].
    ///
    /// [POSIX socket connect request]: psocket::ControlRequest::Connect
    fn connect(&mut self, event_loop: &mut EventLoop, addr: Vec<u8>) -> Result<(), libc::c_int> {
        let sockaddr = I::SocketAddress::parse(addr.as_slice()).ok_or(libc::EFAULT)?;
        trace!("connect UDP sockaddr: {:?}", sockaddr);
        if sockaddr.family() != I::SocketAddress::FAMILY {
            return Err(libc::EAFNOSUPPORT);
        }
        let remote_port = sockaddr.get_specified_port().ok_or(libc::ECONNREFUSED)?;
        let remote_addr = sockaddr.get_specified_addr().ok_or(libc::EINVAL)?;

        let (local_addr, local_port) = match self.info.state {
            SocketState::Unbound => {
                // do nothing, we're already unbound.
                // return None for local_addr and local_port.
                (None, None)
            }
            SocketState::BoundListen { listener_id } => {
                // if we're bound to a listen mode, we need to remove the
                // listener, and retrieve the bound local addr and port.
                let list_info = remove_udp_listener(&mut event_loop.ctx, listener_id);

                (list_info.local_addr, Some(list_info.local_port))
            }
            SocketState::BoundConnect { conn_id } => {
                // if we're bound to a connect mode, we need to remove the
                // connection, and retrieve the bound local addr and port.
                let conn_info = remove_udp_conn(&mut event_loop.ctx, conn_id);
                (Some(conn_info.local_addr), Some(conn_info.local_port))
            }
        };

        let conn_id =
            connect_udp(&mut event_loop.ctx, local_addr, local_port, remote_addr, remote_port);
        self.info.state = SocketState::BoundConnect { conn_id };

        Ok(())
    }

    /// Handles a [POSIX socket bind request]
    ///
    /// [POSIX socket bind request]: psocket::ControlRequest::Bind
    fn bind(&mut self, event_loop: &mut EventLoop, addr: Vec<u8>) -> Result<(), libc::c_int> {
        let sockaddr = I::SocketAddress::parse(addr.as_slice()).ok_or(libc::EFAULT)?;
        trace!("bind UDP sockaddr: {:?}", sockaddr);
        if sockaddr.family() != I::SocketAddress::FAMILY {
            return Err(libc::EAFNOSUPPORT);
        }
        if self.info.state.is_bound() {
            return Err(libc::EALREADY);
        }
        let local_addr = sockaddr.get_specified_addr();
        let local_port = sockaddr.get_specified_port();

        let listener_id = listen_udp(&mut event_loop.ctx, local_addr, local_port);

        self.info.state = SocketState::BoundListen { listener_id };

        Ok(())
    }

    /// Handles a [POSIX socket request].
    ///
    /// [POSIX socket request]: psocket::ControlRequest
    // TODO(fxb/37419): Remove default handling after methods landed.
    #[allow(unreachable_patterns)]
    fn handle_request(&mut self, event_loop: &mut EventLoop, req: psocket::ControlRequest) {
        match req {
            psocket::ControlRequest::Describe { responder } => {
                let peer = self.peer_socket.duplicate_handle(zx::Rights::SAME_RIGHTS);
                if let Ok(peer) = peer {
                    let mut info =
                        fidl_fuchsia_io::NodeInfo::Socket(fidl_fuchsia_io::Socket { socket: peer });
                    responder_send!(responder, &mut info);
                }
                // If the call to duplicate_handle fails, we have no choice but to drop the
                // responder and close the channel, since Describe must be infallible.
            }
            psocket::ControlRequest::Connect { addr, responder } => {
                responder_send!(
                    responder,
                    self.connect(event_loop, addr).err().unwrap_or(0) as i16
                );
            }
            psocket::ControlRequest::Clone { .. } => {}
            psocket::ControlRequest::Close { .. } => {}
            psocket::ControlRequest::Sync { .. } => {}
            psocket::ControlRequest::GetAttr { .. } => {}
            psocket::ControlRequest::SetAttr { .. } => {}
            psocket::ControlRequest::Bind { addr, responder } => {
                responder_send!(responder, self.bind(event_loop, addr).err().unwrap_or(0) as i16)
            }
            psocket::ControlRequest::Listen { .. } => {}
            psocket::ControlRequest::Accept { .. } => {}
            psocket::ControlRequest::GetSockName { .. } => {}
            psocket::ControlRequest::GetPeerName { .. } => {}
            psocket::ControlRequest::SetSockOpt { .. } => {}
            psocket::ControlRequest::GetSockOpt { .. } => {}
            _ => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_async as fasync;

    use crate::eventloop::integration_tests::{
        test_ep_name, StackSetupBuilder, TestSetup, TestSetupBuilder, TestStack,
    };
    use crate::eventloop::socket::{
        testutil::{SockAddrTestOptions, TestSockAddr},
        SockAddr4, SockAddr6,
    };
    use net_types::ip::{Ip, IpAddress};

    async fn udp_prepare_test<A: TestSockAddr>() -> (TestSetup, psocket::ControlProxy) {
        let mut t = TestSetupBuilder::new()
            .add_endpoint()
            .add_stack(
                StackSetupBuilder::new()
                    .add_named_endpoint(test_ep_name(1), Some(A::config_addr_subnet())),
            )
            .build()
            .await
            .unwrap();
        let proxy = get_socket::<A>(t.get(0)).await;
        (t, proxy)
    }

    async fn get_socket<A: TestSockAddr>(test_stack: &mut TestStack) -> psocket::ControlProxy {
        let socket_provider = test_stack.connect_socket_provider().unwrap();
        let socket_response = test_stack
            .run_future(socket_provider.socket(A::FAMILY as i16, libc::SOCK_DGRAM as i16, 0))
            .await
            .expect("Socket call succeeds");
        assert_eq!(socket_response.0, 0);
        socket_response.1.expect("Socket returns a channel").into_proxy().unwrap()
    }

    async fn test_udp_connect_failure<A: TestSockAddr>() {
        let (mut t, proxy) = udp_prepare_test::<A>().await;
        let stack = t.get(0);
        // pass bad SockAddr struct (too small to be parsed):
        let addr = vec![1_u8, 2, 3, 4];
        let res = stack.run_future(proxy.connect(&mut addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, libc::EFAULT);

        // pass a bad family:
        let addr = A::create_for_test(SockAddrTestOptions {
            port: 1010,
            bad_family: true,
            bad_address: false,
        });
        let res = stack.run_future(proxy.connect(&mut addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, libc::EAFNOSUPPORT);

        // pass an unspecified remote address:
        let addr = A::create_for_test(SockAddrTestOptions {
            port: 1010,
            bad_family: false,
            bad_address: true,
        });
        let res = stack.run_future(proxy.connect(&mut addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, libc::EINVAL);

        // pass a bad port:
        let addr = A::create_for_test(SockAddrTestOptions {
            port: 0,
            bad_family: false,
            bad_address: false,
        });
        let res = stack.run_future(proxy.connect(&mut addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, libc::ECONNREFUSED);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_connect_failure_v4() {
        test_udp_connect_failure::<SockAddr4>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_connect_failure_v6() {
        test_udp_connect_failure::<SockAddr6>().await;
    }

    async fn test_udp_connect<A: TestSockAddr>() {
        let (mut t, proxy) = udp_prepare_test::<A>().await;
        let stack = t.get(0);
        let remote = A::create(A::REMOTE_ADDR, 200);
        let res = stack.run_future(proxy.connect(&mut remote.into_iter())).await.unwrap() as i32;
        assert_eq!(res, 0);

        // can connect again to a different remote should succeed.
        let remote = A::create(A::REMOTE_ADDR_2, 200);
        let res = stack.run_future(proxy.connect(&mut remote.into_iter())).await.unwrap() as i32;
        assert_eq!(res, 0);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_connect_v4() {
        test_udp_connect::<SockAddr4>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_connect_v6() {
        test_udp_connect::<SockAddr6>().await;
    }

    async fn test_udp_bind<A: TestSockAddr>() {
        let (mut t, socket) = udp_prepare_test::<A>().await;
        let stack = t.get(0);
        // can bind to local address
        let addr = A::create(A::LOCAL_ADDR, 200);
        let res = stack.run_future(socket.bind(&mut addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, 0);

        // can't bind again (to another port)
        let addr = A::create(A::LOCAL_ADDR, 201);
        let res = stack.run_future(socket.bind(&mut addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, libc::EALREADY);

        // can bind another socket to a different port:
        let socket = get_socket::<A>(stack).await;
        let addr = A::create(A::LOCAL_ADDR, 201);
        let res = stack.run_future(socket.bind(&mut addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, 0);

        // can bind to unspecified address in a different port:
        let socket = get_socket::<A>(stack).await;
        let addr = A::create(<A::AddrType as IpAddress>::Version::UNSPECIFIED_ADDRESS, 202);
        let res = stack.run_future(socket.bind(&mut addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, 0);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_bind_v4() {
        test_udp_bind::<SockAddr4>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_bind_v6() {
        test_udp_bind::<SockAddr6>().await;
    }

    async fn test_udp_bind_then_connect<A: TestSockAddr>() {
        let (mut t, socket) = udp_prepare_test::<A>().await;
        let stack = t.get(0);
        // can bind to local address
        let bind_addr = A::create(A::LOCAL_ADDR, 200);
        let res = stack.run_future(socket.bind(&mut bind_addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, 0);
        let remote_addr = A::create(A::REMOTE_ADDR, 1010);
        let res =
            stack.run_future(socket.connect(&mut remote_addr.into_iter())).await.unwrap() as i32;
        assert_eq!(res, 0);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_bind_then_connect_v4() {
        test_udp_bind_then_connect::<SockAddr4>().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_udp_bind_then_connect_v6() {
        test_udp_bind_then_connect::<SockAddr6>().await;
    }
}
