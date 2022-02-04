// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error, Result};
use async_utils::stream::FlattenUnorderedExt as _;
use fidl::endpoints::Proxy as _;
use fidl_fuchsia_component as fcomponent;
use fidl_fuchsia_component_decl as fdecl;
use fidl_fuchsia_hardware_ethernet as fethernet;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_debug as fnet_debug;
use fidl_fuchsia_net_ext as fnet_ext;
use fidl_fuchsia_net_interfaces as fnet_interfaces;
use fidl_fuchsia_net_interfaces_ext as fnet_interfaces_ext;
use fidl_fuchsia_net_test_realm as fntr;
use fidl_fuchsia_netstack as fnetstack;
use fidl_fuchsia_posix_socket as fposix_socket;
use fuchsia_async::{self as fasync, futures::StreamExt as _, TimeoutExt as _};
use fuchsia_zircon as zx;
use futures::{FutureExt as _, SinkExt as _, TryFutureExt as _, TryStreamExt as _};
use log::{error, warn};
use std::collections::HashMap;
use std::convert::TryFrom as _;
use std::path;

/// URL for the realm that contains the hermetic network components with a
/// Netstack2 instance.
const HERMETIC_NETWORK_V2_URL: &'static str = "#meta/hermetic_network_v2.cm";

/// Values for creating an interface on the hermetic Netstack.
///
/// Note that the topological path and the file path are not used by the
/// underlying Netstack. Consequently, fake values are defined here. Similarly,
/// the metric only needs to be a sensible value.
const DEFAULT_METRIC: u32 = 100;
const DEFAULT_INTERFACE_TOPOLOGICAL_PATH: &'static str = "/dev/fake/topological/path";
const DEFAULT_INTERFACE_FILE_PATH: &'static str = "/dev/fake/file/path";

/// Returns a `fidl::endpoints::ClientEnd` for the device that matches
/// `mac_address`.
///
/// If a matching device is not found, then an error is returned.
async fn find_device_client_end(
    expected_mac_address: fnet_ext::MacAddress,
) -> Result<fidl::endpoints::ClientEnd<fethernet::DeviceMarker>, fntr::Error> {
    // TODO(https://fxbug.dev/89648): Replace this function with a call to
    // fuchsia.net.debug. As an intermediate solution, the
    // `fidl::endpoints::ClientEnd` is obtained by enumerating the ethernet
    // devices in devfs.
    const ETHERNET_DIRECTORY_PATH: &'static str = "/dev/class/ethernet";

    let (directory_proxy, directory_server_end) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().map_err(|e| {
            error!("failed to create directory endpoint: {:?}", e);
            fntr::Error::Internal
        })?;
    fdio::service_connect(ETHERNET_DIRECTORY_PATH, directory_server_end.into_channel().into())
        .map_err(|e| {
            error!(
                "failed to connect to directory service at {} with error: {:?}",
                ETHERNET_DIRECTORY_PATH, e
            );
            fntr::Error::Internal
        })?;
    let files = files_async::readdir(&directory_proxy).await.map_err(|e| {
        error!("failed to read files in {} with error {:?}", ETHERNET_DIRECTORY_PATH, e);
        fntr::Error::Internal
    })?;

    for file in files {
        let filepath = path::Path::new(ETHERNET_DIRECTORY_PATH).join(&file.name);
        let filepath = filepath.to_str().ok_or_else(|| {
            error!("failed to convert file path to string");
            fntr::Error::Internal
        })?;
        let (device_proxy, device_server_end) =
            fidl::endpoints::create_proxy::<fethernet::DeviceMarker>().map_err(|e| {
                error!("failed to create device endpoint: {:?}", e);
                fntr::Error::Internal
            })?;
        fdio::service_connect(filepath, device_server_end.into_channel().into()).map_err(|e| {
            error!("failed to connect to device service: {:?}", e);
            fntr::Error::Internal
        })?;

        let info = device_proxy.get_info().await.map_err(|e| {
            error!("failed to read ethernet device info: {:?}", e);
            fntr::Error::Internal
        })?;

        if info.mac.octets == expected_mac_address.octets {
            let channel = fidl::endpoints::ClientEnd::<fethernet::DeviceMarker>::new(
                device_proxy
                    .into_channel()
                    .map_err(|e| {
                        error!("failed to convert device proxy to channel: {:?}", e);
                        fntr::Error::Internal
                    })?
                    .into_zx_channel(),
            );
            return Ok(channel);
        }
    }

    warn!(
        "failed to find interface with MAC address: {} in {}",
        expected_mac_address, ETHERNET_DIRECTORY_PATH
    );
    Err(fntr::Error::InterfaceNotFound)
}

/// Returns the id for the enabled interface that matches `mac_address`.
///
/// If an interface matching `mac_address` is not found, then an error is
/// returned. If a matching interface is found, but it is not enabled, then
/// `Option::None` is returned.
async fn find_enabled_interface_id(
    expected_mac_address: fnet_ext::MacAddress,
) -> Result<Option<u64>, fntr::Error> {
    let state_proxy = SystemConnector.connect_to_protocol::<fnet_interfaces::StateMarker>()?;
    let stream = fnet_interfaces_ext::event_stream_from_state(&state_proxy).map_err(|e| {
        error!("failed to read interface stream: {:?}", e);
        fntr::Error::Internal
    })?;

    let interfaces = fnet_interfaces_ext::existing(stream, HashMap::new()).await.map_err(|e| {
        error!("failed to read existing interfaces: {:?}", e);
        fntr::Error::Internal
    })?;

    let debug_interfaces_proxy =
        SystemConnector.connect_to_protocol::<fnet_debug::InterfacesMarker>()?;

    let interfaces_stream = futures::stream::iter(interfaces.into_values());

    let results =
        interfaces_stream.filter_map(|fnet_interfaces_ext::Properties { id, online, .. }| {
            let debug_interfaces_proxy = &debug_interfaces_proxy;
            async move {
                match debug_interfaces_proxy.get_mac(id).await {
                    Err(e) => {
                        let _: fidl::Error = e;
                        warn!("get_mac failure: {:?}", e);
                        None
                    }
                    Ok(result) => match result {
                        Err(fnet_debug::InterfacesGetMacError::NotFound) => {
                            warn!("get_mac interface not found for ID: {}", id);
                            None
                        }
                        Ok(mac_address) => mac_address.and_then(|mac_address| {
                            (mac_address.octets == expected_mac_address.octets)
                                .then(move || (id, online))
                        }),
                    },
                }
            }
        });

    futures::pin_mut!(results);

    results.next().await.ok_or(fntr::Error::InterfaceNotFound).map(|(id, online)| {
        if online {
            Some(id)
        } else {
            None
        }
    })
}

/// Returns a `fnet_interfaces_admin::ControlProxy` that can be used to
/// manipulate the interface that has the provided `id`.
async fn connect_to_interface_admin_control(
    id: u64,
    connector: &impl Connector,
) -> Result<fnet_interfaces_ext::admin::Control, fntr::Error> {
    let debug_interfaces_proxy = connector.connect_to_protocol::<fnet_debug::InterfacesMarker>()?;
    let (control, server) =
        fnet_interfaces_ext::admin::Control::create_endpoints().map_err(|e| {
            error!("create_proxy failure: {:?}", e);
            fntr::Error::Internal
        })?;
    debug_interfaces_proxy.get_admin(id, server).map_err(|e| {
        error!("get_admin failure: {:?}", e);
        fntr::Error::Internal
    })?;
    Ok(control)
}

/// Enables the interface with `id` using the provided `debug_interfaces_proxy`.
async fn enable_interface(id: u64, connector: &impl Connector) -> Result<(), fntr::Error> {
    let control_proxy = connect_to_interface_admin_control(id, connector).await?;
    let _did_enable: bool = control_proxy
        .enable()
        .await
        .map_err(|e| {
            error!("enable interface id: {} failure: {:?}", id, e);
            fntr::Error::Internal
        })?
        .map_err(|e| {
            error!("enable interface id: {} error: {:?}", id, e);
            fntr::Error::Internal
        })?;
    Ok(())
}

/// Disables the interface with `id` using the provided
/// `debug_interfaces_proxy`.
async fn disable_interface(id: u64, connector: &impl Connector) -> Result<(), fntr::Error> {
    let control_proxy = connect_to_interface_admin_control(id, connector).await?;
    let _did_disable: bool = control_proxy
        .disable()
        .await
        .map_err(|e| {
            error!("disable interface id: {} failure: {:?}", id, e);
            fntr::Error::Internal
        })?
        .map_err(|e| {
            error!("disable interface id: {} error: {:?}", id, e);
            fntr::Error::Internal
        })?;
    Ok(())
}

fn create_child_decl(child_name: &str, url: &str) -> fdecl::Child {
    fdecl::Child {
        name: Some(child_name.to_string()),
        url: Some(url.to_string()),
        // TODO(https://fxbug.dev/90085): Remove the startup field when the
        // child is being created in a single_run collection. In such a case,
        // this field is currently required to be set to
        // `fdecl::StartupMode::Lazy` even though it is a no-op.
        startup: Some(fdecl::StartupMode::Lazy),
        ..fdecl::Child::EMPTY
    }
}

/// Creates a child component named `child_name` within the provided
/// `collection_name`.
///
/// The `url` corresponds to the URL of the component to add. The `connector`
/// connects to the desired realm.
async fn create_child(
    mut collection_ref: fdecl::CollectionRef,
    child: fdecl::Child,
    connector: &impl Connector,
) -> Result<(), fntr::Error> {
    let realm_proxy = connector.connect_to_protocol::<fcomponent::RealmMarker>()?;

    realm_proxy
        .create_child(&mut collection_ref, child, fcomponent::CreateChildArgs::EMPTY)
        .await
        .map_err(|e| {
            error!("create_child failed: {:?}", e);
            fntr::Error::Internal
        })?
        .map_err(|e| {
            match e {
                // Variants that may be returned by the `CreateChild` method.
                fcomponent::Error::InstanceCannotResolve => fntr::Error::ComponentNotFound,
                fcomponent::Error::InvalidArguments => fntr::Error::InvalidArguments,
                fcomponent::Error::CollectionNotFound
                | fcomponent::Error::InstanceAlreadyExists
                | fcomponent::Error::InstanceDied
                | fcomponent::Error::ResourceUnavailable
                // Variants that are not returned by the `CreateChild` method.
                | fcomponent::Error::AccessDenied
                | fcomponent::Error::InstanceCannotStart
                | fcomponent::Error::InstanceNotFound
                | fcomponent::Error::Internal
                | fcomponent::Error::ResourceNotFound
                | fcomponent::Error::Unsupported => {
                    error!("create_child error: {:?}", e);
                    fntr::Error::Internal
                }
            }
        })
}

#[derive(thiserror::Error, Debug)]
enum DestroyChildError {
    #[error("Internal error")]
    Internal,
    #[error("Component not running")]
    NotRunning,
}

/// Destroys the child component that corresponds to `child_ref`.
///
/// The `connector` connects to the desired realm. A `not_running_error` will be
/// returned if the provided `child_ref` does not exist.
async fn destroy_child(
    mut child_ref: fdecl::ChildRef,
    connector: &impl Connector,
) -> Result<(), DestroyChildError> {
    let realm_proxy = connector
        .connect_to_protocol::<fcomponent::RealmMarker>()
        .map_err(|_e| DestroyChildError::Internal)?;

    realm_proxy
        .destroy_child(&mut child_ref)
        .await
        .map_err(|e| {
            error!("destroy_child failed: {:?}", e);
            DestroyChildError::Internal
        })?
        .map_err(|e| {
            match e {
            // Variants that may be returned by the `DestroyChild`
            // method. `CollectionNotFound` and `InstanceNotFound`
            // mean that the hermetic network realm does not exist. All
            // other errors are propagated as internal errors.
            fcomponent::Error::CollectionNotFound
            | fcomponent::Error::InstanceNotFound =>
                DestroyChildError::NotRunning,
            fcomponent::Error::InstanceDied
            | fcomponent::Error::InvalidArguments
            // Variants that are not returned by the `DestroyChild`
            // method.
            | fcomponent::Error::AccessDenied
            | fcomponent::Error::InstanceAlreadyExists
            | fcomponent::Error::InstanceCannotResolve
            | fcomponent::Error::InstanceCannotStart
            | fcomponent::Error::Internal
            | fcomponent::Error::ResourceNotFound
            | fcomponent::Error::ResourceUnavailable
            | fcomponent::Error::Unsupported => {
                error!("destroy_child error: {:?}", e);
                DestroyChildError::Internal
            }
    }
        })
}

async fn has_stub(connector: &impl Connector) -> Result<bool, fntr::Error> {
    let realm_proxy = connector.connect_to_protocol::<fcomponent::RealmMarker>()?;
    network_test_realm::has_stub(&realm_proxy).await.map_err(|e| {
        error!("failed to check for hermetic network realm: {:?}", e);
        fntr::Error::Internal
    })
}

/// A type that can connect to a FIDL protocol within a particular realm.
trait Connector {
    fn connect_to_protocol<P: fidl::endpoints::DiscoverableProtocolMarker>(
        &self,
    ) -> Result<P::Proxy, fntr::Error>;
}

/// Connects to protocols that are exposed to the Network Test Realm.
struct SystemConnector;

impl Connector for SystemConnector {
    fn connect_to_protocol<P: fidl::endpoints::DiscoverableProtocolMarker>(
        &self,
    ) -> Result<P::Proxy, fntr::Error> {
        fuchsia_component::client::connect_to_protocol::<P>().map_err(|e| {
            error!("failed to connect to {} with error: {:?}", P::NAME, e);
            fntr::Error::Internal
        })
    }
}

/// Connects to protocols within the hermetic-network realm.
struct HermeticNetworkConnector {
    child_directory: fio::DirectoryProxy,
}

impl HermeticNetworkConnector {
    async fn new() -> Result<Self, fntr::Error> {
        Ok(Self {
            child_directory: fuchsia_component::client::open_childs_exposed_directory(
                network_test_realm::HERMETIC_NETWORK_REALM_NAME.to_string(),
                Some(network_test_realm::HERMETIC_NETWORK_COLLECTION_NAME.to_string()),
            )
            .await
            .map_err(|e| {
                error!("open_childs_exposed_directory failed: {:?}", e);
                fntr::Error::Internal
            })?,
        })
    }
}

impl Connector for HermeticNetworkConnector {
    fn connect_to_protocol<P: fidl::endpoints::DiscoverableProtocolMarker>(
        &self,
    ) -> Result<P::Proxy, fntr::Error> {
        fuchsia_component::client::connect_to_protocol_at_dir_root::<P>(&self.child_directory)
            .map_err(|e| {
                error!("failed to connect to {} with error: {:?}", P::NAME, e);
                fntr::Error::Internal
            })
    }
}

async fn create_socket(
    domain: fposix_socket::Domain,
    protocol: fposix_socket::DatagramSocketProtocol,
    connector: &HermeticNetworkConnector,
) -> Result<socket2::Socket, fntr::Error> {
    let socket_provider = connector.connect_to_protocol::<fposix_socket::ProviderMarker>()?;
    let sock = socket_provider
        .datagram_socket(domain, protocol)
        .await
        .map_err(|e| {
            error!("datagram_socket failed: {:?}", e);
            fntr::Error::Internal
        })?
        .map_err(|e| {
            error!("datagram_socket error: {:?}", e);
            fntr::Error::Internal
        })?;

    fdio::create_fd(sock.into()).map_err(|e| {
        error!("create_fd from socket failed: {:?}", e);
        fntr::Error::Internal
    })
}

async fn create_icmp_socket(
    domain: fposix_socket::Domain,
    connector: &HermeticNetworkConnector,
) -> Result<fasync::net::DatagramSocket, fntr::Error> {
    Ok(fasync::net::DatagramSocket::new_from_socket(
        create_socket(domain, fposix_socket::DatagramSocketProtocol::IcmpEcho, connector).await?,
    )
    .map_err(|e| {
        error!("new_from_socket failed: {:?}", e);
        fntr::Error::Internal
    })?)
}

/// Returns the scope ID needed for the provided `address`.
///
/// See https://tools.ietf.org/html/rfc2553#section-3.3 for more information.
async fn get_interface_scope_id(
    interface_name: &Option<String>,
    address: &std::net::Ipv6Addr,
    connector: &impl Connector,
) -> Result<u32, fntr::Error> {
    const DEFAULT_SCOPE_ID: u32 = 0;
    let is_link_local_address =
        net_types::ip::Ipv6Addr::from_bytes(address.octets()).is_unicast_link_local();

    match (interface_name, is_link_local_address) {
        // If a link-local address is specified, then an interface name
        // must be provided.
        (None, true) => Err(fntr::Error::InvalidArguments),
        // The default scope ID should be used for any non link-local
        // address.
        (Some(_), false) | (None, false) => Ok(DEFAULT_SCOPE_ID),
        (Some(interface_name), true) => network_test_realm::get_interface_id(
            &interface_name,
            &connector.connect_to_protocol::<fnet_interfaces::StateMarker>()?,
        )
        .await
        .map_err(|e| {
            error!(
                "failed to obtain interface ID for interface named: {} with error: {:?}",
                interface_name, e
            );
            fntr::Error::Internal
        })?
        .ok_or(fntr::Error::InterfaceNotFound)
        .and_then(|id| {
            u32::try_from(id).map_err(|e| {
                error!("failed to convert interface ID to u32, {:?}", e);
                fntr::Error::Internal
            })
        }),
    }
}

/// Sends a single ICMP echo request to `address`.
async fn ping_once<Ip: ping::IpExt>(
    address: Ip::Addr,
    payload_length: usize,
    interface_name: Option<String>,
    timeout: zx::Duration,
    connector: &HermeticNetworkConnector,
) -> Result<(), fntr::Error> {
    let socket = create_icmp_socket(Ip::DOMAIN_FIDL, connector).await?;

    if let Some(interface_name) = interface_name {
        socket.bind_device(Some(interface_name.as_bytes())).map_err(|e| match e.kind() {
            std::io::ErrorKind::InvalidInput => fntr::Error::InterfaceNotFound,
            _kind => {
                error!("bind_device for interface: {} failed: {:?}", interface_name, e);
                fntr::Error::Internal
            }
        })?;
    }

    // The body of the packet is filled with `payload_length` 0 bytes.
    let payload: Vec<u8> = std::iter::repeat(0).take(payload_length).collect();

    let (mut sink, mut stream) = ping::new_unicast_sink_and_stream::<Ip, _, { u16::MAX as usize }>(
        &socket, &address, &payload,
    );

    const SEQ: u16 = 1;
    sink.send(SEQ).await.map_err(|e| {
        warn!("failed to send ping: {:?}", e);
        match e {
            ping::PingError::Send(error) => match error.kind() {
                // `InvalidInput` corresponds to an oversized `payload_length`.
                std::io::ErrorKind::InvalidInput => fntr::Error::InvalidArguments,
                // TODO(https://github.com/rust-lang/rust/issues/86442): Consider
                // defining more granular error codes once the relevant
                // `std::io::Error` variants are stable (e.g. `HostUnreachable`,
                // `NetworkUnreachable`, etc.).
                _kind => fntr::Error::PingFailed,
            },
            ping::PingError::Body { .. }
            | ping::PingError::Parse
            | ping::PingError::Recv(_)
            | ping::PingError::ReplyCode(_)
            | ping::PingError::ReplyType { .. }
            | ping::PingError::SendLength { .. } => fntr::Error::PingFailed,
        }
    })?;

    if timeout.into_nanos() <= 0 {
        return Ok(());
    }

    match stream.try_next().map(Some).on_timeout(timeout, || None).await {
        None => Err(fntr::Error::TimeoutExceeded),
        Some(Err(e)) => {
            warn!("failed to receive ping response: {:?}", e);
            Err(fntr::Error::PingFailed)
        }
        Some(Ok(None)) => {
            error!("ping reply stream ended unexpectedly");
            Err(fntr::Error::Internal)
        }
        Some(Ok(Some(got))) if got == SEQ => Ok(()),
        Some(Ok(Some(got))) => {
            error!("received unexpected ping sequence number; got: {}, want: {}", got, SEQ);
            Err(fntr::Error::PingFailed)
        }
    }
}

/// Creates a socket if `socket` is None. Otherwise, returns a reference to the
/// `socket` value.
///
/// If a socket is created, then the value of `socket` is replaced with the
/// newly created socket.
async fn get_or_insert_socket<'a>(
    socket: &'a mut Option<socket2::Socket>,
    domain: fposix_socket::Domain,
    connector: &'a HermeticNetworkConnector,
) -> Result<&'a socket2::Socket, fntr::Error> {
    match socket {
        None => Ok(socket.insert(
            create_socket(domain, fposix_socket::DatagramSocketProtocol::Udp, connector).await?,
        )),
        Some(value) => Ok(value),
    }
}

/// A controller for creating and manipulating the Network Test Realm.
///
/// The Network Test Realm corresponds to a hermetic network realm with a
/// Netstack under test. The `Controller` is responsible for configuring
/// this realm, the Netstack under test, and the system's Netstack. Once
/// configured, the controller is expected to yield the following component
/// topology:
///
/// ```
///            network-test-realm (this controller component)
///                    |
///             enclosed-network (a collection)
///                    |
///             hermetic-network
///            /       |        \
///      netstack     ...      stubs (a collection)
///                              |
///                          test-stub (a configurable component)
/// ```
///
/// This topology enables the `Controller` to interact with the system's
/// Netstack and the Netstack in the "hermetic-network" realm. Additionally, it
/// enables capabilities to be routed from the hermetic Netstack to siblings
/// (via the "hermetic-network" parent) while remaining isolated from the rest
/// of the system.
struct Controller {
    /// Interface IDs that have been mutated on the system's Netstack.
    mutated_interface_ids: Vec<u64>,

    /// Connector to access protocols within the hermetic-network realm. If the
    /// hermetic-network realm does not exist, then this will be `None`.
    hermetic_network_connector: Option<HermeticNetworkConnector>,

    /// Socket for joining and leaving Ipv4 multicast groups. This field is
    /// lazily instantiated the first time an Ipv4 multicast group is joined or
    /// left. Note that the lifetime of Ipv4 multicast memberships (those added
    /// via the `join_multicast_group` method) are tied to this field.
    multicast_v4_socket: Option<socket2::Socket>,

    /// Socket for joining and leaving Ipv6 multicast groups. This field is
    /// lazily instantiated the first time an Ipv6 multicast group is joined or
    /// left. Note that the lifetime of Ipv6 multicast memberships (those added
    /// via the `join_multicast_group` method) are tied to this field.
    multicast_v6_socket: Option<socket2::Socket>,
}

impl Controller {
    fn new() -> Self {
        Self {
            mutated_interface_ids: Vec::<u64>::new(),
            hermetic_network_connector: None,
            multicast_v4_socket: None,
            multicast_v6_socket: None,
        }
    }

    async fn handle_request(
        &mut self,
        request: fntr::ControllerRequest,
    ) -> Result<(), fidl::Error> {
        match request {
            fntr::ControllerRequest::StartHermeticNetworkRealm { netstack, responder } => {
                let mut result = self.start_hermetic_network_realm(netstack).await;
                responder.send(&mut result)?;
            }
            fntr::ControllerRequest::StopHermeticNetworkRealm { responder } => {
                let mut result = self.stop_hermetic_network_realm().await;
                responder.send(&mut result)?;
            }
            fntr::ControllerRequest::AddInterface { mac_address, name, responder } => {
                let mac_address = fnet_ext::MacAddress::from(mac_address);
                let mut result = self.add_interface(mac_address, &name).await;
                responder.send(&mut result)?;
            }
            fntr::ControllerRequest::StartStub { component_url, responder } => {
                let mut result = self.start_stub(&component_url).await;
                responder.send(&mut result)?;
            }
            fntr::ControllerRequest::StopStub { responder } => {
                let mut result = self.stop_stub().await;
                responder.send(&mut result)?;
            }
            fntr::ControllerRequest::Ping {
                target,
                payload_length,
                interface_name,
                timeout,
                responder,
            } => {
                let mut result = self
                    .ping(target, payload_length, interface_name, zx::Duration::from_nanos(timeout))
                    .await;
                responder.send(&mut result)?;
            }
            fntr::ControllerRequest::JoinMulticastGroup { address, interface_id, responder } => {
                let mut result = self.join_multicast_group(address, interface_id).await;
                responder.send(&mut result)?;
            }
            fntr::ControllerRequest::LeaveMulticastGroup { address, interface_id, responder } => {
                let mut result = self.leave_multicast_group(address, interface_id).await;
                responder.send(&mut result)?;
            }
        }
        Ok(())
    }

    /// Returns the `socket2::Socket` that should be used join or leave
    /// multicast groups for `address`.
    ///
    /// Creates a socket on the first invocation for the relevant IP version.
    /// Subsequent invocations return the existing socket.
    async fn get_or_create_multicast_socket(
        &mut self,
        address: std::net::IpAddr,
    ) -> Result<&socket2::Socket, fntr::Error> {
        let hermetic_network_connector = self
            .hermetic_network_connector
            .as_ref()
            .ok_or(fntr::Error::HermeticNetworkRealmNotRunning)?;

        match address {
            std::net::IpAddr::V4(_) => {
                get_or_insert_socket(
                    &mut self.multicast_v4_socket,
                    fposix_socket::Domain::Ipv4,
                    hermetic_network_connector,
                )
                .await
            }
            std::net::IpAddr::V6(_) => {
                get_or_insert_socket(
                    &mut self.multicast_v6_socket,
                    fposix_socket::Domain::Ipv6,
                    hermetic_network_connector,
                )
                .await
            }
        }
    }

    /// Leaves the multicast group `address` using the provided `interface_id`.
    async fn leave_multicast_group(
        &mut self,
        address: fnet::IpAddress,
        interface_id: u64,
    ) -> Result<(), fntr::Error> {
        let fnet_ext::IpAddress(address) = address.into();
        let interface_id = u32::try_from(interface_id).map_err(|e| {
            error!("failed to convert interface ID to u32, {:?}", e);
            fntr::Error::Internal
        })?;
        let socket = self.get_or_create_multicast_socket(address).await?;

        match address {
            std::net::IpAddr::V4(addr) => socket.leave_multicast_v4_n(
                &addr,
                &socket2::InterfaceIndexOrAddress::Index(interface_id),
            ),
            std::net::IpAddr::V6(addr) => socket.leave_multicast_v6(&addr, interface_id),
        }
        .map_err(|e| match e.kind() {
            // The group `address` was not previously joined.
            std::io::ErrorKind::AddrNotAvailable => fntr::Error::AddressNotAvailable,
            // The specified `interface_id` does not exist or the `address`
            // does not correspond to a valid multicast address.
            std::io::ErrorKind::InvalidInput => fntr::Error::InvalidArguments,
            _kind => {
                error!("leave_multicast_group failed: {:?}", e);
                fntr::Error::Internal
            }
        })
    }

    /// Joins the multicast group `address` using the provided `interface_id`.
    async fn join_multicast_group(
        &mut self,
        address: fnet::IpAddress,
        interface_id: u64,
    ) -> Result<(), fntr::Error> {
        let fnet_ext::IpAddress(address) = address.into();
        let interface_id = u32::try_from(interface_id).map_err(|e| {
            error!("failed to convert interface ID to u32, {:?}", e);
            fntr::Error::Internal
        })?;
        let socket = self.get_or_create_multicast_socket(address).await?;

        match address {
            std::net::IpAddr::V4(addr) => socket
                .join_multicast_v4_n(&addr, &socket2::InterfaceIndexOrAddress::Index(interface_id)),
            std::net::IpAddr::V6(addr) => socket.join_multicast_v6(&addr, interface_id),
        }
        .map_err(|e| match e.kind() {
            // The group `address` was already joined.
            std::io::ErrorKind::AddrInUse => fntr::Error::AddressInUse,
            // The specified `interface_id` does not exist or the `address`
            // does not correspond to a valid multicast address.
            std::io::ErrorKind::InvalidInput => fntr::Error::InvalidArguments,
            _kind => {
                error!("join_multicast_group failed: {:?}", e);
                fntr::Error::Internal
            }
        })
    }

    /// Starts a test stub within the hermetic-network realm.
    async fn start_stub(&self, component_url: &str) -> Result<(), fntr::Error> {
        // Stubs exist only within the hermetic-network realm. Therefore,
        // the hermetic-network realm must exist.
        let hermetic_network_connector = self
            .hermetic_network_connector
            .as_ref()
            .ok_or(fntr::Error::HermeticNetworkRealmNotRunning)?;

        if has_stub(hermetic_network_connector).await? {
            // The `Controller` only configures one test stub at a time. As a
            // result, any existing stub must be stopped before a new one is
            // started.
            self.stop_stub().await.map_err(|e| match e {
                fntr::Error::StubNotRunning => {
                    error!("attempted to stop stub that was not running");
                    fntr::Error::Internal
                }
                fntr::Error::AddressInUse
                | fntr::Error::AddressNotAvailable
                | fntr::Error::ComponentNotFound
                | fntr::Error::HermeticNetworkRealmNotRunning
                | fntr::Error::Internal
                | fntr::Error::InterfaceNotFound
                | fntr::Error::InvalidArguments
                | fntr::Error::PingFailed
                | fntr::Error::TimeoutExceeded => e,
            })?;
        }

        create_child(
            fdecl::CollectionRef { name: network_test_realm::STUB_COLLECTION_NAME.to_string() },
            create_child_decl(network_test_realm::STUB_COMPONENT_NAME, component_url),
            hermetic_network_connector,
        )
        .await
    }

    /// Stops the test stub within the hermetic-network realm.
    async fn stop_stub(&self) -> Result<(), fntr::Error> {
        // Stubs exist only within the hermetic-network realm. Therefore,
        // the hermetic-network realm must exist.
        let hermetic_network_connector = self
            .hermetic_network_connector
            .as_ref()
            .ok_or(fntr::Error::HermeticNetworkRealmNotRunning)?;

        destroy_child(network_test_realm::create_stub_child_ref(), hermetic_network_connector)
            .await
            .map_err(|e| match e {
                DestroyChildError::Internal => fntr::Error::Internal,
                DestroyChildError::NotRunning => fntr::Error::StubNotRunning,
            })
    }

    /// Pings the `target` using a socket created on the hermetic Netstack.
    async fn ping(
        &self,
        target: fnet::IpAddress,
        payload_length: u16,
        interface_name: Option<String>,
        timeout: zx::Duration,
    ) -> Result<(), fntr::Error> {
        let hermetic_network_connector = self
            .hermetic_network_connector
            .as_ref()
            .ok_or(fntr::Error::HermeticNetworkRealmNotRunning)?;

        let fnet_ext::IpAddress(target) = target.into();
        const UNSPECIFIED_PORT: u16 = 0;
        match target {
            std::net::IpAddr::V4(addr) => {
                ping_once::<ping::Ipv4>(
                    std::net::SocketAddrV4::new(addr, UNSPECIFIED_PORT),
                    payload_length.into(),
                    interface_name,
                    timeout,
                    hermetic_network_connector,
                )
                .await
            }
            std::net::IpAddr::V6(addr) => {
                const DEFAULT_FLOW_INFO: u32 = 0;
                let scope_id =
                    get_interface_scope_id(&interface_name, &addr, hermetic_network_connector)
                        .await?;
                ping_once::<ping::Ipv6>(
                    std::net::SocketAddrV6::new(
                        addr,
                        UNSPECIFIED_PORT,
                        DEFAULT_FLOW_INFO,
                        scope_id,
                    ),
                    payload_length.into(),
                    interface_name,
                    timeout,
                    hermetic_network_connector,
                )
                .await
            }
        }
    }

    /// Adds an interface to the hermetic Netstack.
    ///
    /// An interface will only be added if the system has an interface with a
    /// matching `mac_address`. The added interface will have the provided
    /// `name`. Additionally, the matching interface will be disabled on the
    /// system's Netstack.
    async fn add_interface(
        &mut self,
        mac_address: fnet_ext::MacAddress,
        name: &str,
    ) -> Result<(), fntr::Error> {
        // A hermetic Netstack must be running for an interface to be
        // added.
        let hermetic_network_connector = self
            .hermetic_network_connector
            .as_ref()
            .ok_or(fntr::Error::HermeticNetworkRealmNotRunning)?;

        let device_client_end = find_device_client_end(mac_address).await?;
        let interface_id_to_disable = find_enabled_interface_id(mac_address).await?;

        // TODO(https://fxbug.dev/89651): Support Netstack3. Currently, an
        // interface name cannot be specified when adding an interface via
        // fuchsia.net.stack.Stack. As a result, the Network Test Realm
        // currently does not support Netstack3.
        let id: u32 = hermetic_network_connector
            .connect_to_protocol::<fnetstack::NetstackMarker>()?
            .add_ethernet_device(
                DEFAULT_INTERFACE_TOPOLOGICAL_PATH,
                &mut fnetstack::InterfaceConfig {
                    name: name.to_string(),
                    filepath: DEFAULT_INTERFACE_FILE_PATH.to_string(),
                    metric: DEFAULT_METRIC,
                },
                device_client_end,
            )
            .await
            .map_err(|e| {
                error!("add_ethernet_device failed: {:?}", e);
                fntr::Error::Internal
            })?
            .map_err(|e| {
                error!("add_ethernet_device error: {:?}", e);
                fntr::Error::Internal
            })?;

        // Enable the interface that was newly added to the hermetic Netstack.
        // It is not enabled by default.
        enable_interface(id.into(), hermetic_network_connector).await?;

        if let Some(interface_id_to_disable) = interface_id_to_disable {
            // Disable the matching interface on the system's Netstack.
            disable_interface(interface_id_to_disable, &SystemConnector).await?;
            self.mutated_interface_ids.push(interface_id_to_disable);
        }
        Ok(())
    }

    /// Tears down the "hermetic-network" realm.
    ///
    /// Any interfaces that were previously disabled by the controller on the
    /// system's Netstack will be re-enabled. Returns an error if there is not
    /// a running "hermetic-network" realm.
    async fn stop_hermetic_network_realm(&mut self) -> Result<(), fntr::Error> {
        destroy_child(
            network_test_realm::create_hermetic_network_realm_child_ref(),
            &SystemConnector,
        )
        .await
        .map_err(|e| match e {
            DestroyChildError::NotRunning => {
                self.hermetic_network_connector = None;
                fntr::Error::HermeticNetworkRealmNotRunning
            }
            DestroyChildError::Internal => fntr::Error::Internal,
        })?;

        // Attempt to re-enable all previously disabled interfaces on the
        // system's Netstack. If the controller fails to re-enable any of them,
        // then an error is logged but not returned. Re-enabling interfaces is
        // done on a best-effort basis.
        futures::stream::iter(self.mutated_interface_ids.drain(..))
            .for_each_concurrent(None, |id| async move {
                enable_interface(id, &SystemConnector).await.unwrap_or_else(|e| {
                    warn!("failed to re-enable interface id: {} with erorr: {:?}", id, e)
                })
            })
            .await;
        self.hermetic_network_connector = None;
        self.multicast_v4_socket = None;
        self.multicast_v6_socket = None;
        Ok(())
    }

    /// Starts the "hermetic-network" realm with the provided `netstack`.
    ///
    /// Adds the "hermetic-network" component to the "enclosed-network"
    /// collection.
    async fn start_hermetic_network_realm(
        &mut self,
        netstack: fntr::Netstack,
    ) -> Result<(), fntr::Error> {
        if let Some(_hermetic_network_connector) = &self.hermetic_network_connector {
            // The `Controller` only configures one hermetic network realm
            // at a time. As a result, any existing realm must be stopped before
            // a new one is started.
            self.stop_hermetic_network_realm().await.map_err(|e| match e {
                fntr::Error::HermeticNetworkRealmNotRunning => {
                    panic!("attempted to stop hermetic network realm that was not running")
                }
                fntr::Error::AddressInUse
                | fntr::Error::AddressNotAvailable
                | fntr::Error::ComponentNotFound
                | fntr::Error::Internal
                | fntr::Error::InterfaceNotFound
                | fntr::Error::InvalidArguments
                | fntr::Error::PingFailed
                | fntr::Error::StubNotRunning
                | fntr::Error::TimeoutExceeded => e,
            })?;
        }

        let url = match netstack {
            fntr::Netstack::V2 => HERMETIC_NETWORK_V2_URL,
        };

        create_child(
            fdecl::CollectionRef {
                name: network_test_realm::HERMETIC_NETWORK_COLLECTION_NAME.to_string(),
            },
            create_child_decl(network_test_realm::HERMETIC_NETWORK_REALM_NAME, url),
            &SystemConnector,
        )
        .await?;

        self.hermetic_network_connector = Some(HermeticNetworkConnector::new().await?);
        Ok(())
    }
}

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    let mut fs = fuchsia_component::server::ServiceFs::new_local();
    let _: &mut fuchsia_component::server::ServiceFsDir<'_, _> =
        fs.dir("svc").add_fidl_service(|s: fntr::ControllerRequestStream| s);

    let _: &mut fuchsia_component::server::ServiceFs<_> =
        fs.take_and_serve_directory_handle().context("failed to serve ServiceFs directory")?;

    let mut controller = Controller::new();

    let mut requests = fs.fuse().flatten_unordered();

    while let Some(controller_request) = requests.next().await {
        futures::future::ready(controller_request)
            .and_then(|req| controller.handle_request(req))
            .await
            .unwrap_or_else(|e| {
                if !fidl::Error::is_closed(&e) {
                    error!("handle_request failed: {:?}", e);
                } else {
                    warn!("handle_request closed: {:?}", e);
                }
            });
    }

    unreachable!("Stopped serving requests");
}
