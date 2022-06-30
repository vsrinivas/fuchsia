// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use derivative::Derivative;
use fidl::endpoints::Proxy as _;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use fidl_fuchsia_net_interfaces_ext as fnet_interfaces_ext;
use fidl_fuchsia_net_multicast_admin as fnet_multicast_admin;
use fuchsia_async::{self as fasync, DurationExt as _, TimeoutExt as _};
use fuchsia_zircon::{self as zx};
use futures::{StreamExt as _, TryFutureExt as _, TryStreamExt as _};
use lazy_static::lazy_static;
use maplit::hashmap;
use net_declare::{fidl_ip_v4, fidl_subnet};
use netemul::RealmUdpSocket as _;
use netstack_testing_common::{
    interfaces,
    realms::{Netstack2, TestSandboxExt as _},
};
use netstack_testing_macros::variants_test;
use std::collections::HashMap;
use test_case::test_case;

lazy_static! {
    /// Client device configs for a `MulticastForwardingNetwork`.
    static ref CLIENT_CONFIGS: HashMap<Client, RouterConnectedDeviceConfig<'static>> = hashmap! {
        Client::A => RouterConnectedDeviceConfig {
            name: "clientA",
            ep_addr: fidl_subnet!("192.168.1.2/24"),
            router_ep_addr: fidl_subnet!("192.168.1.1/24"),
        },
        Client::B => RouterConnectedDeviceConfig {
            name: "clientB",
            ep_addr: fidl_subnet!("192.168.2.2/24"),
            router_ep_addr: fidl_subnet!("192.168.2.1/24"),
        },
    };

    /// Server device configs for a `MulticastForwardingNetwork`.
    static ref SERVER_CONFIGS: HashMap<Server, RouterConnectedDeviceConfig<'static>> = hashmap! {
        Server::A => RouterConnectedDeviceConfig {
            name: "serverA",
            ep_addr: fidl_subnet!("192.168.0.2/24"),
            router_ep_addr: fidl_subnet!("192.168.0.1/24"),
        },
        Server::B => RouterConnectedDeviceConfig {
            name: "serverB",
            ep_addr: fidl_subnet!("192.168.3.1/24"),
            router_ep_addr: fidl_subnet!("192.168.3.2/24"),
        },
    };
}

const IPV4_MULTICAST_ADDR: fnet::Ipv4Address = fidl_ip_v4!("225.0.0.0");
const IPV4_ANY_ADDR: fnet::Ipv4Address = fidl_ip_v4!("0.0.0.0");
const IPV4_LINK_LOCAL_UNICAST_ADDR: fnet::Ipv4Address = fidl_ip_v4!("169.254.0.10");
const IPV4_UNICAST_ADDR: fnet::Ipv4Address = fidl_ip_v4!("192.168.0.2");
const IPV4_LINK_LOCAL_MULTICAST_ADDR: fnet::Ipv4Address = fidl_ip_v4!("224.0.0.1");

/// Identifier for a device that listens for multicast packets.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
enum Client {
    A,
    B,
}

/// Identifier for a device that sends multicast packets.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
enum Server {
    A,
    B,
}

/// A network that is configured to send, forward, and receive multicast
/// packets.
///
/// Generates a network with the following structure:
///
/// Server::A ─┐        ┌─ Client::A
///            │        │
///            ├─router─┼
///            │        │
/// Server::B ─┘        └─ Client::B
///
/// The device types and their responsibilities are as follows:
///
///  - A server sends multicast packets to the router.
///  - The router optionally forwards and/or delivers multicast packets locally.
///    Additionally, if configured, the router sends multicast packets.
///  - A client listens for forwarded multicast packets.
struct MulticastForwardingNetwork<'a> {
    router_listener_socket: Option<fasync::net::UdpSocket>,
    router_realm: &'a netemul::TestRealm<'a>,
    clients: std::collections::HashMap<Client, ClientDevice<'a>>,
    servers: std::collections::HashMap<Server, RouterConnectedDevice<'a>>,
    options: MulticastForwardingNetworkOptions,
}

impl<'a> MulticastForwardingNetwork<'a> {
    const PAYLOAD: &'static str = "Hello multicast";

    async fn new<E: netemul::Endpoint>(
        name: &'a str,
        sandbox: &'a netemul::TestSandbox,
        router_realm: &'a netemul::TestRealm<'a>,
        options: MulticastForwardingNetworkOptions,
    ) -> MulticastForwardingNetwork<'a> {
        let MulticastForwardingNetworkOptions {
            multicast_addr,
            source_device,
            enable_multicast_forwarding,
            listen_from_router,
            packet_ttl: _,
        } = options;

        let multicast_socket_addr = std::net::SocketAddr::V4(multicast_addr);
        let servers: HashMap<_, _> = futures::stream::iter(SERVER_CONFIGS.iter())
            .then(|(server, config)| async move {
                let device =
                    create_router_connected_device::<E>(name, sandbox, router_realm, config).await;
                (*server, device)
            })
            .collect()
            .await;
        let clients: HashMap<_, _> = futures::stream::iter(CLIENT_CONFIGS.iter())
            .then(|(client, config)| async move {
                let device =
                    create_router_connected_device::<E>(name, sandbox, router_realm, config).await;
                let socket =
                    create_listener_socket(&device.realm, device.ep_addr, multicast_socket_addr)
                        .await;
                (*client, ClientDevice { device, socket })
            })
            .collect()
            .await;

        let input_server = match source_device {
            SourceDevice::Router(server) | SourceDevice::Server(server) => server,
        };
        let input_server_device = servers.get(&input_server).expect("input server not found");

        set_multicast_forwarding(
            input_server_device.router_interface.control(),
            enable_multicast_forwarding,
        )
        .await;

        let router_listener_socket = if listen_from_router {
            Some(
                create_listener_socket(
                    &router_realm,
                    input_server_device.router_ep_addr,
                    multicast_socket_addr,
                )
                .await,
            )
        } else {
            None
        };
        MulticastForwardingNetwork {
            router_listener_socket: router_listener_socket,
            router_realm: router_realm,
            clients: clients,
            servers: servers,
            options: options,
        }
    }

    /// Sends a single multicast packet from a configured server to the router.
    async fn send_multicast_packet(&self) {
        let (realm, addr) = match self.options.source_device {
            SourceDevice::Router(server) => {
                (self.router_realm, self.get_server(server).router_ep_addr)
            }
            SourceDevice::Server(server) => {
                let server_device = self.get_server(server);
                (&server_device.realm, server_device.ep_addr)
            }
        };

        let server_sock = fasync::net::UdpSocket::bind_in_realm(
            realm,
            std::net::SocketAddr::V4(self.options.multicast_addr),
        )
        .await
        .expect("bind_in_realm failed for server socket");

        let interface_addr = get_ipv4_address(addr);
        server_sock
            .as_ref()
            .set_multicast_if_v4(&interface_addr.addr.into())
            .expect("set_multicast_if_v4 failed");
        server_sock
            .as_ref()
            .set_multicast_ttl_v4(self.options.packet_ttl.into())
            .expect("set_multicast_ttl_v4 failed");

        let r = server_sock
            .send_to(
                Self::PAYLOAD.as_bytes(),
                std::net::SocketAddr::V4(self.options.multicast_addr),
            )
            .await
            .expect("send_to failed");
        assert_eq!(r, Self::PAYLOAD.as_bytes().len());
    }

    /// Verifies the receipt of a multicast packet against the provided
    /// `expectations`.
    ///
    /// The `expectations` should contain an entry for each `Client` that should
    /// be verified. If the value of the entry is true, then the `Client` must
    /// receive a multicast packet. If the value is false, then the `Client`
    /// must not receive a multicast packet.
    async fn receive_multicast_packet(
        &self,
        expectations: std::collections::HashMap<Client, bool>,
    ) {
        futures::stream::iter(
            expectations
                .into_iter()
                .map(|(client, expect_forwarded_packet)| {
                    (
                        format!("client{:?}", client),
                        &self.get_client(client).socket,
                        expect_forwarded_packet,
                    )
                })
                .chain(
                    self.router_listener_socket
                        .as_ref()
                        .and_then(|socket| Some(("router".to_string(), socket, true))),
                ),
        )
        .for_each_concurrent(None, |(name, socket, expect_packet)| async move {
            let mut buf = [0u8; 1024];
            let timeout = if expect_packet {
                netstack_testing_common::ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT
            } else {
                netstack_testing_common::ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT
            };
            match (
                socket
                    .recv_from(&mut buf[..])
                    .map_ok(Some)
                    .on_timeout(timeout.after_now(), || Ok(None))
                    .await
                    .expect("recv_from failed"),
                expect_packet,
            ) {
                (Some((r, from)), true) => {
                    assert_eq!(
                        from,
                        std::net::SocketAddr::V4(create_socket_addr_v4(self.get_source_address()))
                    );
                    assert_eq!(r, Self::PAYLOAD.as_bytes().len());
                    assert_eq!(&buf[..r], Self::PAYLOAD.as_bytes());
                }
                (Some((_r, from)), false) => {
                    panic!("{} unexpectedly received packet from {:?}", name, from)
                }
                (None, true) => panic!("{} failed to receive packet", name),
                (None, false) => {}
            }
        })
        .await;
    }

    /// Sends a multicast packet and verifies the receipt of the packet against
    /// the provided `expectations`.
    ///
    /// See `receive_multicast_packet` for details regarding the expected format
    /// of the `expectations`.
    async fn send_and_receive_multicast_packet(
        &self,
        expectations: std::collections::HashMap<Client, bool>,
    ) {
        let send_fut = self.send_multicast_packet();
        let receive_fut = self.receive_multicast_packet(expectations);

        let ((), ()) = futures::future::join(send_fut, receive_fut).await;
    }

    fn get_server(&self, server: Server) -> &RouterConnectedDevice<'_> {
        self.servers.get(&server).unwrap_or_else(|| panic!("server {:?} not found", server))
    }

    fn get_client(&self, client: Client) -> &ClientDevice<'_> {
        self.clients.get(&client).unwrap_or_else(|| panic!("client {:?} not found", client))
    }

    /// Returns the address of the interface that sends multicast packets.
    fn get_source_address(&self) -> fnet::Ipv4Address {
        let subnet = match self.options.source_device {
            SourceDevice::Router(server) => self.get_server(server).router_ep_addr,
            SourceDevice::Server(server) => self.get_server(server).ep_addr,
        };
        get_ipv4_address(subnet)
    }

    /// Returns the interface ID of the router interface handles incoming
    /// multicast packets.
    fn get_source_router_interface_id(&self) -> u64 {
        match self.options.source_device {
            SourceDevice::Router(server) | SourceDevice::Server(server) => {
                self.get_server(server).router_interface.id()
            }
        }
    }

    fn get_device_address(&self, address: DeviceAddress) -> fnet::Ipv4Address {
        match address {
            DeviceAddress::Router(server) => {
                get_ipv4_address(self.get_server(server).router_ep_addr)
            }
            DeviceAddress::Server(server) => get_ipv4_address(self.get_server(server).ep_addr),
            DeviceAddress::Other(addr) => addr,
        }
    }

    fn default_unicast_source_and_multicast_destination(
        &self,
    ) -> fnet_multicast_admin::Ipv4UnicastSourceAndMulticastDestination {
        fnet_multicast_admin::Ipv4UnicastSourceAndMulticastDestination {
            unicast_source: self.get_source_address(),
            multicast_destination: fnet::Ipv4Address {
                addr: self.options.multicast_addr.ip().octets(),
            },
        }
    }

    fn default_multicast_route(&self) -> fnet_multicast_admin::Route {
        fnet_multicast_admin::Route {
            expected_input_interface: Some(self.get_source_router_interface_id()),
            action: Some(fnet_multicast_admin::Action::OutgoingInterfaces(vec![
                fnet_multicast_admin::OutgoingInterfaces {
                    id: self.get_client(Client::A).device.router_interface.id(),
                    min_ttl: self.options.packet_ttl - 1,
                },
            ])),
            ..fnet_multicast_admin::Route::EMPTY
        }
    }
}

/// The device that should send multicast packets.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
enum SourceDevice {
    /// The router should use the interface that is connected to the provided
    /// server to send multicast packets.
    Router(Server),
    /// The proivded server should send multicast packets.
    Server(Server),
}

/// Defaultable options for configuring a `MulticastForwardingNetwork`.
#[derive(Derivative)]
#[derivative(Default)]
struct MulticastForwardingNetworkOptions {
    /// The TTL of multicast packets sent over the network.
    #[derivative(Default(value = "2"))]
    packet_ttl: u8,
    /// The device that sends multicast packets.
    #[derivative(Default(value = "SourceDevice::Server(Server::A)"))]
    source_device: SourceDevice,
    #[derivative(Default(value = "true"))]
    enable_multicast_forwarding: bool,
    /// If true, then the router will also act as a listener for multicast
    /// packets.
    #[derivative(Default(value = "false"))]
    listen_from_router: bool,
    /// The multicast group address that packets should be sent to and that
    /// listeners should join.
    #[derivative(Default(value = "create_socket_addr_v4(IPV4_MULTICAST_ADDR)"))]
    multicast_addr: std::net::SocketAddrV4,
}

/// Configuration for a device that is connected to a router.
///
/// The device and the router are connected via the interfaces that correspond
/// to `ep_addr` and `router_ep_addr`.
struct RouterConnectedDeviceConfig<'a> {
    name: &'a str,
    /// The address of an interface that should be added to the device.
    ep_addr: fnet::Subnet,
    /// The address of a connecting interface that should be added to the
    /// router.
    router_ep_addr: fnet::Subnet,
}

/// A router connected device that has been initialized from a
/// `RouterConnectedDeviceConfig`.
struct RouterConnectedDevice<'a> {
    realm: netemul::TestRealm<'a>,
    ep_addr: fnet::Subnet,
    router_ep_addr: fnet::Subnet,
    router_interface: netemul::TestInterface<'a>,

    _network: netemul::TestNetwork<'a>,
    _interface: netemul::TestInterface<'a>,
}

/// A device that is connected to a router and is listening for multicast
/// traffic.
struct ClientDevice<'a> {
    device: RouterConnectedDevice<'a>,
    socket: fasync::net::UdpSocket,
}

/// Adds the `addr` to the `interface`.
async fn add_address(interface: &netemul::TestInterface<'_>, addr: fnet::Subnet) {
    let address_state_provider = interfaces::add_address_wait_assigned(
        &interface.control(),
        addr,
        fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
    )
    .await
    .expect("add_address_wait_assigned failed");
    let () = address_state_provider.detach().expect("detach failed");
}

/// Creates a `RouterConnectedDevice` from the provided `config` that is
/// connected to the `router_realm`.
async fn create_router_connected_device<'a, E: netemul::Endpoint>(
    name: &'a str,
    sandbox: &'a netemul::TestSandbox,
    router_realm: &'a netemul::TestRealm<'a>,
    config: &'a RouterConnectedDeviceConfig<'a>,
) -> RouterConnectedDevice<'a> {
    let realm = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_{}_realm", name, config.name))
        .expect("create_netstack_realm failed");
    let network = sandbox
        .create_network(format!("{}_{}_network", name, config.name))
        .await
        .expect("create_network failed");
    let interface = realm
        .join_network::<E, _>(&network, format!("{}-{}-ep", name, config.name))
        .await
        .expect("join_network failed for router connected device");
    add_address(&interface, config.ep_addr).await;

    let router_interface = router_realm
        .join_network::<E, _>(&network, format!("{}-router-{}-ep", name, config.name))
        .await
        .expect("join_network failed for router");
    add_address(&router_interface, config.router_ep_addr).await;

    RouterConnectedDevice {
        realm: realm,
        _network: network,
        _interface: interface,
        ep_addr: config.ep_addr,
        router_ep_addr: config.router_ep_addr,
        router_interface: router_interface,
    }
}

/// Creates a socket that has joined the `multicast_addr` group.
///
/// The socket joins the group using the interface that corresponds to
/// `interface_address`.
async fn create_listener_socket<'a>(
    realm: &'a netemul::TestRealm<'a>,
    interface_address: fnet::Subnet,
    multicast_addr: std::net::SocketAddr,
) -> fasync::net::UdpSocket {
    let socket = fasync::net::UdpSocket::bind_in_realm(realm, multicast_addr)
        .await
        .expect("bind_in_realm failed");

    let iface_addr = get_ipv4_address(interface_address);

    let multicast_v4_addr = match multicast_addr.ip() {
        std::net::IpAddr::V4(ipv4) => ipv4,
        std::net::IpAddr::V6(ipv6) => panic!("multicast_addr unexpectedly IPv6: {:?}", ipv6),
    };
    socket
        .as_ref()
        .join_multicast_v4(&multicast_v4_addr, &iface_addr.addr.into())
        .expect("join_multicast_v4 failed");
    socket
}

/// Sets the multicast forwarding state (enabled or disabled) for the provided
/// `interface`.
async fn set_multicast_forwarding(interface: &fnet_interfaces_ext::admin::Control, enabled: bool) {
    let config = fnet_interfaces_admin::Configuration {
        ipv4: Some(fnet_interfaces_admin::Ipv4Configuration {
            multicast_forwarding: Some(enabled),
            ..fnet_interfaces_admin::Ipv4Configuration::EMPTY
        }),
        ..fnet_interfaces_admin::Configuration::EMPTY
    };

    let _prev_config: fnet_interfaces_admin::Configuration = interface
        .set_configuration(config)
        .await
        .expect("set_configuration failed")
        .expect("set_configuration error");
}

async fn expect_table_controller_closed_with_reason(
    controller: &fnet_multicast_admin::Ipv4RoutingTableControllerProxy,
    expected_reason: fnet_multicast_admin::TableControllerCloseReason,
) {
    let fnet_multicast_admin::Ipv4RoutingTableControllerEvent::OnClose { error: reason } =
        controller
            .take_event_stream()
            .try_next()
            .await
            .expect("read Ipv4RoutingTableController event")
            .expect("Ipv4RoutingTableController event stream ended unexpectedly");
    assert_eq!(reason, expected_reason);
    assert_eq!(controller.on_closed().await, Ok(zx::Signals::CHANNEL_PEER_CLOSED));
}

/// Returns an `fnet::Ipv4Address` from the `subnet` or panics if one does not
/// exist.
fn get_ipv4_address(subnet: fnet::Subnet) -> fnet::Ipv4Address {
    match subnet.addr {
        fnet::IpAddress::Ipv4(ipv4) => ipv4,
        fnet::IpAddress::Ipv6(_ipv6) => panic!("subnet unexpectedly IPv6: {:?}", subnet),
    }
}

fn create_socket_addr_v4(addr: fnet::Ipv4Address) -> std::net::SocketAddrV4 {
    std::net::SocketAddrV4::new(addr.addr.into(), 1234)
}

/// Configuration for a client device that has joined a multicast group.
struct ClientConfig {
    route_min_ttl: u8,
    expect_forwarded_packet: bool,
}

/// The address of a particular device.
enum DeviceAddress {
    /// The address of a router interface that is connected to the provided
    /// server.
    Router(Server),
    /// The address of a `Server`.
    Server(Server),
    /// A manually specified address.
    Other(fnet::Ipv4Address),
}

/// Defaultable options for a multicast forwarding test.
#[derive(Derivative)]
#[derivative(Default)]
struct MulticastForwardingTestOptions {
    #[derivative(Default(value = "Server::A"))]
    route_input_interface: Server,
    #[derivative(Default(value = "DeviceAddress::Server(Server::A)"))]
    route_source_address: DeviceAddress,
    #[derivative(Default(value = "false"))]
    drop_multicast_controller: bool,
}

#[variants_test]
#[test_case(
    "ttl_same_as_route_min_ttl",
    hashmap! {
        Client::A => ClientConfig {
            route_min_ttl: 1,
            expect_forwarded_packet: true,
        },
        Client::B => ClientConfig {
            route_min_ttl: 1,
            expect_forwarded_packet: true,
        }
    },
    MulticastForwardingNetworkOptions {
        packet_ttl: 1,
        ..MulticastForwardingNetworkOptions::default()
    },
    MulticastForwardingTestOptions::default();
    "ttl same as route min ttl"
)]
#[test_case(
    "packet_sent_from_router",
    hashmap! {
        Client::A => ClientConfig {
            route_min_ttl: 1,
            expect_forwarded_packet: true,
        },
        Client::B => ClientConfig {
            route_min_ttl: 1,
            expect_forwarded_packet: true,
        }
    },
    MulticastForwardingNetworkOptions {
        packet_ttl: 1,
        source_device: SourceDevice::Router(Server::A),
        ..MulticastForwardingNetworkOptions::default()
    },
    MulticastForwardingTestOptions {
        route_source_address: DeviceAddress::Router(Server::A),
        ..MulticastForwardingTestOptions::default()
    };
    "packet_sent_from_router"
)]
#[test_case(
    "ttl_greater_than_and_less_than_route_min_ttl",
    hashmap! {
        Client::A => ClientConfig {
            route_min_ttl: 1,
            expect_forwarded_packet: true,
        },
        Client::B => ClientConfig {
            route_min_ttl: 3,
            expect_forwarded_packet: false,
        }
    },
    MulticastForwardingNetworkOptions {
        packet_ttl: 2,
        ..MulticastForwardingNetworkOptions::default()
    },
    MulticastForwardingTestOptions::default();
    "ttl greater than and less than route min ttl"
)]
#[test_case(
    "unexpected_input_interface",
    hashmap! {
        Client::A => ClientConfig {
            route_min_ttl: 1,
            expect_forwarded_packet: false,
        }
    },
    MulticastForwardingNetworkOptions {
        source_device: SourceDevice::Server(Server::A),
        ..MulticastForwardingNetworkOptions::default()
    },
    MulticastForwardingTestOptions {
        route_source_address: DeviceAddress::Server(Server::A),
        route_input_interface: Server::B,
        ..MulticastForwardingTestOptions::default()
    };
    "unexpected input interface"
)]
#[test_case(
    "multicast_forwarding_disabled",
    hashmap! {
        Client::A => ClientConfig {
            route_min_ttl: 1,
            expect_forwarded_packet: false,
        }
    },
    MulticastForwardingNetworkOptions {
        enable_multicast_forwarding: false,
        ..MulticastForwardingNetworkOptions::default()
    },
    MulticastForwardingTestOptions::default();
    "multicast forwarding disabled"
)]
#[test_case(
    "multicast_controller_dropped",
    hashmap! {
        Client::A => ClientConfig {
            route_min_ttl: 1,
            // Dropping the multicast controller results in the routing
            // table being cleared. As a result, the packet should not be
            // forwarded.
            expect_forwarded_packet: false,
        }
    },
    MulticastForwardingNetworkOptions::default(),
    MulticastForwardingTestOptions {
        drop_multicast_controller: true,
        ..MulticastForwardingTestOptions::default()
    };
    "multicast controller dropped"
)]
#[test_case(
    "forwarded_and_delivered_locally",
    hashmap! {
        Client::A => ClientConfig {
            route_min_ttl: 1,
            expect_forwarded_packet: true,
        }
    },
    MulticastForwardingNetworkOptions {
        listen_from_router: true,
        ..MulticastForwardingNetworkOptions::default()
    },
    MulticastForwardingTestOptions::default();
    "forwarded and delivered locally"
)]
#[test_case(
    "only_delivered_locally",
    hashmap! {
        Client::A => ClientConfig {
            route_min_ttl: 2,
            expect_forwarded_packet: false,
        }
    },
    MulticastForwardingNetworkOptions {
        packet_ttl: 1,
        listen_from_router: true,
        ..MulticastForwardingNetworkOptions::default()
    },
    MulticastForwardingTestOptions::default();
    "only delivered locally"
)]
#[test_case(
    "missing_route",
    hashmap! {
        Client::A => ClientConfig {
            route_min_ttl: 1,
            expect_forwarded_packet: false,
        }
    },
    MulticastForwardingNetworkOptions {
        source_device: SourceDevice::Server(Server::A),
        listen_from_router: true,
        ..MulticastForwardingNetworkOptions::default()
    },
    MulticastForwardingTestOptions {
        route_source_address: DeviceAddress::Server(Server::B),
        ..MulticastForwardingTestOptions::default()
    };
    "missing route"
)]
async fn multicast_forwarding<E: netemul::Endpoint>(
    name: &str,
    case_name: &str,
    clients: HashMap<Client, ClientConfig>,
    network_options: MulticastForwardingNetworkOptions,
    options: MulticastForwardingTestOptions,
) {
    let MulticastForwardingTestOptions {
        route_input_interface,
        route_source_address,
        drop_multicast_controller,
    } = options;
    let test_name = format!("{}_{}", name, case_name);
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let router_realm = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_router_realm", &test_name))
        .expect("create realm");
    let test_network =
        MulticastForwardingNetwork::new::<E>(&test_name, &sandbox, &router_realm, network_options)
            .await;

    let controller = router_realm
        .connect_to_protocol::<fnet_multicast_admin::Ipv4RoutingTableControllerMarker>()
        .expect("connect to protocol");

    let mut addresses = fnet_multicast_admin::Ipv4UnicastSourceAndMulticastDestination {
        unicast_source: test_network.get_device_address(route_source_address),
        multicast_destination: IPV4_MULTICAST_ADDR,
    };

    let outgoing_interfaces = clients
        .iter()
        .map(|(client, config)| fnet_multicast_admin::OutgoingInterfaces {
            id: test_network.get_client(*client).device.router_interface.id(),
            min_ttl: config.route_min_ttl,
        })
        .collect();

    let route = fnet_multicast_admin::Route {
        expected_input_interface: Some(
            test_network.get_server(route_input_interface).router_interface.id(),
        ),
        action: Some(fnet_multicast_admin::Action::OutgoingInterfaces(outgoing_interfaces)),
        ..fnet_multicast_admin::Route::EMPTY
    };

    controller
        .add_route(&mut addresses, route)
        .await
        .expect("add_route failed")
        .expect("add_route error");

    if drop_multicast_controller {
        drop(controller);
    }

    let expectations = clients
        .into_iter()
        .map(|(client, config)| (client, config.expect_forwarded_packet))
        .collect();

    test_network.send_and_receive_multicast_packet(expectations).await;
}

/// An interface owned by the router.
enum RouterInterface {
    /// A router interface connected to a particular `Client`.
    Client(Client),
    /// A router interface connected to a particular 'Server'.
    Server(Server),
    /// A router interface that does not correspond to a `Client` or a `Server`.
    Other(u64),
}

/// Configuration for a `fnet_multicast_admin::Action`.
enum RouteAction {
    /// A `fnet_multicast_admin::Action::OutgoingInterfaces` action with an
    /// empty vector of outgoing interfaces.
    EmptyOutgoingInterfaces,
    /// A `fnet_multicast_admin::Action::OutgoingInterfaces` action with a
    /// single outgoing interface that corresponds to the `RouterInterface`.
    OutgoingInterface(RouterInterface),
}

/// Defaultable options for configuring a multicast route in an add multicast
/// route test.
#[derive(Derivative)]
#[derivative(Default)]
struct AddMulticastRouteTestOptions {
    #[derivative(Default(value = "Some(RouterInterface::Server(Server::A))"))]
    input_interface: Option<RouterInterface>,
    #[derivative(Default(
        value = "Some(RouteAction::OutgoingInterface(RouterInterface::Client(Client::A)))"
    ))]
    action: Option<RouteAction>,
    #[derivative(Default(value = "DeviceAddress::Server(Server::A)"))]
    source_address: DeviceAddress,
    #[derivative(Default(value = "IPV4_MULTICAST_ADDR"))]
    destination_address: fnet::Ipv4Address,
}

#[variants_test]
#[test_case(
    "success",
    ClientConfig {
        route_min_ttl: 1,
        expect_forwarded_packet: true,
    },
    Ok(()),
    AddMulticastRouteTestOptions::default();
    "success"
)]
#[test_case(
    "unexpected_input_interface",
    ClientConfig {
        route_min_ttl: 1,
        // A route is successfully added, but its input interface does not
        // match the interface that the packet arrived on. As a result, the
        // pending packet should not be forwarded.
        expect_forwarded_packet: false,
    },
    Ok(()),
    AddMulticastRouteTestOptions {
        input_interface: Some(RouterInterface::Server(Server::B)),
        ..AddMulticastRouteTestOptions::default()
    };
    "unexpected input interface"
)]
#[test_case(
    "unknown_input_interface",
    ClientConfig {
        route_min_ttl: 1,
        expect_forwarded_packet: false,
    },
    Err(fnet_multicast_admin::Ipv4RoutingTableControllerAddRouteError::InterfaceNotFound),
    AddMulticastRouteTestOptions {
        input_interface: Some(RouterInterface::Other(1000)),
        ..AddMulticastRouteTestOptions::default()
    };
    "unknown input interface"
)]
#[test_case(
    "unknown_output_interface",
    ClientConfig {
        route_min_ttl: 1,
        expect_forwarded_packet: false,
    },
    Err(fnet_multicast_admin::Ipv4RoutingTableControllerAddRouteError::InterfaceNotFound),
    AddMulticastRouteTestOptions {
        action: Some(RouteAction::OutgoingInterface(RouterInterface::Other(1000))),
        ..AddMulticastRouteTestOptions::default()
    };
    "unknown output interface"
)]
#[test_case(
    "input_interface_matches_output_interface",
    ClientConfig {
        route_min_ttl: 1,
        expect_forwarded_packet: false,
    },
    Err(fnet_multicast_admin::Ipv4RoutingTableControllerAddRouteError::InputCannotBeOutput),
    AddMulticastRouteTestOptions {
        input_interface: Some(RouterInterface::Server(Server::A)),
        action: Some(RouteAction::OutgoingInterface(RouterInterface::Server(Server::A))),
        ..AddMulticastRouteTestOptions::default()
    };
    "input interface matches output interface"
)]
#[test_case(
    "no_outgoing_interfaces",
    ClientConfig {
        route_min_ttl: 1,
        expect_forwarded_packet: false,
    },
    Err(fnet_multicast_admin::Ipv4RoutingTableControllerAddRouteError::RequiredRouteFieldsMissing),
    AddMulticastRouteTestOptions {
        action: Some(RouteAction::EmptyOutgoingInterfaces),
        ..AddMulticastRouteTestOptions::default()
    };
    "no outgoing interfaces"
)]
#[test_case(
    "expected_input_interface_not_specified",
    ClientConfig {
        route_min_ttl: 1,
        expect_forwarded_packet: false,
    },
    Err(fnet_multicast_admin::Ipv4RoutingTableControllerAddRouteError::RequiredRouteFieldsMissing),
    AddMulticastRouteTestOptions {
        input_interface: None,
        ..AddMulticastRouteTestOptions::default()
    };
    "expected input interface not specified"
)]
#[test_case(
    "action_not_specified",
    ClientConfig {
        route_min_ttl: 1,
        expect_forwarded_packet: false,
    },
    Err(fnet_multicast_admin::Ipv4RoutingTableControllerAddRouteError::RequiredRouteFieldsMissing),
    AddMulticastRouteTestOptions {
        action: None,
        ..AddMulticastRouteTestOptions::default()
    };
    "action not specified"
)]
#[test_case(
    "multicast_source_address",
    ClientConfig {
        route_min_ttl: 1,
        expect_forwarded_packet: false,
    },
    Err(fnet_multicast_admin::Ipv4RoutingTableControllerAddRouteError::InvalidAddress),
    AddMulticastRouteTestOptions {
        source_address: DeviceAddress::Other(IPV4_MULTICAST_ADDR),
        ..AddMulticastRouteTestOptions::default()
    };
    "multicast source address"
)]
#[test_case(
    "any_source_address",
    ClientConfig {
        route_min_ttl: 1,
        expect_forwarded_packet: false,
    },
    Err(fnet_multicast_admin::Ipv4RoutingTableControllerAddRouteError::InvalidAddress),
    AddMulticastRouteTestOptions {
        source_address: DeviceAddress::Other(IPV4_ANY_ADDR),
        ..AddMulticastRouteTestOptions::default()
    };
    "any source address"
)]
#[test_case(
    "link_local_unicast_source_address",
    ClientConfig {
        route_min_ttl: 1,
        expect_forwarded_packet: false,
    },
    Err(fnet_multicast_admin::Ipv4RoutingTableControllerAddRouteError::InvalidAddress),
    AddMulticastRouteTestOptions {
        source_address: DeviceAddress::Other(IPV4_LINK_LOCAL_UNICAST_ADDR),
        ..AddMulticastRouteTestOptions::default()
    };
    "link-local unicast source address"
)]
#[test_case(
    "unicast_destination_address",
    ClientConfig {
        route_min_ttl: 1,
        expect_forwarded_packet: false,
    },
    Err(fnet_multicast_admin::Ipv4RoutingTableControllerAddRouteError::InvalidAddress),
    AddMulticastRouteTestOptions {
        destination_address: IPV4_UNICAST_ADDR,
        ..AddMulticastRouteTestOptions::default()
    };
    "unicast destination address"
)]
#[test_case(
    "link_local_multicast_destination_address",
    ClientConfig {
        route_min_ttl: 1,
        expect_forwarded_packet: false,
    },
    Err(fnet_multicast_admin::Ipv4RoutingTableControllerAddRouteError::InvalidAddress),
    AddMulticastRouteTestOptions {
        destination_address: IPV4_LINK_LOCAL_MULTICAST_ADDR,
        ..AddMulticastRouteTestOptions::default()
    };
    "link-local multicast destination address"
)]
async fn add_multicast_route<E: netemul::Endpoint>(
    name: &str,
    case_name: &str,
    client: ClientConfig,
    expected_add_route_result: Result<
        (),
        fnet_multicast_admin::Ipv4RoutingTableControllerAddRouteError,
    >,
    options: AddMulticastRouteTestOptions,
) {
    let AddMulticastRouteTestOptions {
        input_interface,
        action,
        source_address,
        destination_address,
    } = options;

    let test_name = format!("{}_{}", name, case_name);
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let router_realm = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_router_realm", &test_name))
        .expect("create realm");
    let test_network = MulticastForwardingNetwork::new::<E>(
        &test_name,
        &sandbox,
        &router_realm,
        MulticastForwardingNetworkOptions::default(),
    )
    .await;

    let controller = router_realm
        .connect_to_protocol::<fnet_multicast_admin::Ipv4RoutingTableControllerMarker>()
        .expect("connect to protocol");

    // Queue a packet that could potentially be forwarded once a multicast route
    // is installed.
    test_network.send_multicast_packet().await;

    let mut addresses = fnet_multicast_admin::Ipv4UnicastSourceAndMulticastDestination {
        unicast_source: test_network.get_device_address(source_address),
        multicast_destination: destination_address,
    };

    let get_interface_id = |interface| -> u64 {
        match interface {
            RouterInterface::Server(server) => {
                test_network.get_server(server).router_interface.id()
            }
            RouterInterface::Client(client) => {
                test_network.get_client(client).device.router_interface.id()
            }
            RouterInterface::Other(id) => id,
        }
    };

    let route_action = action.and_then(|action| {
        let outgoing_interfaces = match action {
            RouteAction::EmptyOutgoingInterfaces => vec![],
            RouteAction::OutgoingInterface(interface) => {
                vec![fnet_multicast_admin::OutgoingInterfaces {
                    id: get_interface_id(interface),
                    min_ttl: client.route_min_ttl,
                }]
            }
        };
        Some(fnet_multicast_admin::Action::OutgoingInterfaces(outgoing_interfaces))
    });
    let route = fnet_multicast_admin::Route {
        expected_input_interface: input_interface
            .and_then(|interface| Some(get_interface_id(interface))),
        action: route_action,
        ..fnet_multicast_admin::Route::EMPTY
    };

    assert_eq!(
        controller.add_route(&mut addresses, route).await.expect("add_route failed"),
        expected_add_route_result
    );

    test_network
        .receive_multicast_packet(hashmap! {
            Client::A => client.expect_forwarded_packet,
        })
        .await;
}

#[variants_test]
async fn multiple_multicast_controllers<E: netemul::Endpoint>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let router_realm = sandbox
        .create_netstack_realm::<Netstack2, _>(format!("{}_router_realm", name))
        .expect("create realm");
    let test_network = MulticastForwardingNetwork::new::<E>(
        name,
        &sandbox,
        &router_realm,
        MulticastForwardingNetworkOptions::default(),
    )
    .await;

    let controller = router_realm
        .connect_to_protocol::<fnet_multicast_admin::Ipv4RoutingTableControllerMarker>()
        .expect("connect to protocol");

    let mut addresses = test_network.default_unicast_source_and_multicast_destination();
    let route = test_network.default_multicast_route();

    controller
        .add_route(&mut addresses, route)
        .await
        .expect("add_route failed")
        .expect("add_route error");

    let closed_controller = router_realm
        .connect_to_protocol::<fnet_multicast_admin::Ipv4RoutingTableControllerMarker>()
        .expect("connect to protocol");

    expect_table_controller_closed_with_reason(
        &closed_controller,
        fnet_multicast_admin::TableControllerCloseReason::AlreadyInUse,
    )
    .await;

    // The closed controller should not impact the already active controller.
    // Consequently, a packet should still be forwardable using the route added
    // above.
    test_network
        .send_and_receive_multicast_packet(hashmap! {
            Client::A => true,
        })
        .await;
}
