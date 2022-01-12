// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::ProtocolMarker as _;
use fixture::fixture;
use fuchsia_zircon as zx;
use futures::{FutureExt as _, Stream, StreamExt as _, TryStreamExt as _};
use net_declare::std_socket_addr;
use net_types::Witness as _;
use netemul::{Endpoint as _, RealmUdpSocket as _};
use netstack_testing_common::realms::{Netstack2, TestSandboxExt as _};
use netsvc_proto::netboot;
use packet::{FragmentedBuffer as _, InnerPacketBuilder as _, ParseBuffer as _, Serializer as _};
use std::borrow::Cow;
use std::convert::TryInto as _;

const NETSVC_URL: &str = "#meta/netsvc.cm";
const NETSVC_NAME: &str = "netsvc";

const NAME_PROVIDER_URL: &str = "#meta/device-name-provider.cm";
const NAME_PROVIDER_NAME: &str = "device-name-provider";

const MOCK_SERVICES_NAME: &str = "mock";

const DEV_ETHERNET_DIRECTORY: &str = "dev-class-ethernet";

const BUFFER_SIZE: usize = 2048;

fn create_netsvc_realm<'a>(
    sandbox: &'a netemul::TestSandbox,
    name: impl Into<Cow<'a, str>>,
) -> (netemul::TestRealm<'a>, impl Stream<Item = ()> + Unpin) {
    use fuchsia_component::server::{ServiceFs, ServiceFsDir};

    let (mock_dir, server_end) = fidl::endpoints::create_endpoints().expect("create endpoints");

    let mut fs = ServiceFs::new();
    let _: &mut ServiceFsDir<'_, _> =
        fs.dir("svc").add_fidl_service(|rs: fidl_fuchsia_boot::ReadOnlyLogRequestStream| {
            rs.map_ok(|fidl_fuchsia_boot::ReadOnlyLogRequest::Get { responder }| {
                // TODO(https://fxbug.dev/91150): Move netsvc to use LogListener
                // instead. We're temporarily using a loophole here that
                // zx_debuglog_create accepts an invalid root resource handle,
                // but that might not be true forever.
                let debuglog =
                    zx::DebugLog::create(&zx::Handle::invalid().into(), zx::DebugLogOpts::READABLE)
                        .expect("failed to create debuglog handle");
                let () = responder.send(debuglog).expect("failed to respond");
            })
        });
    let _: &mut ServiceFs<_> =
        fs.serve_connection(server_end.into_channel()).expect("serve connection");

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

    (realm, fs.flatten().map(|r| r.expect("fs error")))
}

async fn with_netsvc_and_netstack<F, Fut>(name: &str, test: F)
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

    let sock =
        fuchsia_async::net::UdpSocket::bind_in_realm(&netstack_realm, std_socket_addr!("[::]:0"))
            .await
            .expect("bind in realm");

    let test_fut = test(sock, interface.id().try_into().expect("interface ID doesn't fit u32"));
    futures::select! {
        () = services.collect().fuse() => panic!("ServiceFs ended unexpectedly"),
        () =  test_fut.fuse() => (),
    }
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

#[fixture(with_netsvc_and_netstack)]
#[fuchsia_async::run_singlethreaded(test)]
async fn can_discover(sock: fuchsia_async::net::UdpSocket, scope_id: u32) {
    let _: std::net::Ipv6Addr = discover(&sock, scope_id).await;
}
