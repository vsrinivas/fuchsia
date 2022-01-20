// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::ProtocolMarker as _;
use fixture::fixture;
use fuchsia_zircon as zx;
use futures::{Future, FutureExt as _, StreamExt as _, TryStreamExt as _};
use net_types::Witness as _;
use netemul::{Endpoint as _, RealmUdpSocket as _};
use netstack_testing_common::realms::{Netstack2, TestSandboxExt as _};
use netsvc_proto::{debuglog, netboot, tftp};
use packet::{FragmentedBuffer as _, InnerPacketBuilder as _, ParseBuffer as _, Serializer};
use std::borrow::Cow;
use std::convert::{TryFrom as _, TryInto as _};
use zerocopy::{FromBytes, LayoutVerified, NativeEndian, Unaligned, U32};

const NETSVC_URL: &str = "#meta/netsvc.cm";
const NETSVC_NAME: &str = "netsvc";

const NAME_PROVIDER_URL: &str = "#meta/device-name-provider.cm";
const NAME_PROVIDER_NAME: &str = "device-name-provider";

const MOCK_SERVICES_NAME: &str = "mock";

const DEV_ETHERNET_DIRECTORY: &str = "dev-class-ethernet";

const BUFFER_SIZE: usize = 2048;

const MOCK_BOARD_NAME: &str = "mock-board";
const MOCK_BOOTLOADER_VENDOR: &str = "mock-bootloader-vendor";
const MOCK_BOARD_REVISION: u32 = 0xDEADBEEF;

fn create_netsvc_realm<'a>(
    sandbox: &'a netemul::TestSandbox,
    name: impl Into<Cow<'a, str>>,
) -> (netemul::TestRealm<'a>, impl Future<Output = ()> + Unpin) {
    use fuchsia_component::server::{ServiceFs, ServiceFsDir};

    let (mock_dir, server_end) = fidl::endpoints::create_endpoints().expect("create endpoints");

    enum Services {
        ReadOnlyLog(fidl_fuchsia_boot::ReadOnlyLogRequestStream),
        SysInfo(fidl_fuchsia_sysinfo::SysInfoRequestStream),
    }

    let mut fs = ServiceFs::new();
    let _: &mut ServiceFsDir<'_, _> =
        fs.dir("svc").add_fidl_service(Services::ReadOnlyLog).add_fidl_service(Services::SysInfo);
    let _: &mut ServiceFs<_> =
        fs.serve_connection(server_end.into_channel()).expect("serve connection");

    let fs = fs.for_each_concurrent(None, |r| async move {
        match r {
            Services::ReadOnlyLog(rs) => {
                let () = rs
                    .map_ok(|fidl_fuchsia_boot::ReadOnlyLogRequest::Get { responder }| {
                        // TODO(https://fxbug.dev/91150): Move netsvc to use LogListener
                        // instead. We're temporarily using a loophole here that
                        // zx_debuglog_create accepts an invalid root resource handle,
                        // but that might not be true forever.
                        let debuglog = zx::DebugLog::create(
                            &zx::Handle::invalid().into(),
                            zx::DebugLogOpts::READABLE,
                        )
                        .expect("failed to create debuglog handle");
                        let () = responder.send(debuglog).expect("failed to respond");
                    })
                    .try_collect()
                    .await
                    .expect("handling request stream");
            }
            Services::SysInfo(rs) => {
                let () = rs
                    .map_ok(|req| {
                        match req {
                        fidl_fuchsia_sysinfo::SysInfoRequest::GetBoardName { responder } => {
                            responder.send(zx::Status::OK.into_raw(), Some(MOCK_BOARD_NAME))
                        }
                        fidl_fuchsia_sysinfo::SysInfoRequest::GetBoardRevision { responder } => {
                            responder.send(zx::Status::OK.into_raw(), MOCK_BOARD_REVISION)
                        }
                        fidl_fuchsia_sysinfo::SysInfoRequest::GetBootloaderVendor { responder } => {
                            responder.send(zx::Status::OK.into_raw(), Some(MOCK_BOOTLOADER_VENDOR))
                        }
                        r @ fidl_fuchsia_sysinfo::SysInfoRequest::GetInterruptControllerInfo {
                            ..
                        } => panic!("unsupported request {:?}", r),
                    }
                    .expect("failed to send response")
                    })
                    .try_collect()
                    .await
                    .expect("handling request stream");
            }
        }
    });

    let realm = sandbox
        .create_realm(
            name,
            [
                fidl_fuchsia_netemul::ChildDef {
                    source: Some(fidl_fuchsia_netemul::ChildSource::Component(
                        NETSVC_URL.to_string(),
                    )),
                    name: Some(NETSVC_NAME.to_string()),
                    program_args: Some(vec!["--netboot".to_string(), "--all-features".to_string()]),
                    uses: Some(fidl_fuchsia_netemul::ChildUses::Capabilities(vec![
                        fidl_fuchsia_netemul::Capability::NetemulDevfs(
                            fidl_fuchsia_netemul::DevfsDep {
                                name: Some(DEV_ETHERNET_DIRECTORY.to_string()),
                                subdir: Some(netemul::Ethernet::DEV_PATH.to_string()),
                                ..fidl_fuchsia_netemul::DevfsDep::EMPTY
                            },
                        ),
                        fidl_fuchsia_netemul::Capability::ChildDep(
                            fidl_fuchsia_netemul::ChildDep {
                                name: Some(NAME_PROVIDER_NAME.to_string()),
                                capability: Some(
                                    fidl_fuchsia_netemul::ExposedCapability::Protocol(
                                        fidl_fuchsia_device::NameProviderMarker::NAME.to_string(),
                                    ),
                                ),
                                ..fidl_fuchsia_netemul::ChildDep::EMPTY
                            },
                        ),
                        fidl_fuchsia_netemul::Capability::ChildDep(
                            fidl_fuchsia_netemul::ChildDep {
                                name: Some(MOCK_SERVICES_NAME.to_string()),
                                capability: Some(
                                    fidl_fuchsia_netemul::ExposedCapability::Protocol(
                                        fidl_fuchsia_boot::ReadOnlyLogMarker::NAME.to_string(),
                                    ),
                                ),
                                ..fidl_fuchsia_netemul::ChildDep::EMPTY
                            },
                        ),
                        fidl_fuchsia_netemul::Capability::ChildDep(
                            fidl_fuchsia_netemul::ChildDep {
                                name: Some(MOCK_SERVICES_NAME.to_string()),
                                capability: Some(
                                    fidl_fuchsia_netemul::ExposedCapability::Protocol(
                                        fidl_fuchsia_sysinfo::SysInfoMarker::NAME.to_string(),
                                    ),
                                ),
                                ..fidl_fuchsia_netemul::ChildDep::EMPTY
                            },
                        ),
                        fidl_fuchsia_netemul::Capability::LogSink(fidl_fuchsia_netemul::Empty {}),
                    ])),
                    eager: Some(true),
                    ..fidl_fuchsia_netemul::ChildDef::EMPTY
                },
                fidl_fuchsia_netemul::ChildDef {
                    source: Some(fidl_fuchsia_netemul::ChildSource::Component(
                        NAME_PROVIDER_URL.to_string(),
                    )),
                    name: Some(NAME_PROVIDER_NAME.to_string()),
                    uses: Some(fidl_fuchsia_netemul::ChildUses::Capabilities(vec![
                        fidl_fuchsia_netemul::Capability::NetemulDevfs(
                            fidl_fuchsia_netemul::DevfsDep {
                                name: Some(DEV_ETHERNET_DIRECTORY.to_string()),
                                subdir: Some(netemul::Ethernet::DEV_PATH.to_string()),
                                ..fidl_fuchsia_netemul::DevfsDep::EMPTY
                            },
                        ),
                        fidl_fuchsia_netemul::Capability::LogSink(fidl_fuchsia_netemul::Empty {}),
                    ])),
                    exposes: Some(vec![fidl_fuchsia_device::NameProviderMarker::NAME.to_string()]),
                    ..fidl_fuchsia_netemul::ChildDef::EMPTY
                },
                fidl_fuchsia_netemul::ChildDef {
                    source: Some(fidl_fuchsia_netemul::ChildSource::Mock(mock_dir)),
                    name: Some(MOCK_SERVICES_NAME.to_string()),
                    ..fidl_fuchsia_netemul::ChildDef::EMPTY
                },
            ],
        )
        .expect("create realm");

    (realm, fs)
}

async fn with_netsvc_and_netstack_bind_port<F, Fut>(port: u16, name: &str, test: F)
where
    F: FnOnce(fuchsia_async::net::UdpSocket, u32) -> Fut,
    Fut: futures::Future<Output = ()>,
{
    type E = netemul::Ethernet;
    let netsvc_name = format!("{}-netsvc", name);
    let ns_name = format!("{}-netstack", name);
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let (netsvc_realm, services) = create_netsvc_realm(&sandbox, &netsvc_name);
    let network = sandbox.create_network("net").await.expect("create network");
    let ep = network
        .create_endpoint::<netemul::Ethernet, _>(&netsvc_name)
        .await
        .expect("create endpoint");
    let () = ep.set_link_up(true).await.expect("set link up");

    let () = netsvc_realm
        .add_virtual_device(&ep, E::dev_path("ep").as_path())
        .await
        .expect("add virtual device");

    let netstack_realm =
        sandbox.create_netstack_realm::<Netstack2, _>(&ns_name).expect("create netstack realm");

    let interface: netemul::TestInterface<'_> = netstack_realm
        .join_network::<E, _>(&network, &ns_name, &netemul::InterfaceConfig::None)
        .await
        .expect("join network");

    let _: net_types::ip::Ipv6Addr = netstack_testing_common::interfaces::wait_for_v6_ll(
        &netstack_realm
            .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
            .expect("connect to protocol"),
        interface.id(),
    )
    .await
    .expect("wait ll address");

    let sock = fuchsia_async::net::UdpSocket::bind_in_realm(
        &netstack_realm,
        std::net::SocketAddrV6::new(
            std::net::Ipv6Addr::UNSPECIFIED,
            port,
            /* flowinfo */ 0,
            /* scope id */ 0,
        )
        .into(),
    )
    .await
    .expect("bind in realm");

    let test_fut = test(sock, interface.id().try_into().expect("interface ID doesn't fit u32"));
    futures::select! {
        () = services.fuse() => panic!("ServiceFs ended unexpectedly"),
        () =  test_fut.fuse() => (),
    }
}

async fn with_netsvc_and_netstack<F, Fut>(name: &str, test: F)
where
    F: FnOnce(fuchsia_async::net::UdpSocket, u32) -> Fut,
    Fut: futures::Future<Output = ()>,
{
    with_netsvc_and_netstack_bind_port(/* unspecified port */ 0, name, test).await
}

async fn with_netsvc_and_netstack_debuglog_port<F, Fut>(name: &str, test: F)
where
    F: FnOnce(fuchsia_async::net::UdpSocket, u32) -> Fut,
    Fut: futures::Future<Output = ()>,
{
    with_netsvc_and_netstack_bind_port(debuglog::MULTICAST_PORT.get(), name, test).await
}

async fn discover(sock: &fuchsia_async::net::UdpSocket, scope_id: u32) -> std::net::Ipv6Addr {
    const ARG: u32 = 0;
    let mut cookie = 1234;

    // NB: We can't guarantee there isn't a race between netsvc starting and all
    // the socket set up. The safe way is to send queries periodically in case
    // netsvc misses our first query to prevent flakes.
    let mut send_interval = futures::stream::once(futures::future::ready(()))
        .chain(fuchsia_async::Interval::new(zx::Duration::from_seconds(1)));

    let mut buf = [0; BUFFER_SIZE];

    loop {
        enum Action<'a> {
            Poll,
            Data(&'a [u8], std::net::SocketAddr),
        }

        let action = futures::select! {
            n = send_interval.next() => {
                let () = n.expect("interval stream ended unexpectedly");
                Action::Poll
            }
            r = sock.recv_from(&mut buf[..]).fuse() => {
                let (n, addr) = r.expect("recv_from failed");
                Action::Data(&buf[..n], addr)
            }
        };

        match action {
            Action::Poll => {
                cookie += 1;
                // Build a query for all nodes ("*" + null termination).
                let query = ("*\0".as_bytes())
                    .into_serializer()
                    .serialize_vec(netboot::NetbootPacketBuilder::new(
                        netboot::OpcodeOrErr::Op(netboot::Opcode::Query),
                        cookie,
                        ARG,
                    ))
                    .expect("serialize query")
                    .unwrap_b();

                let sent = sock
                    .send_to(
                        query.as_ref(),
                        std::net::SocketAddrV6::new(
                            net_types::ip::Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS
                                .into_addr()
                                .into(),
                            netboot::SERVER_PORT.get(),
                            /* flowinfo */ 0,
                            scope_id,
                        )
                        .into(),
                    )
                    .await
                    .expect("sendto");
                assert_eq!(sent, query.len());
            }
            Action::Data(mut buf, addr) => {
                let pkt = buf.parse::<netboot::NetbootPacket<_>>().expect("failed to parse");
                assert_eq!(pkt.command(), netboot::OpcodeOrErr::Op(netboot::Opcode::Ack));
                if pkt.cookie() != cookie {
                    println!("ignoring {:?} with old cookie", pkt);
                    continue;
                }
                assert_eq!(pkt.arg(), ARG);
                let nodename =
                    std::str::from_utf8(pkt.payload()).expect("failed to parse advertisement");
                assert!(nodename.starts_with("fuchsia-"), "invalid nodename {}", nodename);

                let ip = match addr {
                    std::net::SocketAddr::V4(v4) => panic!("unexpected v4 sender: {}", v4),
                    std::net::SocketAddr::V6(addr) => {
                        assert_eq!(addr.port(), netboot::SERVER_PORT.get());

                        let ip = addr.ip();
                        // Should be link local address with non zero scope ID.
                        assert!(
                            net_types::ip::Ipv6Addr::from_bytes(ip.octets())
                                .is_unicast_link_local(),
                            "bad address {}",
                            ip
                        );
                        assert_ne!(addr.scope_id(), 0);

                        ip.clone()
                    }
                };

                break ip;
            }
        }
    }
}

async fn send_message<S>(ser: S, sock: &fuchsia_async::net::UdpSocket, to: std::net::SocketAddr)
where
    S: Serializer + std::fmt::Debug,
    S::Buffer: packet::ReusableBuffer + std::fmt::Debug + AsRef<[u8]>,
{
    let b = ser
        .serialize_outer(|length| {
            assert!(length <= BUFFER_SIZE, "{} > {}", length, BUFFER_SIZE);
            Result::<_, std::convert::Infallible>::Ok(packet::Buf::new([0u8; BUFFER_SIZE], ..))
        })
        .expect("failed to serialize");
    let sent = sock.send_to(b.as_ref(), to).await.expect("send to failed");
    assert_eq!(sent, b.len());
}

async fn read_message<'a, P, B>(
    buffer: &'a mut B,
    sock: &fuchsia_async::net::UdpSocket,
    expect_src: std::net::SocketAddr,
) -> P
where
    P: packet::ParsablePacket<&'a [u8], ()>,
    P::Error: std::fmt::Debug,
    B: packet::ParseBufferMut,
{
    let (n, addr) = sock.recv_from(buffer.as_mut()).await.expect("recv from failed");
    assert_eq!(addr, expect_src);
    let () = buffer.shrink_back_to(n);
    buffer.parse::<P>().expect("parse failed")
}

#[fixture(with_netsvc_and_netstack)]
#[fuchsia_async::run_singlethreaded(test)]
async fn can_discover(sock: fuchsia_async::net::UdpSocket, scope_id: u32) {
    let _: std::net::Ipv6Addr = discover(&sock, scope_id).await;
}

#[fixture(with_netsvc_and_netstack_debuglog_port)]
#[fuchsia_async::run_singlethreaded(test)]
async fn debuglog(sock: fuchsia_async::net::UdpSocket, _scope_id: u32) {
    #[derive(Clone)]
    enum Ack {
        Yes,
        No,
    }
    // Test that we observe and acknowledge multiple log messages. Then assert
    // that an unacknowledged message gets resent.
    // The delay for retransmission is low on the first retransmission, which
    // should not make this test unnecessarily long, but we keep it to one
    // observation of that event.
    let _: (fuchsia_async::net::UdpSocket, Option<u32>) =
        futures::stream::iter(std::iter::repeat(Ack::Yes).take(10).chain(std::iter::once(Ack::No)))
            .fold((sock, None), |(sock, seqno), ack| async move {
                let mut buf = [0; BUFFER_SIZE];
                let (n, addr) = sock.recv_from(&mut buf[..]).await.expect("recv_from failed");
                let mut bv = &buf[..n];
                let pkt = bv.parse::<debuglog::DebugLogPacket<_>>().expect("parse failed");

                match ack {
                    Ack::Yes => {
                        let () = send_message(
                            debuglog::AckPacketBuilder::new(pkt.seqno()).into_serializer(),
                            &sock,
                            addr,
                        )
                        .await;
                    }
                    Ack::No => (),
                }

                let seqno = match seqno {
                    None => pkt.seqno(),
                    Some(s) => {
                        if pkt.seqno() <= s {
                            // Don't verify repeat or old packets.
                            return (sock, Some(s));
                        }
                        let nxt = s + 1;
                        assert_eq!(pkt.seqno(), nxt);
                        nxt
                    }
                };

                let nodename = pkt.nodename();
                assert!(nodename.starts_with("fuchsia-"), "bad nodename {}", nodename);
                // TODO(https://fxbug.dev/91150): We can assert on message contents
                // here when we move netsvc to use LogListener.
                let msg: &str = pkt.data();

                // Wait for a repeat of the packet if we didn't ack.
                match ack {
                    Ack::No => {
                        // NB: we need to read into a new buffer because we use
                        // variables stored in the old one for comparison.
                        let mut buf = [0; BUFFER_SIZE];
                        let (n, next_addr) =
                            sock.recv_from(&mut buf[..]).await.expect("recv_from failed");
                        let mut bv = &buf[..n];
                        let pkt = bv.parse::<debuglog::DebugLogPacket<_>>().expect("parse failed");
                        assert_eq!(next_addr, addr);
                        assert_eq!(pkt.seqno(), seqno);
                        assert_eq!(pkt.nodename(), nodename);
                        assert_eq!(pkt.data(), msg);
                    }
                    Ack::Yes => (),
                }

                (sock, Some(seqno))
            })
            .await;
}

#[fixture(with_netsvc_and_netstack)]
#[fuchsia_async::run_singlethreaded(test)]
async fn get_board_info(sock: fuchsia_async::net::UdpSocket, scope_id: u32) {
    const BOARD_NAME_FILE: &str = "<<image>>board_info";
    let device = discover(&sock, scope_id).await;
    let socket_addr = std::net::SocketAddrV6::new(
        device,
        tftp::INCOMING_PORT.get(),
        /* flowinfo */ 0,
        scope_id,
    )
    .into();

    // Request a very large timeout to make sure we don't get flakes.
    const TIMEOUT_OPTION_SECS: u8 = std::u8::MAX;

    #[repr(C)]
    #[derive(FromBytes, Unaligned)]
    // Defined in zircon/system/public/zircon/boot/netboot.h.
    struct BoardInfo {
        board_name: [u8; 32],
        board_revision: U32<NativeEndian>,
        mac_address: [u8; 6],
        _padding: [u8; 2],
    }

    let () = send_message(
        tftp::TransferRequestBuilder::new_with_options(
            tftp::TransferDirection::Read,
            BOARD_NAME_FILE,
            tftp::TftpMode::OCTET,
            [
                tftp::TftpOption::TransferSize(std::u64::MAX).not_forced(),
                tftp::TftpOption::Timeout(TIMEOUT_OPTION_SECS).not_forced(),
            ],
        )
        .into_serializer(),
        &sock,
        socket_addr,
    )
    .await;

    // After the first message, everything must happen on a different port.
    let socket_addr = std::net::SocketAddrV6::new(
        device,
        tftp::OUTGOING_PORT.get(),
        /* flowinfo */ 0,
        scope_id,
    )
    .into();

    let mut buffer = [0u8; BUFFER_SIZE];
    {
        let mut pb = &mut buffer[..];
        let oack = read_message::<tftp::TftpPacket<_>, _>(&mut pb, &sock, socket_addr)
            .await
            .into_oack()
            .expect("unexpected response");

        assert_eq!(
            oack.options().collect(),
            tftp::AllOptions {
                window_size: None,
                block_size: None,
                timeout: Some(tftp::Forceable { value: TIMEOUT_OPTION_SECS, forced: false }),
                transfer_size: Some(tftp::Forceable {
                    value: u64::try_from(std::mem::size_of::<BoardInfo>())
                        .expect("doesn't fit u64"),
                    forced: false
                })
            }
        );
    }

    // Acknowledge options by sending an ack.
    let () = send_message(
        tftp::AckPacketBuilder::new(/* block */ 0).into_serializer(),
        &sock,
        socket_addr,
    )
    .await;

    {
        let mut pb = &mut buffer[..];
        let data = read_message::<tftp::TftpPacket<_>, _>(&mut pb, &sock, socket_addr)
            .await
            .into_data()
            .expect("unexpected message");
        assert_eq!(data.block(), 1);
        assert_eq!(data.payload().len(), std::mem::size_of::<BoardInfo>());
        let board_info = LayoutVerified::<_, BoardInfo>::new(data.payload().as_ref())
            .expect("failed to get board info");
        let BoardInfo { board_name, board_revision, mac_address, _padding } = &*board_info;
        // mac_address is not filled by netsvc.
        assert_eq!(mac_address, [0u8; 6].as_ref());
        assert_eq!(board_revision.get(), MOCK_BOARD_REVISION);
        let board_name =
            board_name.split(|b| *b == 0).next().expect("failed to find null termination");
        let board_name = std::str::from_utf8(board_name).expect("failed to parse board name");

        let expected_board_name = if cfg!(target_arch = "x86_64") {
            // netsvc overrides the board name on x64 boards 🤷.
            "x64"
        } else {
            MOCK_BOARD_NAME
        };
        assert_eq!(board_name, expected_board_name);
    }
}
