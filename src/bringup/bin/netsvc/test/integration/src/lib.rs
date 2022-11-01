// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::prelude::*;
use fuchsia_zircon::{self as zx, HandleBased as _};
use futures::{Future, FutureExt as _, StreamExt as _};
use itertools::Itertools as _;
use net_types::Witness as _;
use netemul::{Endpoint, RealmUdpSocket as _};
use netstack_testing_common::realms::{Netstack2, TestSandboxExt as _};
use netstack_testing_macros::variants_test;
use netsvc_proto::{debuglog, netboot, tftp};
use packet::{FragmentedBuffer as _, InnerPacketBuilder as _, ParseBuffer as _, Serializer};
use std::borrow::Cow;
use std::convert::{TryFrom as _, TryInto as _};
use test_case::test_case;
use zerocopy::{byteorder::native_endian::U32, FromBytes, LayoutVerified, Unaligned};

const NETSVC_URL: &str = "#meta/netsvc.cm";
const NETSVC_NAME: &str = "netsvc";

const NAME_PROVIDER_URL: &str = "#meta/device-name-provider.cm";
const NAME_PROVIDER_NAME: &str = "device-name-provider";

const MOCK_SERVICES_NAME: &str = "mock";

const DEV_ETHERNET_DIRECTORY: &str = "dev-class-ethernet";
const DEV_NETWORK_DIRECTORY: &str = "dev-class-network";

const BUFFER_SIZE: usize = 2048;

const MOCK_BOARD_NAME: &str = "mock-board";
const MOCK_BOOTLOADER_VENDOR: &str = "mock-bootloader-vendor";
const MOCK_BOARD_REVISION: u32 = 0xDEADBEEF;

// Use a number that is not an exact power of two to make sure we're not hitting
// only happy paths of full blocks.
const PAVE_IMAGE_LEN: u64 = (50 << 10) + 20;
const PAVE_IMAGE_LEN_USIZE: usize = PAVE_IMAGE_LEN as usize;

const LOG_MSG_PID: u64 = 1234;
const LOG_MSG_TID: u64 = 6789;
const LOG_MSG_TAG: &str = "tag";
const LOG_MSG_CONTENTS: &str = "hello world";

fn create_netsvc_realm<'a, N, T, V>(
    sandbox: &'a netemul::TestSandbox,
    name: N,
    args: V,
) -> (netemul::TestRealm<'a>, impl Future<Output = ()> + Unpin)
where
    N: Into<Cow<'a, str>>,
    T: Into<String>,
    V: IntoIterator<Item = T>,
{
    use fuchsia_component::server::{ServiceFs, ServiceFsDir};

    let (mock_dir, server_end) = fidl::endpoints::create_endpoints().expect("create endpoints");

    enum Services {
        Log(fidl_fuchsia_logger::LogRequestStream),
        SysInfo(fidl_fuchsia_sysinfo::SysInfoRequestStream),
        Paver(fidl_fuchsia_paver::PaverRequestStream),
    }

    let mut fs = ServiceFs::new();
    let _: &mut ServiceFsDir<'_, _> = fs
        .dir("svc")
        .add_fidl_service(Services::Log)
        .add_fidl_service(Services::SysInfo)
        .add_fidl_service(Services::Paver);
    let _: &mut ServiceFs<_> = fs.serve_connection(server_end).expect("serve connection");

    let fs = fs.for_each_concurrent(None, |r| async move {
        match r {
            Services::Log(rs) => {
                let () = rs
                    .for_each_concurrent(None, |req| async move {
                        let log_listener = match req.expect("request stream error") {
                            fidl_fuchsia_logger::LogRequest::ListenSafe {
                                log_listener,
                                control_handle: _,
                                options,
                            } => {
                                assert_eq!(options, None);
                                log_listener.into_proxy().expect("create proxy")
                            }
                            r @ fidl_fuchsia_logger::LogRequest::ListenSafeWithSelectors {
                                ..
                            }
                            | r @ fidl_fuchsia_logger::LogRequest::DumpLogsSafe { .. } => {
                                panic!("unsupported request {:?}", r)
                            }
                        };
                        // NB: Start iterator at 1 so it matches debuglog
                        // sequence numbers.
                        let messages_gen = (1..).map(|v| fidl_fuchsia_logger::LogMessage {
                            pid: LOG_MSG_PID,
                            tid: LOG_MSG_TID,
                            time: zx::Duration::from_seconds(v).into_nanos(),
                            severity: fidl_fuchsia_logger::LOG_LEVEL_DEFAULT.into_primitive().into(),
                            dropped_logs: 0,
                            tags: vec![LOG_MSG_TAG.to_string()],
                            msg: LOG_MSG_CONTENTS.to_string(),
                        });
                        let log_listener = &log_listener;
                        futures::stream::iter(messages_gen)
                            .for_each(|mut msg| async move {
                                let () = log_listener
                                    .log(&mut msg)
                                    .await
                                    .expect("failed to send log to listener");
                            })
                            .await;
                    })
                    .await;
            }
            Services::SysInfo(rs) => {
                let () = rs
                    .for_each(|req| {
                        futures::future::ready(
                            match req.expect("request stream error") {
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
                                .expect("failed to send response"),
                        )
                    })
                    .await;
            }
            Services::Paver(rs) => {
                let () = rs
                    // NB: Extracted into separate function because rustfmt was
                    // getting confused by deep indentation.
                    .for_each_concurrent(None, |r| {
                        process_paver_request(r.expect("paver request stream error"))
                    })
                    .await;
            }
        }
    });

    fn validate_image(payload: &[u8]) {
        let bytes = IntoIterator::into_iter(payload).tuples().enumerate().fold(
            0,
            |bytes, (index, (a, b, c, d))| {
                let value = u32::from_ne_bytes([*a, *b, *c, *d]);
                let index = u32::try_from(index).expect("index doesn't fit u32");
                assert_eq!(value, index);
                bytes + std::mem::size_of::<u32>()
            },
        );
        // Ensure we consumed all bytes. This fails if
        // the image length is not a multiple of
        // `size_of::<u32>()`.
        assert_eq!(bytes, PAVE_IMAGE_LEN_USIZE);
    }

    async fn process_paver_request(req: fidl_fuchsia_paver::PaverRequest) {
        match req {
            fidl_fuchsia_paver::PaverRequest::FindDataSink { data_sink, control_handle: _ } => {
                data_sink
                    .into_stream()
                    .expect("failed to get request stream")
                    .for_each(|r| async move {
                        match r.expect("data sink request error") {
                            fidl_fuchsia_paver::DataSinkRequest::WriteAsset {
                                responder,
                                asset,
                                configuration,
                                payload: fidl_fuchsia_mem::Buffer { vmo, size },
                            } => {
                                assert_eq!(asset, fidl_fuchsia_paver::Asset::Kernel);
                                assert_eq!(
                                    configuration,
                                    fidl_fuchsia_paver::Configuration::Recovery
                                );
                                assert_eq!(size, PAVE_IMAGE_LEN);
                                let mut payload = [0u8; PAVE_IMAGE_LEN_USIZE];
                                vmo.read(&mut payload[..], 0).expect("failed to read payload");
                                let () = validate_image(&payload[..]);
                                responder.send(zx::Status::OK.into_raw())
                            }
                            fidl_fuchsia_paver::DataSinkRequest::WriteVolumes {
                                payload,
                                responder,
                            } => {
                                let () = process_streamed_payload(
                                    payload.into_proxy().expect("failed to get proxy"),
                                )
                                .await;
                                responder.send(zx::Status::OK.into_raw())
                            }
                            r => panic!("unexpected request {:?}", r),
                        }
                        .expect("failed to send response")
                    })
                    .await
            }
            fidl_fuchsia_paver::PaverRequest::FindBootManager {
                boot_manager,
                control_handle: _,
            } => {
                boot_manager
                    .into_stream()
                    .expect("failed to get request stream")
                    .for_each(|r| {
                        match r.expect("boot manager request error") {
                            fidl_fuchsia_paver::BootManagerRequest::QueryActiveConfiguration {
                                responder,
                            } => {
                                // Return an error so netsvc thinks there's no
                                // active configuration.
                                responder.send(&mut Err(zx::Status::NOT_FOUND.into_raw()))
                            }
                            fidl_fuchsia_paver::BootManagerRequest::Flush { responder } => {
                                responder.send(zx::Status::OK.into_raw())
                            }
                            r => panic!("unexpected request {:?}", r),
                        }
                        .expect("failed to send response");
                        futures::future::ready(())
                    })
                    .await
            }
            r => panic!("unexpected request {:?}", r),
        }
    }

    async fn process_streamed_payload(payload_stream: fidl_fuchsia_paver::PayloadStreamProxy) {
        let vmo = zx::Vmo::create(PAVE_IMAGE_LEN).expect("failed to create VMO");
        let dup = vmo.duplicate_handle(zx::Rights::SAME_RIGHTS).expect("failed to duplicate VMO");
        let () =
            zx::Status::ok(payload_stream.register_vmo(dup).await.expect("calling register_vmo"))
                .expect("register_vmo failed");

        let stream = futures::stream::unfold(payload_stream, |proxy| async move {
            let read_result = proxy.read_data().await.expect("calling read data");
            Some((read_result, proxy))
        });
        let mut payload = [0u8; PAVE_IMAGE_LEN_USIZE];
        let payload = async_utils::fold::fold_while(
            stream,
            (0, &mut payload[..], vmo),
            |(payload_offset, payload, vmo), read_result| {
                futures::future::ready(match read_result {
                    fidl_fuchsia_paver::ReadResult::Eof(eof) => {
                        assert!(eof);
                        async_utils::fold::FoldWhile::Done(&payload[..payload_offset])
                    }
                    fidl_fuchsia_paver::ReadResult::Err(err) => {
                        panic!("error streaming payload {}", zx::Status::from_raw(err))
                    }
                    fidl_fuchsia_paver::ReadResult::Info(fidl_fuchsia_paver::ReadInfo {
                        size,
                        offset,
                    }) => {
                        let size = usize::try_from(size).expect("convert to usize");
                        let () = vmo
                            .read(&mut payload[payload_offset..payload_offset + size], offset)
                            .expect("failed to read VMO");
                        async_utils::fold::FoldWhile::Continue((
                            payload_offset + size,
                            payload,
                            vmo,
                        ))
                    }
                })
            },
        )
        .await
        .short_circuited()
        .expect("stream ended unexpectedly");
        assert_eq!(payload.len(), PAVE_IMAGE_LEN_USIZE);
        let () = validate_image(payload);
    }

    let realm = sandbox
        .create_realm(
            name,
            [
                fidl_fuchsia_netemul::ChildDef {
                    source: Some(fidl_fuchsia_netemul::ChildSource::Component(
                        NETSVC_URL.to_string(),
                    )),
                    name: Some(NETSVC_NAME.to_string()),
                    program_args: Some(args.into_iter().map(Into::into).collect()),
                    uses: Some(fidl_fuchsia_netemul::ChildUses::Capabilities(vec![
                        fidl_fuchsia_netemul::Capability::NetemulDevfs(
                            fidl_fuchsia_netemul::DevfsDep {
                                name: Some(DEV_ETHERNET_DIRECTORY.to_string()),
                                subdir: Some(netemul::Ethernet::DEV_PATH.to_string()),
                                ..fidl_fuchsia_netemul::DevfsDep::EMPTY
                            },
                        ),
                        fidl_fuchsia_netemul::Capability::NetemulDevfs(
                            fidl_fuchsia_netemul::DevfsDep {
                                name: Some(DEV_NETWORK_DIRECTORY.to_string()),
                                subdir: Some(netemul::NetworkDevice::DEV_PATH.to_string()),
                                ..fidl_fuchsia_netemul::DevfsDep::EMPTY
                            },
                        ),
                        fidl_fuchsia_netemul::Capability::ChildDep(
                            fidl_fuchsia_netemul::ChildDep {
                                name: Some(NAME_PROVIDER_NAME.to_string()),
                                capability: Some(
                                    fidl_fuchsia_netemul::ExposedCapability::Protocol(
                                        fidl_fuchsia_device::NameProviderMarker::PROTOCOL_NAME
                                            .to_string(),
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
                                        fidl_fuchsia_logger::LogMarker::PROTOCOL_NAME.to_string(),
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
                                        fidl_fuchsia_sysinfo::SysInfoMarker::PROTOCOL_NAME
                                            .to_string(),
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
                                        fidl_fuchsia_paver::PaverMarker::PROTOCOL_NAME.to_string(),
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
                        fidl_fuchsia_netemul::Capability::NetemulDevfs(
                            fidl_fuchsia_netemul::DevfsDep {
                                name: Some(DEV_NETWORK_DIRECTORY.to_string()),
                                subdir: Some(netemul::NetworkDevice::DEV_PATH.to_string()),
                                ..fidl_fuchsia_netemul::DevfsDep::EMPTY
                            },
                        ),
                        fidl_fuchsia_netemul::Capability::LogSink(fidl_fuchsia_netemul::Empty {}),
                    ])),
                    exposes: Some(vec![
                        fidl_fuchsia_device::NameProviderMarker::PROTOCOL_NAME.to_string()
                    ]),
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

async fn with_netsvc_and_netstack_bind_port<E, F, Fut, A, V, P>(
    port: u16,
    avoid_ports: P,
    name: &str,
    args: V,
    test: F,
) where
    F: FnOnce(fuchsia_async::net::UdpSocket, u32) -> Fut,
    Fut: futures::Future<Output = ()>,
    A: Into<String>,
    V: IntoIterator<Item = A>,
    E: netemul::Endpoint,
    P: IntoIterator<Item = u16>,
{
    let netsvc_name = format!("{}-netsvc", name);
    let ns_name = format!("{}-netstack", name);

    // Create an event stream watcher before starting any realms so we're sure
    // to observe netsvc early stop events.
    let mut component_event_stream = netstack_testing_common::get_component_stopped_event_stream()
        .await
        .expect("get event stream");

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let (netsvc_realm, services) = create_netsvc_realm(&sandbox, &netsvc_name, args);

    let netsvc_stopped_fut = netstack_testing_common::wait_for_component_stopped_with_stream(
        &mut component_event_stream,
        &netsvc_realm,
        NETSVC_NAME,
        None,
    );

    let network = sandbox.create_network("net").await.expect("create network");
    let ep = network.create_endpoint::<E, _>(&netsvc_name).await.expect("create endpoint");
    let () = ep.set_link_up(true).await.expect("set link up");

    let () = netsvc_realm
        .add_virtual_device(&ep, E::dev_path("ep").as_path())
        .await
        .expect("add virtual device");

    let netstack_realm =
        sandbox.create_netstack_realm::<Netstack2, _>(&ns_name).expect("create netstack realm");

    let interface: netemul::TestInterface<'_> =
        netstack_realm.join_network::<E, _>(&network, &ns_name).await.expect("join network");

    let _: net_types::ip::Ipv6Addr = netstack_testing_common::interfaces::wait_for_v6_ll(
        &netstack_realm
            .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
            .expect("connect to protocol"),
        interface.id(),
    )
    .await
    .expect("wait ll address");

    // Bind to the specified ports to avoid later binding to an unspecified port
    // that ends up matching these. Used by tests to avoid receiving unexpected
    // traffic.
    let _captive_ports_socks = futures::stream::iter(avoid_ports.into_iter())
        .then(|port| {
            fuchsia_async::net::UdpSocket::bind_in_realm(
                &netstack_realm,
                std::net::SocketAddrV6::new(
                    std::net::Ipv6Addr::UNSPECIFIED,
                    port,
                    /* flowinfo */ 0,
                    /* scope id */ 0,
                )
                .into(),
            )
            .map(move |r| r.unwrap_or_else(|e| panic!("bind in realm with {port}: {:?}", e)))
        })
        .collect::<Vec<_>>()
        .await;

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

    // Disable looping multicast sockets back to us; That prevents us from
    // seeing our own generated multicast traffic in case the local port we get
    // matches some netsvc service port.
    let () = sock.as_ref().set_multicast_loop_v6(false).expect("failed to disable multicast loop");

    let test_fut = test(sock, interface.id().try_into().expect("interface ID doesn't fit u32"));
    futures::select! {
        r = netsvc_stopped_fut.fuse() => {
            let e: component_events::events::Stopped = r.expect("failed to observe stopped event");
            panic!("netsvc stopped unexpectedly with {:?}", e);
        },
        () = services.fuse() => panic!("ServiceFs ended unexpectedly"),
        () =  test_fut.fuse() => (),
    }
}

const DEFAULT_NETSVC_ARGS: [&str; 3] = ["--netboot", "--all-features", "--log-packets"];

async fn with_netsvc_and_netstack<E, F, Fut>(name: &str, test: F)
where
    F: FnOnce(fuchsia_async::net::UdpSocket, u32) -> Fut,
    Fut: futures::Future<Output = ()>,
    E: netemul::Endpoint,
{
    with_netsvc_and_netstack_bind_port::<E, _, _, _, _, _>(
        /* unspecified port */ 0,
        // Avoid the multicast ports, which will cause test flakes.
        [debuglog::MULTICAST_PORT.get(), netboot::ADVERT_PORT.get()],
        name,
        DEFAULT_NETSVC_ARGS,
        test,
    )
    .await
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

async fn can_discover_inner(sock: fuchsia_async::net::UdpSocket, scope_id: u32) {
    let _: std::net::Ipv6Addr = discover(&sock, scope_id).await;
}

#[variants_test]
async fn can_discover<E: netemul::Endpoint>(name: &str) {
    with_netsvc_and_netstack::<E, _, _>(name, can_discover_inner).await;
}

async fn debuglog_inner(sock: fuchsia_async::net::UdpSocket, _scope_id: u32) {
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
                let msg: &str = pkt.data();
                assert_eq!(
                    msg,
                    format!(
                        "[{:05}.000] {:05}.{:05} [{}] {}\n",
                        seqno, LOG_MSG_PID, LOG_MSG_TID, LOG_MSG_TAG, LOG_MSG_CONTENTS,
                    )
                );

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

#[variants_test]
async fn debuglog<E: netemul::Endpoint>(name: &str) {
    with_netsvc_and_netstack_bind_port::<E, _, _, _, _, _>(
        debuglog::MULTICAST_PORT.get(),
        [],
        name,
        DEFAULT_NETSVC_ARGS,
        debuglog_inner,
    )
    .await
}

async fn get_board_info_inner(sock: fuchsia_async::net::UdpSocket, scope_id: u32) {
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
        board_revision: U32,
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

    expect_oack(
        &sock,
        socket_addr,
        tftp::AllOptions {
            window_size: None,
            block_size: None,
            timeout: Some(tftp::Forceable { value: TIMEOUT_OPTION_SECS, forced: false }),
            transfer_size: Some(tftp::Forceable {
                value: u64::try_from(std::mem::size_of::<BoardInfo>()).expect("doesn't fit u64"),
                forced: false,
            }),
        },
    )
    .await;

    // Acknowledge options by sending an ack.
    let () = send_message(
        tftp::AckPacketBuilder::new(/* block */ 0).into_serializer(),
        &sock,
        socket_addr,
    )
    .await;

    {
        let mut buffer = [0u8; BUFFER_SIZE];
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

#[variants_test]
async fn get_board_info<E: netemul::Endpoint>(name: &str) {
    with_netsvc_and_netstack::<E, _, _>(name, get_board_info_inner).await;
}

async fn start_transfer(
    image_name: &str,
    sock: &fuchsia_async::net::UdpSocket,
    addr: std::net::Ipv6Addr,
    scope_id: u32,
    options: impl IntoIterator<Item = tftp::Forceable<tftp::TftpOption>>,
) {
    let () = send_message(
        tftp::TransferRequestBuilder::new_with_options(
            tftp::TransferDirection::Write,
            image_name,
            tftp::TftpMode::OCTET,
            options,
        )
        .into_serializer(),
        sock,
        // The first message must always go to the INCOMING port. That's
        // what's used to establish a new "session". See
        // https://cs.opensource.google/fuchsia/fuchsia/+/main:src/bringup/bin/netsvc/tftp.cc;l=165;drc=3c621e98789592de213e9899e7056400d29e3b1c.
        std::net::SocketAddrV6::new(
            addr,
            tftp::INCOMING_PORT.get(),
            /* flowinfo */ 0,
            scope_id,
        )
        .into(),
    )
    .await;
}

fn pave_image_contents(block_size: u16) -> itertools::IntoChunks<impl Iterator<Item = u8>> {
    (0u32..).map(u32::to_ne_bytes).flatten().take(PAVE_IMAGE_LEN_USIZE).chunks(block_size.into())
}

/// Helper function to stream paving data `contents` over TFTP over the
/// connected socket `sock`. All responses are going to be asserted as coming
/// from `socket_addr`.
///
/// Returns `Some` if there's a pending unacknowledged block index.
async fn stream_contents<'a, I, B, const BLOCK_SIZE: usize, const WINDOW_SIZE: u16>(
    sock: &fuchsia_async::net::UdpSocket,
    socket_addr: std::net::SocketAddr,
    contents: I,
) -> Option<u16>
where
    I: IntoIterator<Item = itertools::Chunk<'a, B>>,
    B: Iterator<Item = u8> + 'a,
{
    let mut buffer = [0u8; BUFFER_SIZE];
    futures::stream::iter(contents.into_iter().enumerate().map(|(i, b)| (i + 1, b)))
        .fold(None, |_, (index, block)| async move {
            // NB: Collecting into ArrayVec panics if iterator doesn't fit
            // in capacity. See
            // https://docs.rs/arrayvec/latest/arrayvec/struct.ArrayVec.html#impl-FromIterator%3CT%3E.
            let block = block.collect::<arrayvec::ArrayVec<u8, BLOCK_SIZE>>();

            let index = index.try_into().expect("index doesn't fit wire representation");

            let () = send_message(
                (&block[..]).into_serializer().encapsulate(tftp::DataPacketBuilder::new(index)),
                &sock,
                socket_addr,
            )
            .await;
            // Every WINDOW_SIZE blocks must be acknowledged.
            // See https://datatracker.ietf.org/doc/html/rfc7440#section-4.
            if index % WINDOW_SIZE != 0 {
                return Some(index);
            }
            // Wait for an acknowledgement.
            loop {
                let mut pb = &mut buffer[..];
                let ack = read_message::<tftp::TftpPacket<_>, _>(&mut pb, &sock, socket_addr)
                    .await
                    .into_ack()
                    .expect("unexpected response");
                if ack.block() == index {
                    break None;
                }
                // If this is not an acknowledgement for the most recent
                // block, we must be seeing either a retransmission of the
                // acknowledgement on the previous block, or an
                // acknowledgement within the current block if the server
                // observes a timeout waiting for the next block of data.
                let valid_range = (index - WINDOW_SIZE)..index;
                assert!(
                    valid_range.contains(&ack.block()),
                    "acked block {} out of range {:?}",
                    ack.block(),
                    valid_range
                );
            }
        })
        .await
}

async fn expect_oack(
    sock: &fuchsia_async::net::UdpSocket,
    socket_addr: std::net::SocketAddr,
    options: tftp::AllOptions,
) {
    let mut buffer = [0u8; BUFFER_SIZE];
    let mut pb = &mut buffer[..];
    let oack = read_message::<tftp::TftpPacket<_>, _>(&mut pb, &sock, socket_addr)
        .await
        .into_oack()
        .expect("unexpected response");

    let all_options = oack.options().collect();
    assert_eq!(all_options, options);
}

async fn pave(image_name: &str, sock: fuchsia_async::net::UdpSocket, scope_id: u32) {
    let device = discover(&sock, scope_id).await;

    const TIMEOUT_OPTION_SECS: u8 = std::u8::MAX;
    const BLOCK_SIZE: u16 = 1024;
    const WINDOW_SIZE: u16 = 4;

    const TRANSFER_OPTIONS: [tftp::Forceable<tftp::TftpOption>; 4] = [
        tftp::TftpOption::TransferSize(PAVE_IMAGE_LEN).not_forced(),
        // Request a very large timeout to make sure we don't get flakes.
        tftp::TftpOption::Timeout(TIMEOUT_OPTION_SECS).not_forced(),
        tftp::TftpOption::BlockSize(BLOCK_SIZE).not_forced(),
        tftp::TftpOption::WindowSize(WINDOW_SIZE).not_forced(),
    ];

    let () = start_transfer(image_name, &sock, device, scope_id, TRANSFER_OPTIONS).await;

    let socket_addr = std::net::SocketAddrV6::new(
        device,
        tftp::OUTGOING_PORT.get(),
        /* flowinfo */ 0,
        scope_id,
    )
    .into();

    expect_oack(
        &sock,
        socket_addr,
        tftp::AllOptions {
            window_size: Some(tftp::Forceable { value: WINDOW_SIZE, forced: false }),
            block_size: Some(tftp::Forceable { value: BLOCK_SIZE, forced: false }),
            timeout: Some(tftp::Forceable { value: TIMEOUT_OPTION_SECS, forced: false }),
            transfer_size: Some(tftp::Forceable { value: PAVE_IMAGE_LEN, forced: false }),
        },
    )
    .await;

    // Start sending blocks in.
    let contents = pave_image_contents(BLOCK_SIZE);
    let unacked = stream_contents::<_, _, { BLOCK_SIZE as usize }, WINDOW_SIZE>(
        &sock,
        socket_addr,
        &contents,
    )
    .await;
    if let Some(index) = unacked {
        // Wait for final acknowledgement.
        let mut buffer = [0u8; BUFFER_SIZE];
        let mut pb = &mut buffer[..];
        let ack = read_message::<tftp::TftpPacket<_>, _>(&mut pb, &sock, socket_addr)
            .await
            .into_ack()
            .expect("unexpected response");
        assert_eq!(ack.block(), index);
    }

    // The best way to observe the paver terminating is to attempt to start a
    // new transfer.
    loop {
        let () = start_transfer(image_name, &sock, device, scope_id, TRANSFER_OPTIONS).await;
        let mut buffer = [0u8; BUFFER_SIZE];
        let mut pb = &mut buffer[..];
        let pkt = read_message::<tftp::TftpPacket<_>, _>(&mut pb, &sock, socket_addr).await;
        match pkt {
            tftp::TftpPacket::OptionAck(_) => {
                break;
            }
            tftp::TftpPacket::Error(e) => {
                assert_eq!(e.error(), tftp::TftpError::Busy, "unexpected error {:?}", e);
                println!("paver is busy...");
                let () = fuchsia_async::Timer::new(fuchsia_async::Time::after(
                    zx::Duration::from_millis(10),
                ))
                .await;
            }
            p => panic!("unexpected packet {:?}", p),
        }
    }
}

#[variants_test]
#[test_case("zirconr.img"; "recovery")]
#[test_case("sparse.fvm"; "fvm")]
async fn pave_image<E: netemul::Endpoint>(name: &str, image: &str) {
    with_netsvc_and_netstack::<E, _, _>(
        &format!("{}-{}", name, image),
        |sock, scope_id| async move {
            let () = pave(&format!("<<image>>{}", image), sock, scope_id).await;
        },
    )
    .await;
}

#[variants_test]
async fn advertises<E: netemul::Endpoint>(name: &str) {
    let () = with_netsvc_and_netstack_bind_port::<E, _, _, _, _, _>(
        netsvc_proto::netboot::ADVERT_PORT.get(),
        [],
        name,
        IntoIterator::into_iter(DEFAULT_NETSVC_ARGS).chain(["--advertise"]),
        |sock, scope| async move {
            let mut buffer = [0u8; BUFFER_SIZE];
            let (n, addr) = sock.recv_from(&mut buffer[..]).await.expect("recv from failed");
            match addr {
                std::net::SocketAddr::V6(addr) => {
                    assert_eq!(addr.scope_id(), scope);
                    assert!(
                        net_types::ip::Ipv6Addr::from_bytes(addr.ip().octets())
                            .is_unicast_link_local(),
                        "{} is not a unicast link local address",
                        addr
                    );
                }
                std::net::SocketAddr::V4(addr) => panic!("unexpected IPv4 address {}", addr),
            }

            let mut buffer = &mut buffer[..n];
            let pkt = buffer.parse::<netboot::NetbootPacket<_>>().expect("parse failed");
            assert_eq!(pkt.command(), netboot::OpcodeOrErr::Op(netboot::Opcode::Advertise));
            let payload =
                std::str::from_utf8(pkt.payload()).expect("failed to parse advertisement");
            let (nodename, version) =
                payload.split(';').fold((None, None), |(nodename, version), kv| {
                    let mut it = kv.split('=');
                    let k = it.next().unwrap_or_else(|| panic!("missing key on {}", kv));
                    let v = it.next().unwrap_or_else(|| panic!("missing value on {}", kv));
                    assert_eq!(it.next(), None);
                    match k {
                        "nodename" => {
                            assert_eq!(nodename, None);
                            (Some(v), version)
                        }
                        "version" => {
                            assert_eq!(version, None);
                            (nodename, Some(v))
                        }
                        k => panic!("unexpected key {} = {}", k, v),
                    }
                });
            // No checks on version other than presence and not empty.
            assert_matches::assert_matches!(version, Some(v) if !v.is_empty());
            assert_matches::assert_matches!(nodename, Some(n) if n.starts_with("fuchsia-"));
        },
    )
    .await;
}

#[variants_test]
async fn survives_device_removal<E: netemul::Endpoint>(name: &str) {
    use packet_formats::ethernet::{EthernetFrame, EthernetFrameLengthCheck};

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");

    // Create an event stream watcher before starting any realms so we're sure
    // to observe netsvc early stop events.
    let mut component_event_stream = netstack_testing_common::get_component_stopped_event_stream()
        .await
        .expect("get event stream");

    // NB: We intentionally don't poll `services` since we don't need to
    // observe proper interaction with them.
    let (netsvc_realm, _services) = create_netsvc_realm(
        &sandbox,
        name,
        IntoIterator::into_iter(DEFAULT_NETSVC_ARGS).chain(["--advertise"]),
    );
    let network = sandbox.create_network("net").await.expect("create network");
    let fake_ep = network.create_fake_endpoint().expect("create fake endpoint");
    let mut frames = fake_ep.frame_stream().map(|r| {
        let (frame, dropped) = r.expect("failed to read frame");
        assert_eq!(dropped, 0);
        let mut buffer_view = &frame[..];
        let eth = buffer_view
            .parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::NoCheck)
            .expect("failed to parse ethernet");
        eth.src_mac()
    });

    let netsvc_stopped_fut = netstack_testing_common::wait_for_component_stopped_with_stream(
        &mut component_event_stream,
        &netsvc_realm,
        NETSVC_NAME,
        None,
    );

    let test_fut = async {
        for i in 0..3 {
            let ep_name = format!("ep-{:?}-{}", E::NETEMUL_BACKING, i);
            let mac = net_types::ethernet::Mac::new([2, 3, 4, 5, 6, i]);
            let ep = network
                .create_endpoint_with(
                    &ep_name,
                    E::make_config(
                        netemul::DEFAULT_MTU,
                        Some(fidl_fuchsia_net::MacAddress { octets: mac.bytes() }),
                    ),
                )
                .await
                .expect("create endpoint");
            let () = ep.set_link_up(true).await.expect("set link up");
            let () = netsvc_realm
                .add_virtual_device(&ep, E::dev_path(&ep_name).as_path())
                .await
                .expect("add virtual device");

            // Wait until we observe any netsvc packet with the source mac set to
            // our endpoint, as proof that it is alive.
            frames
                .by_ref()
                .filter_map(|src_mac| {
                    futures::future::ready(if src_mac == mac {
                        Some(())
                    } else {
                        println!("ignoring frame with mac {}", src_mac);
                        None
                    })
                })
                .next()
                .await
                .expect("frames stream ended unexpectedly");

            // Destroy the device backed by netemul. Netsvc must survive this
            // and observe new devices in future iterations.
            drop(ep);
        }
    };
    futures::select! {
        r = netsvc_stopped_fut.fuse() => {
            let e: component_events::events::Stopped = r.expect("failed to observe stopped event");
            panic!("netsvc stopped unexpectedly with {:?}", e);
        },
        () =  test_fut.fuse() => (),
    }
}

/// Tests that netsvc retransmits ACKs following the timeout option. Guards
/// against a regression where netsvc was not sending ACKs when it was receiving
/// retransmitted blocks from the host.
#[variants_test]
async fn retransmits_acks<E: netemul::Endpoint>(name: &str) {
    with_netsvc_and_netstack::<E, _, _>(name, |sock, scope_id| async move {
        let device = discover(&sock, scope_id).await;
        let image_name = "<<image>>sparse.fvm";

        /// This controls how often ACK retransmits will be sent. We don't use
        /// the minimum value of 1 second here because the paver will timeout at
        /// a fixed integer multiplier of the TFTP timeout. 2s here gives us
        /// more leeway over there to protect against flakes without making this
        /// test too slow.
        const TIMEOUT_OPTION_SECS: u8 = 2;
        const BLOCK_SIZE: u16 = 1024;
        const WINDOW_SIZE: u16 = 2;

        let () = start_transfer(
            image_name,
            &sock,
            device,
            scope_id,
            [
                tftp::TftpOption::TransferSize(PAVE_IMAGE_LEN).not_forced(),
                tftp::TftpOption::Timeout(TIMEOUT_OPTION_SECS).not_forced(),
                tftp::TftpOption::BlockSize(BLOCK_SIZE).not_forced(),
                tftp::TftpOption::WindowSize(WINDOW_SIZE).not_forced(),
            ],
        )
        .await;

        let socket_addr = std::net::SocketAddrV6::new(
            device,
            tftp::OUTGOING_PORT.get(),
            /* flowinfo */ 0,
            scope_id,
        )
        .into();

        expect_oack(
            &sock,
            socket_addr,
            tftp::AllOptions {
                window_size: Some(tftp::Forceable { value: WINDOW_SIZE, forced: false }),
                block_size: Some(tftp::Forceable { value: BLOCK_SIZE, forced: false }),
                timeout: Some(tftp::Forceable { value: TIMEOUT_OPTION_SECS, forced: false }),
                transfer_size: Some(tftp::Forceable { value: PAVE_IMAGE_LEN, forced: false }),
            },
        )
        .await;

        // Only send the first window.
        let contents = pave_image_contents(BLOCK_SIZE);
        let contents = contents.into_iter().take(WINDOW_SIZE.into());
        let unacked = stream_contents::<_, _, { BLOCK_SIZE as usize }, WINDOW_SIZE>(
            &sock,
            socket_addr,
            contents,
        )
        .await;
        assert_eq!(unacked, None);

        // Wait to observe ack retransmits while blasting more traffic at the
        // same time. The valid, but repeated, traffic received by netsvc should
        // not keep it from sending its retransmissions.
        let wait_acks = futures::stream::repeat(())
            .take(2)
            .then(|()| async {
                let mut buffer = [0u8; BUFFER_SIZE];
                let mut pb = &mut buffer[..];
                let ack = read_message::<tftp::TftpPacket<_>, _>(&mut pb, &sock, socket_addr)
                    .await
                    .into_ack()
                    .expect("unexpected response");
                assert_eq!(ack.block(), WINDOW_SIZE);
            })
            .collect::<()>();

        fuchsia_async::Interval::new(zx::Duration::from_millis(200))
            .then(|()| async {
                let contents = pave_image_contents(BLOCK_SIZE);
                let contents = contents.into_iter().take(WINDOW_SIZE.into());
                // Lie about window size here so stream_contents won't wait for acks.
                let unacked =
                    stream_contents::<_, _, { BLOCK_SIZE as usize }, { WINDOW_SIZE + 1 }>(
                        &sock,
                        socket_addr,
                        contents,
                    )
                    .await;
                assert_eq!(unacked, Some(WINDOW_SIZE));
            })
            .take_until(wait_acks)
            .collect::<()>()
            .await;
    })
    .await;
}
