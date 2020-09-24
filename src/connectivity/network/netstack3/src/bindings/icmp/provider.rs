// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! As the only discoverable service of fuchsia.net.icmp, the Provider service facilitates the
//! creation of ICMP sockets.

use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{channel::mpsc, TryFutureExt, TryStreamExt};
use log::{debug, error, trace};
use rand::RngCore;

use net_types::ip::{IpAddr, Ipv4, Ipv6};
use net_types::SpecifiedAddr;

use fidl_fuchsia_net_icmp::{
    EchoSocketConfig, EchoSocketRequestStream, ProviderRequest, ProviderRequestStream,
};

use netstack3_core::EventDispatcher;

use super::{
    echo::EchoSocketWorker, EchoSocket, IcmpEchoSockets, IcmpStackContext, IpExt, RX_BUFFER_SIZE,
};

use crate::bindings::{context::InnerValue, util::TryIntoCore, LockedStackContext, StackContext};

pub(crate) struct IcmpProviderWorker<C: StackContext> {
    ctx: C,
}

impl<C> IcmpProviderWorker<C>
where
    C: IcmpStackContext,
    C::Dispatcher: InnerValue<IcmpEchoSockets>,
{
    fn new(ctx: C) -> Self {
        Self { ctx }
    }

    pub(crate) fn spawn(ctx: C, mut rs: ProviderRequestStream) {
        fasync::Task::spawn(
            async move {
                let worker = Self::new(ctx);
                while let Some(req) = rs.try_next().await? {
                    worker.handle_request(req).await?;
                }
                Ok(())
            }
            .unwrap_or_else(|e: fidl::Error| {
                debug!("IcmpProviderWorker finished with error {:?}", e)
            }),
        )
        .detach();
    }

    /// Handle a [`fidl_fuchsia_net_icmp::ProviderRequest`], which is used for opening ICMP sockets.
    async fn handle_request(&self, req: ProviderRequest) -> Result<(), fidl::Error> {
        match req {
            ProviderRequest::OpenEchoSocket { config, socket, control_handle: _ } => {
                let (stream, handle) = socket.into_stream_and_control_handle()?;
                handle.send_on_open_(
                    self.open_echo_socket(config, stream)
                        .await
                        .err()
                        .unwrap_or(zx::Status::OK)
                        .into_raw(),
                )
            }
        }
    }

    async fn open_echo_socket(
        &self,
        config: EchoSocketConfig,
        stream: EchoSocketRequestStream,
    ) -> Result<(), zx::Status> {
        trace!("Opening ICMP Echo socket: {:?}", config);

        let remote: SpecifiedAddr<IpAddr> = config
            .remote
            .ok_or(zx::Status::INVALID_ARGS)?
            .try_into_core()
            .map_err(|_| zx::Status::INVALID_ARGS)?;

        let local: Option<SpecifiedAddr<IpAddr>> = match config.local {
            Some(l) => Some(l.try_into_core().map_err(|_| zx::Status::INVALID_ARGS)?),
            None => None,
        };

        use net_types::ip::IpAddr::{V4, V6};

        let ctx = self.ctx.lock().await;
        match local {
            Some(local) => match (local.into(), remote.into()) {
                (V4(local), V4(remote)) => {
                    self.connect_echo_socket::<Ipv4>(ctx, stream, Some(local), remote)
                }
                (V6(local), V6(remote)) => {
                    self.connect_echo_socket::<Ipv6>(ctx, stream, Some(local), remote)
                }
                _ => Err(zx::Status::INVALID_ARGS),
            },
            None => match remote.into() {
                V4(remote) => self.connect_echo_socket::<Ipv4>(ctx, stream, None, remote),
                V6(remote) => self.connect_echo_socket::<Ipv6>(ctx, stream, None, remote),
            },
        }
    }

    fn connect_echo_socket<I: IpExt>(
        &self,
        mut ctx: LockedStackContext<'_, C>,
        stream: EchoSocketRequestStream,
        local: Option<SpecifiedAddr<I::Addr>>,
        remote: SpecifiedAddr<I::Addr>,
    ) -> Result<(), zx::Status> {
        // TODO(fxbug.dev/36212): Generate icmp_ids without relying on an RNG. This line
        // of code does not handle conflicts very well, requiring the client to
        // continuously create sockets until it succeeds.
        let icmp_id = ctx.dispatcher_mut().rng_mut().next_u32() as u16;
        self.connect_echo_socket_inner::<I>(ctx, stream, local, remote, icmp_id)
    }

    fn connect_echo_socket_inner<I: IpExt>(
        &self,
        mut ctx: LockedStackContext<'_, C>,
        stream: EchoSocketRequestStream,
        local: Option<SpecifiedAddr<I::Addr>>,
        remote: SpecifiedAddr<I::Addr>,
        icmp_id: u16,
    ) -> Result<(), zx::Status> {
        match I::new_icmp_connection::<C>(&mut ctx, local, remote, icmp_id) {
            Ok(conn) => {
                let (reply_tx, reply_rx) = mpsc::channel(RX_BUFFER_SIZE);

                EchoSocketWorker::new(self.ctx.clone(), reply_rx, conn.into(), icmp_id)
                    .spawn(stream);

                trace!("Spawned ICMP Echo socket worker");

                let socket = EchoSocket { reply_tx };
                I::get_icmp_echo_sockets(ctx.dispatcher_mut()).insert(conn, socket);
                Ok(())
            }
            Err(e) => {
                error!("Cannot create ICMP connection: {:?}", e);
                Err(zx::Status::ALREADY_EXISTS)
            }
        }
    }
}

#[cfg(test)]
mod test {
    use fuchsia_zircon as zx;
    use futures::stream::StreamExt;
    use log::debug;

    use fidl_fuchsia_net as fidl_net;
    use fidl_fuchsia_net_icmp::{
        EchoPacket, EchoSocketConfig, EchoSocketEvent, EchoSocketMarker, EchoSocketProxy,
    };

    use net_types::ip::{AddrSubnetEither, Ipv4, Ipv4Addr};
    use net_types::{SpecifiedAddr, Witness};

    use crate::bindings::icmp::IcmpProviderWorker;
    use crate::bindings::integration_tests::{
        new_ipv4_addr_subnet, new_ipv6_addr_subnet, StackSetupBuilder, TestSetup, TestSetupBuilder,
    };

    /// `TestAddr` abstracts extraction of IP addresses (or lack thereof) for testing. This eases
    /// the process of testing different permutations of IP versions.
    trait TestAddr {
        fn local_subnet() -> Option<AddrSubnetEither>;
        fn remote_subnet() -> Option<AddrSubnetEither>;

        fn local_fidl() -> Option<fidl_net::IpAddress>;
        fn remote_fidl() -> Option<fidl_net::IpAddress>;
    }

    struct TestIpv4Addr;
    impl TestAddr for TestIpv4Addr {
        fn local_subnet() -> Option<AddrSubnetEither> {
            Some(new_ipv4_addr_subnet([192, 168, 0, 1], 24))
        }
        fn remote_subnet() -> Option<AddrSubnetEither> {
            Some(new_ipv4_addr_subnet([192, 168, 0, 2], 24))
        }

        fn local_fidl() -> Option<fidl_net::IpAddress> {
            Some(fidl_net::IpAddress::Ipv4(fidl_net::Ipv4Address { addr: [192, 168, 0, 1] }))
        }
        fn remote_fidl() -> Option<fidl_net::IpAddress> {
            Some(fidl_net::IpAddress::Ipv4(fidl_net::Ipv4Address { addr: [192, 168, 0, 2] }))
        }
    }

    struct TestIpv6Addr;
    impl TestAddr for TestIpv6Addr {
        fn local_subnet() -> Option<AddrSubnetEither> {
            Some(new_ipv6_addr_subnet([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2], 64))
        }
        fn remote_subnet() -> Option<AddrSubnetEither> {
            Some(new_ipv6_addr_subnet([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3], 64))
        }

        fn local_fidl() -> Option<fidl_net::IpAddress> {
            Some(fidl_net::IpAddress::Ipv6(fidl_net::Ipv6Address {
                addr: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2],
            }))
        }
        fn remote_fidl() -> Option<fidl_net::IpAddress> {
            Some(fidl_net::IpAddress::Ipv6(fidl_net::Ipv6Address {
                addr: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3],
            }))
        }
    }

    struct TestNoIpv4Addr;
    impl TestAddr for TestNoIpv4Addr {
        fn local_subnet() -> Option<AddrSubnetEither> {
            Some(new_ipv4_addr_subnet([192, 168, 0, 1], 24))
        }
        fn remote_subnet() -> Option<AddrSubnetEither> {
            Some(new_ipv4_addr_subnet([192, 168, 0, 2], 24))
        }

        fn local_fidl() -> Option<fidl_net::IpAddress> {
            None
        }
        fn remote_fidl() -> Option<fidl_net::IpAddress> {
            None
        }
    }

    struct TestNoIpv6Addr;
    impl TestAddr for TestNoIpv6Addr {
        fn local_subnet() -> Option<AddrSubnetEither> {
            Some(new_ipv6_addr_subnet([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2], 64))
        }
        fn remote_subnet() -> Option<AddrSubnetEither> {
            Some(new_ipv6_addr_subnet([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3], 64))
        }

        fn local_fidl() -> Option<fidl_net::IpAddress> {
            None
        }
        fn remote_fidl() -> Option<fidl_net::IpAddress> {
            None
        }
    }

    const ALICE: usize = 0;
    const BOB: usize = 1;

    async fn open_icmp_echo_socket<Src: TestAddr, Dst: TestAddr>(
        expected_status: zx::Status,
    ) -> (TestSetup, EchoSocketProxy) {
        let mut t = TestSetupBuilder::new()
            .add_named_endpoint("alice")
            .add_named_endpoint("bob")
            .add_stack(StackSetupBuilder::new().add_named_endpoint("alice", Src::local_subnet()))
            .add_stack(StackSetupBuilder::new().add_named_endpoint("bob", Dst::remote_subnet()))
            .build()
            .await
            .expect("Test Setup succeeds");

        // Wait for interfaces on both stacks to signal online correctly
        t.get(ALICE).wait_for_interface_online(1).await;
        t.get(BOB).wait_for_interface_online(1).await;

        let icmp_provider = t.get(ALICE).connect_icmp_provider().unwrap();
        let config = EchoSocketConfig { local: Src::local_fidl(), remote: Dst::remote_fidl() };

        let (socket_client, socket_server) =
            fidl::endpoints::create_endpoints::<EchoSocketMarker>().unwrap();
        let socket = socket_client.into_proxy().unwrap();
        let mut event_stream = socket.take_event_stream();

        icmp_provider.open_echo_socket(config, socket_server).expect("ICMP Echo socket opens");

        // Wait for the ICMP echo socket to open
        loop {
            match event_stream.next().await.unwrap().unwrap() {
                EchoSocketEvent::OnOpen_ { s } => {
                    let status = zx::Status::from_raw(s);
                    debug!("ICMP Echo socket opened with status: {}", status);
                    assert_eq!(status, expected_status);
                    break;
                }
            }
        }

        (t, socket)
    }

    async fn send_echoes(socket: EchoSocketProxy, payload: Vec<u8>) {
        for sequence_num in 1u16..=4 {
            debug!("Sending ping seq {} to socket {:?}", sequence_num, socket);
            let mut req = EchoPacket { sequence_num, payload: payload.to_owned() };
            socket.send_request(&mut req).unwrap();

            let packet = socket.watch().await.unwrap().unwrap();
            debug!("Received packet: {:?}", packet);
            assert_eq!(packet.sequence_num, sequence_num);
            assert_eq!(packet.payload, payload);
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_icmp_echo_socket_ipv4() {
        let (_t, socket) =
            open_icmp_echo_socket::<TestIpv4Addr, TestIpv4Addr>(zx::Status::OK).await;
        send_echoes(socket, vec![1, 2, 3, 4, 5, 6]).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_icmp_echo_socket_ipv6() {
        let (_t, socket) =
            open_icmp_echo_socket::<TestIpv6Addr, TestIpv6Addr>(zx::Status::OK).await;
        send_echoes(socket, vec![1, 2, 3, 4, 5, 6]).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_icmp_echo_socket_ipv4_no_local_ip() {
        let (_t, socket) =
            open_icmp_echo_socket::<TestNoIpv4Addr, TestIpv4Addr>(zx::Status::OK).await;
        send_echoes(socket, vec![1, 2, 3, 4, 5, 6]).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_icmp_echo_socket_ipv6_no_local_ip() {
        let (_t, socket) =
            open_icmp_echo_socket::<TestNoIpv6Addr, TestIpv6Addr>(zx::Status::OK).await;
        send_echoes(socket, vec![1, 2, 3, 4, 5, 6]).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_icmp_echo_socket_ipv4_no_remote_ip() {
        open_icmp_echo_socket::<TestIpv4Addr, TestNoIpv4Addr>(zx::Status::INVALID_ARGS).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_icmp_echo_socket_ipv6_no_remote_ip() {
        open_icmp_echo_socket::<TestIpv6Addr, TestNoIpv6Addr>(zx::Status::INVALID_ARGS).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_icmp_echo_socket_ipv4_ipv6_mismatch() {
        open_icmp_echo_socket::<TestIpv4Addr, TestIpv6Addr>(zx::Status::INVALID_ARGS).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_icmp_echo_socket_ipv6_ipv4_mismatch() {
        open_icmp_echo_socket::<TestIpv6Addr, TestIpv4Addr>(zx::Status::INVALID_ARGS).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_icmp_echo_socket_no_local_or_remote_ipv4() {
        open_icmp_echo_socket::<TestNoIpv4Addr, TestNoIpv4Addr>(zx::Status::INVALID_ARGS).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_icmp_echo_socket_no_local_or_remote_ipv6() {
        open_icmp_echo_socket::<TestNoIpv6Addr, TestNoIpv6Addr>(zx::Status::INVALID_ARGS).await;
    }

    // Relies on connect_echo_socket_inner, thus cannot use the `open_icmp_echo_socket` test helper.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_icmp_echo_socket_duplicate() {
        const ALICE: usize = 0;
        const BOB: usize = 1;
        const ALICE_IP: [u8; 4] = [192, 168, 0, 1];
        const BOB_IP: [u8; 4] = [192, 168, 0, 2];

        let mut t = TestSetupBuilder::new()
            .add_named_endpoint("alice")
            .add_named_endpoint("bob")
            .add_stack(
                StackSetupBuilder::new()
                    .add_named_endpoint("alice", Some(new_ipv4_addr_subnet(ALICE_IP, 24))),
            )
            .add_stack(
                StackSetupBuilder::new()
                    .add_named_endpoint("bob", Some(new_ipv4_addr_subnet(BOB_IP, 24))),
            )
            .build()
            .await
            .expect("Test Setup succeeds");

        // Wait for interfaces on both stacks to signal online correctly
        t.get(ALICE).wait_for_interface_online(1).await;
        t.get(BOB).wait_for_interface_online(1).await;

        // Open an ICMP echo socket from Alice to Bob
        t.get(ALICE).connect_icmp_provider().unwrap();

        let local = Some(SpecifiedAddr::new(Ipv4Addr::new(ALICE_IP)).unwrap());
        let remote = SpecifiedAddr::new(Ipv4Addr::new(BOB_IP)).unwrap();

        let (_, socket_server) = fidl::endpoints::create_endpoints::<EchoSocketMarker>().unwrap();
        let request_stream = socket_server.into_stream().unwrap();
        // create a SocketProviderWorker without actually serving a request
        // stream.
        let provider = IcmpProviderWorker::new(t.clone_ctx(ALICE));

        assert_eq!(
            provider.connect_echo_socket_inner::<Ipv4>(
                t.ctx(ALICE).await,
                request_stream,
                local,
                remote,
                1,
            ),
            Ok(())
        );

        // Open another ICMP echo socket from Alice to Bob with same connection identifier
        let (_, socket_server) = fidl::endpoints::create_endpoints::<EchoSocketMarker>().unwrap();
        let request_stream = socket_server.into_stream().unwrap();

        assert_eq!(
            provider.connect_echo_socket_inner::<Ipv4>(
                t.ctx(ALICE).await,
                request_stream,
                local,
                remote,
                1,
            ),
            Err(zx::Status::ALREADY_EXISTS)
        );
    }
}
