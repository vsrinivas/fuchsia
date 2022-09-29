// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs, unreachable_patterns)]

//! Netemul utilities.

use std::{
    borrow::Cow,
    convert::TryFrom as _,
    path::{Path, PathBuf},
};

use fidl_fuchsia_hardware_ethernet as fethernet;
use fidl_fuchsia_hardware_network as fnetwork;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_debug as fnet_debug;
use fidl_fuchsia_net_ext as fnet_ext;
use fidl_fuchsia_net_interfaces as fnet_interfaces;
use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use fidl_fuchsia_net_interfaces_ext as fnet_interfaces_ext;
use fidl_fuchsia_net_neighbor as fnet_neighbor;
use fidl_fuchsia_net_stack as fnet_stack;
use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use fidl_fuchsia_netemul as fnetemul;
use fidl_fuchsia_netemul_network as fnetemul_network;
use fidl_fuchsia_netstack as fnetstack;
use fidl_fuchsia_posix_socket as fposix_socket;
use fidl_fuchsia_posix_socket_raw as fposix_socket_raw;
use fuchsia_zircon as zx;

use anyhow::{anyhow, Context as _};
use fidl::endpoints::Proxy as _;
use futures::{
    future::{FutureExt as _, TryFutureExt as _},
    SinkExt as _, StreamExt as _, TryStreamExt as _,
};

type Result<T = ()> = std::result::Result<T, anyhow::Error>;

/// The default MTU used in netemul endpoint configurations.
pub const DEFAULT_MTU: u16 = 1500;

/// Abstraction for different endpoint backing types.
pub trait Endpoint: Copy + Clone {
    /// The backing [`EndpointBacking`] for this `Endpoint`.
    ///
    /// [`EndpointBacking`]: fnetemul_network::EndpointBacking
    const NETEMUL_BACKING: fnetemul_network::EndpointBacking;

    /// The relative path from the root device directory where devices of this `Endpoint`
    /// can be discovered.
    const DEV_PATH: &'static str;

    /// Returns the path to an endpoint with the given name in this `Endpoint`'s
    /// device directory.
    fn dev_path(name: &str) -> PathBuf {
        Path::new(Self::DEV_PATH).join(name)
    }

    /// Returns an [`EndpointConfig`] with the provided parameters for this
    /// endpoint type.
    ///
    /// [`EndpointConfig`]: fnetemul_network::EndpointConfig
    fn make_config(mtu: u16, mac: Option<fnet::MacAddress>) -> fnetemul_network::EndpointConfig {
        fnetemul_network::EndpointConfig {
            mtu,
            mac: mac.map(Box::new),
            backing: Self::NETEMUL_BACKING,
        }
    }
}

/// An Ethernet implementation of `Endpoint`.
#[derive(Copy, Clone)]
pub enum Ethernet {}

impl Endpoint for Ethernet {
    const NETEMUL_BACKING: fnetemul_network::EndpointBacking =
        fnetemul_network::EndpointBacking::Ethertap;
    const DEV_PATH: &'static str = "class/ethernet";
}

/// A Network Device implementation of `Endpoint`.
#[derive(Copy, Clone)]
pub enum NetworkDevice {}

impl Endpoint for NetworkDevice {
    const NETEMUL_BACKING: fnetemul_network::EndpointBacking =
        fnetemul_network::EndpointBacking::NetworkDevice;
    const DEV_PATH: &'static str = "class/network";
}

/// A test sandbox backed by a [`fnetemul::SandboxProxy`].
///
/// `TestSandbox` provides various utility methods to set up network realms for
/// use in testing. The lifetime of the `TestSandbox` is tied to the netemul
/// sandbox itself, dropping it will cause all the created realms, networks, and
/// endpoints to be destroyed.
#[must_use]
pub struct TestSandbox {
    sandbox: fnetemul::SandboxProxy,
}

impl TestSandbox {
    /// Creates a new empty sandbox.
    pub fn new() -> Result<TestSandbox> {
        fuchsia_component::client::connect_to_protocol::<fnetemul::SandboxMarker>()
            .context("failed to connect to sandbox protocol")
            .map(|sandbox| TestSandbox { sandbox })
    }

    /// Creates a realm with `name` and `children`.
    pub fn create_realm<'a, I>(
        &'a self,
        name: impl Into<Cow<'a, str>>,
        children: I,
    ) -> Result<TestRealm<'a>>
    where
        I: IntoIterator,
        I::Item: Into<fnetemul::ChildDef>,
    {
        let (realm, server) = fidl::endpoints::create_proxy::<fnetemul::ManagedRealmMarker>()?;
        let name = name.into();
        let () = self.sandbox.create_realm(
            server,
            fnetemul::RealmOptions {
                name: Some(name.clone().into_owned()),
                children: Some(children.into_iter().map(Into::into).collect()),
                ..fnetemul::RealmOptions::EMPTY
            },
        )?;
        Ok(TestRealm { realm, name, _sandbox: self })
    }

    /// Creates a realm with no components.
    pub fn create_empty_realm<'a>(
        &'a self,
        name: impl Into<Cow<'a, str>>,
    ) -> Result<TestRealm<'a>> {
        self.create_realm(name, std::iter::empty::<fnetemul::ChildDef>())
    }

    /// Connects to the sandbox's `NetworkContext`.
    fn get_network_context(&self) -> Result<fnetemul_network::NetworkContextProxy> {
        let (ctx, server) =
            fidl::endpoints::create_proxy::<fnetemul_network::NetworkContextMarker>()?;
        let () = self.sandbox.get_network_context(server)?;
        Ok(ctx)
    }

    /// Connects to the sandbox's `NetworkManager`.
    pub fn get_network_manager(&self) -> Result<fnetemul_network::NetworkManagerProxy> {
        let ctx = self.get_network_context()?;
        let (network_manager, server) =
            fidl::endpoints::create_proxy::<fnetemul_network::NetworkManagerMarker>()?;
        let () = ctx.get_network_manager(server)?;
        Ok(network_manager)
    }

    /// Connects to the sandbox's `EndpointManager`.
    pub fn get_endpoint_manager(&self) -> Result<fnetemul_network::EndpointManagerProxy> {
        let ctx = self.get_network_context()?;
        let (ep_manager, server) =
            fidl::endpoints::create_proxy::<fnetemul_network::EndpointManagerMarker>()?;
        let () = ctx.get_endpoint_manager(server)?;
        Ok(ep_manager)
    }

    /// Creates a new empty network with default configurations and `name`.
    pub async fn create_network<'a>(
        &'a self,
        name: impl Into<Cow<'a, str>>,
    ) -> Result<TestNetwork<'a>> {
        let name = name.into();
        let netm = self.get_network_manager()?;
        let (status, network) = netm
            .create_network(
                &name,
                fnetemul_network::NetworkConfig {
                    latency: None,
                    packet_loss: None,
                    reorder: None,
                    ..fnetemul_network::NetworkConfig::EMPTY
                },
            )
            .await
            .context("create_network FIDL error")?;
        let () = zx::Status::ok(status).context("create_network failed")?;
        let network = network
            .ok_or_else(|| anyhow::anyhow!("create_network didn't return a valid network"))?
            .into_proxy()?;
        Ok(TestNetwork { network, name, sandbox: self })
    }

    /// Creates new networks and endpoints as specified in `networks`.
    pub async fn setup_networks<'a>(
        &'a self,
        mut networks: Vec<fnetemul_network::NetworkSetup>,
    ) -> Result<TestNetworkSetup<'a>> {
        let ctx = self.get_network_context()?;
        let (status, handle) =
            ctx.setup(&mut networks.iter_mut()).await.context("setup FIDL error")?;
        let () = zx::Status::ok(status).context("setup failed")?;
        let handle = handle
            .ok_or_else(|| anyhow::anyhow!("setup didn't return a valid handle"))?
            .into_proxy()?;
        Ok(TestNetworkSetup { _setup: handle, _sandbox: self })
    }

    /// Creates a new unattached endpoint with default configurations and `name`.
    ///
    /// Characters may be dropped from the front of `name` if it exceeds the maximum length.
    pub async fn create_endpoint<'a, E, S>(&'a self, name: S) -> Result<TestEndpoint<'a>>
    where
        S: Into<Cow<'a, str>>,
        E: Endpoint,
    {
        self.create_endpoint_with(name, E::make_config(DEFAULT_MTU, None)).await
    }

    /// Creates a new unattached endpoint with the provided configuration.
    ///
    /// Characters may be dropped from the front of `name` if it exceeds the maximum length.
    pub async fn create_endpoint_with<'a>(
        &'a self,
        name: impl Into<Cow<'a, str>>,
        mut config: fnetemul_network::EndpointConfig,
    ) -> Result<TestEndpoint<'a>> {
        let name = {
            let max_len = usize::try_from(fidl_fuchsia_hardware_ethertap::MAX_NAME_LENGTH)
                .context("fuchsia.hardware.ethertap/MAX_NAME_LENGTH does not fit in usize")?;
            truncate_dropping_front(name.into(), max_len)
        };

        let epm = self.get_endpoint_manager()?;
        let (status, endpoint) =
            epm.create_endpoint(&name, &mut config).await.context("create_endpoint FIDL error")?;
        let () = zx::Status::ok(status).context("create_endpoint failed")?;
        let endpoint = endpoint
            .ok_or_else(|| anyhow::anyhow!("create_endpoint didn't return a valid endpoint"))?
            .into_proxy()?;
        Ok(TestEndpoint { endpoint, name, _sandbox: self })
    }
}

/// A set of virtual networks and endpoints.
///
/// Created through [`TestSandbox::setup_networks`].
#[must_use]
pub struct TestNetworkSetup<'a> {
    _setup: fnetemul_network::SetupHandleProxy,
    _sandbox: &'a TestSandbox,
}

impl TestNetworkSetup<'_> {
    /// Extracts the proxy to the backing setup handle.
    ///
    /// Note that this defeats the lifetime semantics that ensure the sandbox in
    /// which these networks were created lives as long as the networks. The caller
    /// of [`TestNetworkSetup::into_proxy`] is responsible for ensuring that the
    /// sandbox outlives the networks.
    pub fn into_proxy(self) -> fnetemul_network::SetupHandleProxy {
        let Self { _setup, _sandbox: _ } = self;
        _setup
    }
}

/// [`TestInterface`] configuration.
#[derive(Default)]
pub struct InterfaceConfig<'a> {
    /// Optional interface name.
    pub name: Option<Cow<'a, str>>,
    /// Optional default route metric.
    pub metric: Option<u32>,
}

/// A realm within a netemul sandbox.
#[must_use]
pub struct TestRealm<'a> {
    realm: fnetemul::ManagedRealmProxy,
    name: Cow<'a, str>,
    _sandbox: &'a TestSandbox,
}

impl<'a> std::fmt::Debug for TestRealm<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::result::Result<(), std::fmt::Error> {
        let Self { realm: _, name, _sandbox } = self;
        f.debug_struct("TestRealm").field("name", name).finish_non_exhaustive()
    }
}

impl<'a> TestRealm<'a> {
    /// Connects to a protocol within the realm.
    pub fn connect_to_protocol<S>(&self) -> Result<S::Proxy>
    where
        S: fidl::endpoints::DiscoverableProtocolMarker,
    {
        (|| {
            let (proxy, server_end) =
                fidl::endpoints::create_proxy::<S>().context("create proxy")?;
            let () = self
                .connect_to_protocol_with_server_end(server_end)
                .context("connect to protocol name with server end")?;
            Result::Ok(proxy)
        })()
        .context(S::DEBUG_NAME)
    }

    /// Connects to a protocol within the realm.
    pub fn connect_to_protocol_with_server_end<S: fidl::endpoints::DiscoverableProtocolMarker>(
        &self,
        server_end: fidl::endpoints::ServerEnd<S>,
    ) -> Result {
        self.realm
            .connect_to_protocol(S::PROTOCOL_NAME, None, server_end.into_channel())
            .context("connect to protocol")
    }

    /// Gets the relative moniker of the root of the managed realm.
    pub async fn get_moniker(&self) -> Result<String> {
        self.realm.get_moniker().await.context("failed to call get moniker")
    }

    /// Stops the specified child component of the managed realm.
    pub async fn stop_child_component(&self, child_name: &str) -> Result {
        self.realm
            .stop_child_component(child_name)
            .await?
            .map_err(zx::Status::from_raw)
            .with_context(|| format!("failed to stop child component '{}'", child_name))
    }

    /// Use default endpoint/interface configuration and the specified address
    /// configuration to create a test interface.
    ///
    /// Characters may be dropped from the front of `ep_name` if it exceeds the
    /// maximum length.
    pub async fn join_network<E, S>(
        &self,
        network: &TestNetwork<'a>,
        ep_name: S,
    ) -> Result<TestInterface<'a>>
    where
        E: Endpoint,
        S: Into<Cow<'a, str>>,
    {
        self.join_network_with_if_config::<E, S>(network, ep_name, Default::default()).await
    }

    /// Use default endpoint configuration and the specified interface/address
    /// configuration to create a test interface.
    ///
    /// Characters may be dropped from the front of `ep_name` if it exceeds the
    /// maximum length.
    pub async fn join_network_with_if_config<E, S>(
        &self,
        network: &TestNetwork<'a>,
        ep_name: S,
        if_config: InterfaceConfig<'a>,
    ) -> Result<TestInterface<'a>>
    where
        E: Endpoint,
        S: Into<Cow<'a, str>>,
    {
        let endpoint =
            network.create_endpoint::<E, _>(ep_name).await.context("failed to create endpoint")?;
        self.install_endpoint(endpoint, if_config).await
    }

    /// Joins `network` with by creating an endpoint with `ep_config` and
    /// installing it into the realm with `if_config`.
    ///
    /// Returns a [`TestInterface`] corresponding to the added interface. The
    /// interface is guaranteed to have its link up and be enabled when this
    /// async function resolves.
    ///
    /// Note that this realm needs a Netstack for this operation to succeed.
    ///
    /// Characters may be dropped from the front of `ep_name` if it exceeds the maximum length.
    pub async fn join_network_with(
        &self,
        network: &TestNetwork<'a>,
        ep_name: impl Into<Cow<'a, str>>,
        ep_config: fnetemul_network::EndpointConfig,
        if_config: InterfaceConfig<'a>,
    ) -> Result<TestInterface<'a>> {
        let endpoint = network
            .create_endpoint_with(ep_name, ep_config)
            .await
            .context("failed to create endpoint")?;
        self.install_endpoint(endpoint, if_config).await
    }

    /// Installs and configures the endpoint in this realm.
    ///
    /// Note that if `name` is not `None`, the string must fit within interface name limits.
    pub async fn install_endpoint(
        &self,
        endpoint: TestEndpoint<'a>,
        if_config: InterfaceConfig<'a>,
    ) -> Result<TestInterface<'a>> {
        let interface = endpoint
            .into_interface_in_realm_with_name(self, if_config)
            .await
            .context("failed to add endpoint")?;
        let () = interface.set_link_up(true).await.context("failed to start endpoint")?;
        let _did_enable: bool = interface
            .control()
            .enable()
            .await
            .map_err(anyhow::Error::new)
            .and_then(|res| {
                res.map_err(|e: fnet_interfaces_admin::ControlEnableError| {
                    anyhow::anyhow!("{:?}", e)
                })
            })
            .context("failed to enable interface")?;

        // Wait for Netstack to observe interface up so callers can safely
        // assume the state of the world on return.
        let interface_state = self
            .connect_to_protocol::<fnet_interfaces::StateMarker>()
            .context("failed to connect to fuchsia.net.interfaces/State")?;
        let () = fnet_interfaces_ext::wait_interface_with_id(
            fnet_interfaces_ext::event_stream_from_state(&interface_state)?,
            &mut fnet_interfaces_ext::InterfaceState::Unknown(interface.id()),
            |&fnet_interfaces_ext::Properties { online, .. }| online.then_some(()),
        )
        .await
        .context("failed to observe interface up")?;

        Ok(interface)
    }

    /// Adds a device to the realm's virtual device filesystem.
    pub async fn add_virtual_device(&self, e: &TestEndpoint<'_>, path: &Path) -> Result {
        let path = path
            .to_str()
            .with_context(|| format!("convert {} to str", path.display()))?
            .to_string();
        let (device, device_server_end) =
            fidl::endpoints::create_endpoints::<fnetemul_network::DeviceProxy_Marker>()
                .context("create endpoints")?;
        e.get_proxy_(device_server_end).context("get proxy")?;

        self.realm
            .add_device(&path, device)
            .await
            .context("add device")?
            .map_err(zx::Status::from_raw)
            .context("add device error")
    }

    /// Removes a device from the realm's virtual device filesystem.
    pub async fn remove_virtual_device(&self, path: &Path) -> Result {
        let path = path.to_str().with_context(|| format!("convert {} to str", path.display()))?;
        self.realm
            .remove_device(path)
            .await
            .context("remove device")?
            .map_err(zx::Status::from_raw)
            .context("remove device error")
    }

    /// Creates a Datagram [`socket2::Socket`] backed by the implementation of
    /// `fuchsia.posix.socket/Provider` in this realm.
    pub async fn datagram_socket(
        &self,
        domain: fposix_socket::Domain,
        proto: fposix_socket::DatagramSocketProtocol,
    ) -> Result<socket2::Socket> {
        let socket_provider = self
            .connect_to_protocol::<fposix_socket::ProviderMarker>()
            .context("failed to connect to socket provider")?;
        let response = socket_provider
            .datagram_socket(domain, proto)
            .await
            .context("failed to call socket")?
            .map_err(|e| std::io::Error::from_raw_os_error(e.into_primitive()))
            .context("failed to create socket")?;

        match response {
            fposix_socket::ProviderDatagramSocketResponse::SynchronousDatagramSocket(sock) => {
                Ok(fdio::create_fd(sock.into()).context("failed to create fd")?)
            }
            fposix_socket::ProviderDatagramSocketResponse::DatagramSocket(sock) => {
                Ok(fdio::create_fd(sock.into()).context("failed to create fd")?)
            }
        }
    }

    /// Creates a raw [`socket2::Socket`] backed by the implementation of
    /// `fuchsia.posix.socket.raw/Provider` in this realm.
    pub async fn raw_socket(
        &self,
        domain: fposix_socket::Domain,
        mut association: fposix_socket_raw::ProtocolAssociation,
    ) -> Result<socket2::Socket> {
        let socket_provider = self
            .connect_to_protocol::<fposix_socket_raw::ProviderMarker>()
            .context("failed to connect to socket provider")?;
        let sock = socket_provider
            .socket(domain, &mut association)
            .await
            .context("failed to call socket")?
            .map_err(|e| std::io::Error::from_raw_os_error(e.into_primitive()))
            .context("failed to create socket")?;

        Ok(fdio::create_fd(sock.into()).context("failed to create fd")?)
    }

    /// Creates a Stream [`socket2::Socket`] backed by the implementation of
    /// `fuchsia.posix.socket/Provider` in this realm.
    pub async fn stream_socket(
        &self,
        domain: fposix_socket::Domain,
        proto: fposix_socket::StreamSocketProtocol,
    ) -> Result<socket2::Socket> {
        let socket_provider = self
            .connect_to_protocol::<fposix_socket::ProviderMarker>()
            .context("failed to connect to socket provider")?;
        let sock = socket_provider
            .stream_socket(domain, proto)
            .await
            .context("failed to call socket")?
            .map_err(|e| std::io::Error::from_raw_os_error(e.into_primitive()))
            .context("failed to create socket")?;

        Ok(fdio::create_fd(sock.into()).context("failed to create fd")?)
    }

    /// Shuts down the realm.
    ///
    /// It is often useful to call this method to ensure that the realm
    /// completes orderly shutdown before allowing other resources to be dropped
    /// and get cleaned up, such as [`TestEndpoint`]s, which components in the
    /// realm might be interacting with.
    pub async fn shutdown(&self) -> Result {
        let () = self.realm.shutdown().context("call shutdown")?;
        let events = self
            .realm
            .take_event_stream()
            .try_collect::<Vec<_>>()
            .await
            .context("error on realm event stream")?;
        // Ensure there are no more events sent on the event stream after `OnShutdown`.
        assert_matches::assert_matches!(events[..], [fnetemul::ManagedRealmEvent::OnShutdown {}]);
        Ok(())
    }

    /// Constructs an ICMP socket.
    pub async fn icmp_socket<Ip: ping::IpExt>(&self) -> Result<fuchsia_async::net::DatagramSocket> {
        let sock = self
            .datagram_socket(Ip::DOMAIN_FIDL, fposix_socket::DatagramSocketProtocol::IcmpEcho)
            .await
            .context("failed to create ICMP datagram socket")?;
        fuchsia_async::net::DatagramSocket::new_from_socket(sock)
            .context("failed to create async ICMP datagram socket")
    }

    /// Sends a single ICMP echo request to `addr`, and waits for the echo reply.
    pub async fn ping_once<Ip: ping::IpExt>(&self, addr: Ip::Addr, seq: u16) -> Result {
        let icmp_sock = self.icmp_socket::<Ip>().await?;

        const MESSAGE: &'static str = "hello, world";
        let (mut sink, mut stream) = ping::new_unicast_sink_and_stream::<
            Ip,
            _,
            { MESSAGE.len() + ping::ICMP_HEADER_LEN },
        >(&icmp_sock, &addr, MESSAGE.as_bytes());

        let send_fut = sink.send(seq).map_err(anyhow::Error::new);
        let recv_fut = stream.try_next().map(|r| match r {
            Ok(Some(got)) if got == seq => Ok(()),
            Ok(Some(got)) => Err(anyhow!("unexpected echo reply; got: {}, want: {}", got, seq)),
            Ok(None) => Err(anyhow!("echo reply stream ended unexpectedly")),
            Err(e) => Err(anyhow::Error::from(e)),
        });

        let ((), ()) = futures::future::try_join(send_fut, recv_fut)
            .await
            .with_context(|| format!("failed to ping from {} to {}", self.name, addr,))?;
        Ok(())
    }

    // TODO(https://fxbug.dev/88245): Remove this function when pinging only
    // once is free from NUD-related issues and is guaranteed to succeed.
    /// Sends ICMP echo requests to `addr` on a 1-second interval until a response
    /// is received.
    pub async fn ping<Ip: ping::IpExt>(&self, addr: Ip::Addr) -> Result {
        let icmp_sock = self.icmp_socket::<Ip>().await?;

        const MESSAGE: &'static str = "hello, world";
        let (mut sink, stream) = ping::new_unicast_sink_and_stream::<
            Ip,
            _,
            { MESSAGE.len() + ping::ICMP_HEADER_LEN },
        >(&icmp_sock, &addr, MESSAGE.as_bytes());

        let mut seq = 0;
        let mut interval_stream =
            fuchsia_async::Interval::new(fuchsia_async::Duration::from_seconds(1));
        let mut stream = stream.fuse();
        loop {
            futures::select! {
                opt = interval_stream.next() => {
                    let () = opt.ok_or_else(|| anyhow!("ping interval stream ended unexpectedly"))?;
                    seq += 1;
                    let () = sink.send(seq).map_err(anyhow::Error::new).await?;
                }
                r = stream.try_next() => {
                    return match r {
                        Ok(Some(got)) if got <= seq => Ok(()),
                        Ok(Some(got)) => {
                            Err(anyhow!("unexpected echo reply; got: {}, want: {}", got, seq))
                        }
                        Ok(None) => Err(anyhow!("echo reply stream ended unexpectedly")),
                        Err(e) => Err(anyhow::Error::from(e)),
                    };
                }
            }
        }
    }

    /// Add a static neighbor entry.
    ///
    /// Useful to prevent NUD resolving too slow and causing spurious test failures.
    pub async fn add_neighbor_entry(
        &self,
        interface: u64,
        mut addr: fnet::IpAddress,
        mut mac: fnet::MacAddress,
    ) -> Result {
        let controller = self
            .connect_to_protocol::<fnet_neighbor::ControllerMarker>()
            .context("connect to protocol")?;
        controller
            .add_entry(interface, &mut addr, &mut mac)
            .await
            .context("add_entry")?
            .map_err(zx::Status::from_raw)
            .context("add_entry failed")
    }

    /// Get a stream of interface events from a new watcher.
    pub fn get_interface_event_stream(
        &self,
    ) -> Result<impl futures::Stream<Item = std::result::Result<fnet_interfaces::Event, fidl::Error>>>
    {
        let interface_state = self
            .connect_to_protocol::<fnet_interfaces::StateMarker>()
            .context("connect to protocol")?;
        fnet_interfaces_ext::event_stream_from_state(&interface_state)
            .context("get interface event stream")
    }
}

/// A virtual Network.
///
/// `TestNetwork` is a single virtual broadcast domain backed by Netemul.
/// Created through [`TestSandbox::create_network`].
#[must_use]
pub struct TestNetwork<'a> {
    network: fnetemul_network::NetworkProxy,
    name: Cow<'a, str>,
    sandbox: &'a TestSandbox,
}

impl<'a> std::fmt::Debug for TestNetwork<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::result::Result<(), std::fmt::Error> {
        let Self { name, network: _, sandbox: _ } = self;
        f.debug_struct("TestNetwork").field("name", name).finish_non_exhaustive()
    }
}

impl<'a> TestNetwork<'a> {
    /// Extracts the proxy to the backing network.
    ///
    /// Note that this defeats the lifetime semantics that ensure the sandbox in
    /// which this network was created lives as long as the network. The caller of
    /// [`TestNetwork::into_proxy`] is responsible for ensuring that the sandbox
    /// outlives the network.
    pub fn into_proxy(self) -> fnetemul_network::NetworkProxy {
        let Self { network, name: _, sandbox: _ } = self;
        network
    }

    /// Sets the configuration for this network to `config`.
    pub async fn set_config(&self, config: fnetemul_network::NetworkConfig) -> Result<()> {
        let status = self.network.set_config(config).await.context("call set_config")?;
        zx::Status::ok(status).context("set config")
    }

    /// Attaches `ep` to this network.
    pub async fn attach_endpoint(&self, ep: &TestEndpoint<'a>) -> Result<()> {
        let status =
            self.network.attach_endpoint(&ep.name).await.context("attach_endpoint FIDL error")?;
        let () = zx::Status::ok(status).context("attach_endpoint failed")?;
        Ok(())
    }

    /// Creates a new endpoint with `name` attached to this network.
    ///
    /// Characters may be dropped from the front of `name` if it exceeds the maximum length.
    pub async fn create_endpoint<E, S>(&self, name: S) -> Result<TestEndpoint<'a>>
    where
        E: Endpoint,
        S: Into<Cow<'a, str>>,
    {
        let ep = self
            .sandbox
            .create_endpoint::<E, _>(name)
            .await
            .with_context(|| format!("failed to create endpoint for network {}", self.name))?;
        let () = self.attach_endpoint(&ep).await.with_context(|| {
            format!("failed to attach endpoint {} to network {}", ep.name, self.name)
        })?;
        Ok(ep)
    }

    /// Creates a new endpoint with `name` and `config` attached to this network.
    ///
    /// Characters may be dropped from the front of `name` if it exceeds the maximum length.
    pub async fn create_endpoint_with(
        &self,
        name: impl Into<Cow<'a, str>>,
        config: fnetemul_network::EndpointConfig,
    ) -> Result<TestEndpoint<'a>> {
        let ep = self
            .sandbox
            .create_endpoint_with(name, config)
            .await
            .with_context(|| format!("failed to create endpoint for network {}", self.name))?;
        let () = self.attach_endpoint(&ep).await.with_context(|| {
            format!("failed to attach endpoint {} to network {}", ep.name, self.name)
        })?;
        Ok(ep)
    }

    /// Returns a fake endpoint.
    pub fn create_fake_endpoint(&self) -> Result<TestFakeEndpoint<'a>> {
        let (endpoint, server) =
            fidl::endpoints::create_proxy::<fnetemul_network::FakeEndpointMarker>()
                .context("failed to create launcher proxy")?;
        let () = self.network.create_fake_endpoint(server)?;
        return Ok(TestFakeEndpoint { endpoint, _sandbox: self.sandbox });
    }
}

/// A virtual network endpoint backed by Netemul.
#[must_use]
pub struct TestEndpoint<'a> {
    endpoint: fnetemul_network::EndpointProxy,
    name: Cow<'a, str>,
    _sandbox: &'a TestSandbox,
}

impl<'a> std::fmt::Debug for TestEndpoint<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::result::Result<(), std::fmt::Error> {
        let Self { endpoint: _, name, _sandbox } = self;
        f.debug_struct("TestEndpoint").field("name", name).finish_non_exhaustive()
    }
}

impl<'a> std::ops::Deref for TestEndpoint<'a> {
    type Target = fnetemul_network::EndpointProxy;

    fn deref(&self) -> &Self::Target {
        &self.endpoint
    }
}

/// A virtual fake network endpoint backed by Netemul.
#[must_use]
pub struct TestFakeEndpoint<'a> {
    endpoint: fnetemul_network::FakeEndpointProxy,
    _sandbox: &'a TestSandbox,
}

impl<'a> std::ops::Deref for TestFakeEndpoint<'a> {
    type Target = fnetemul_network::FakeEndpointProxy;

    fn deref(&self) -> &Self::Target {
        &self.endpoint
    }
}

impl<'a> TestFakeEndpoint<'a> {
    /// Return a stream of frames.
    ///
    /// Frames will be yielded as they are read from the fake endpoint.
    pub fn frame_stream(
        &self,
    ) -> impl futures::Stream<Item = std::result::Result<(Vec<u8>, u64), fidl::Error>> + '_ {
        futures::stream::try_unfold(&self.endpoint, |ep| ep.read().map_ok(move |r| Some((r, ep))))
    }
}

/// Returns the [`fnetwork::PortId`] for a device that is expected to have a
/// single port.
///
/// Returns an error if the device has more or less than a single port.
pub async fn get_single_device_port_id(device: &fnetwork::DeviceProxy) -> Result<fnetwork::PortId> {
    let (watcher, server) = fidl::endpoints::create_proxy::<fnetwork::PortWatcherMarker>()
        .context("create endpoints")?;
    let () = device.get_port_watcher(server).context("get port watcher")?;
    let stream = futures::stream::try_unfold(watcher, |watcher| async move {
        let event = watcher.watch().await.context("watch failed")?;
        match event {
            fnetwork::DevicePortEvent::Existing(port_id) => Ok(Some((port_id, watcher))),
            fnetwork::DevicePortEvent::Idle(fnetwork::Empty {}) => Ok(None),
            e @ fnetwork::DevicePortEvent::Added(_) | e @ fnetwork::DevicePortEvent::Removed(_) => {
                Err(anyhow::anyhow!("unexpected device port event {:?}", e))
            }
        }
    });
    futures::pin_mut!(stream);
    let port_id = stream
        .try_next()
        .await
        .context("fetching first port")?
        .ok_or_else(|| anyhow::anyhow!("no ports found on device"))?;
    let rest = stream.try_collect::<Vec<_>>().await.context("observing idle")?;
    if rest.is_empty() {
        Ok(port_id)
    } else {
        Err(anyhow::anyhow!("found more than one device port: {:?}, {:?}", port_id, rest))
    }
}

/// Helper function to retrieve device and port information from a netemul
/// netdevice instance.
async fn to_netdevice_inner(
    netdevice: fidl::endpoints::ClientEnd<fnetwork::DeviceInstanceMarker>,
) -> Result<(fidl::endpoints::ClientEnd<fnetwork::DeviceMarker>, fnetwork::PortId)> {
    let netdevice: fnetwork::DeviceInstanceProxy = netdevice.into_proxy()?;
    let (device, device_server_end) = fidl::endpoints::create_proxy::<fnetwork::DeviceMarker>()?;
    let () = netdevice.get_device(device_server_end)?;
    let port_id = get_single_device_port_id(&device).await?;
    // No other references exist, we just created this proxy, unwrap is safe.
    let device = fidl::endpoints::ClientEnd::new(device.into_channel().unwrap().into_zx_channel());
    Ok((device, port_id))
}

impl<'a> TestEndpoint<'a> {
    /// Extracts the proxy to the backing endpoint.
    ///
    /// Note that this defeats the lifetime semantics that ensure the sandbox in
    /// which this endpoint was created lives as long as the endpoint. The caller of
    /// [`TestEndpoint::into_proxy`] is responsible for ensuring that the sandbox
    /// outlives the endpoint.
    pub fn into_proxy(self) -> fnetemul_network::EndpointProxy {
        let Self { endpoint, name: _, _sandbox: _ } = self;
        endpoint
    }

    /// Gets access to this device's virtual Ethernet device.
    ///
    /// Note that an error is returned if the Endpoint is not a
    /// [`fnetemul_network::DeviceConnection::Ethernet`].
    pub async fn get_ethernet(
        &self,
    ) -> Result<fidl::endpoints::ClientEnd<fethernet::DeviceMarker>> {
        match self
            .get_device()
            .await
            .with_context(|| format!("failed to get device connection for {}", self.name))?
        {
            fnetemul_network::DeviceConnection::Ethernet(e) => Ok(e),
            fnetemul_network::DeviceConnection::NetworkDevice(_) => {
                Err(anyhow::anyhow!("Endpoint {} is not an Ethernet device", self.name))
            }
        }
    }

    /// Gets access to this device's virtual Network device.
    ///
    /// Note that an error is returned if the Endpoint is not a
    /// [`fnetemul_network::DeviceConnection::NetworkDevice`].
    pub async fn get_netdevice(
        &self,
    ) -> Result<(fidl::endpoints::ClientEnd<fnetwork::DeviceMarker>, fnetwork::PortId)> {
        match self
            .get_device()
            .await
            .with_context(|| format!("failed to get device connection for {}", self.name))?
        {
            fnetemul_network::DeviceConnection::NetworkDevice(n) => to_netdevice_inner(n).await,
            fnetemul_network::DeviceConnection::Ethernet(_) => {
                Err(anyhow::anyhow!("Endpoint {} is not a Network Device", self.name))
            }
        }
    }

    /// Adds the [`TestEndpoint`] to the provided `realm` with an optional
    /// interface name.
    ///
    /// Returns the interface ID and control protocols on success.
    pub async fn add_to_stack(
        &self,
        realm: &TestRealm<'a>,
        InterfaceConfig { name, metric }: InterfaceConfig<'_>,
    ) -> Result<(
        u64,
        fnet_interfaces_ext::admin::Control,
        Option<fnet_interfaces_admin::DeviceControlProxy>,
    )> {
        let name = name.map(|n| {
            truncate_dropping_front(n.into(), fnet_interfaces::INTERFACE_NAME_LENGTH.into())
                .to_string()
        });
        // TODO(https://fxbug.dev/34719) remove support for Ethernet once the
        // netdevice migration is complete.
        match self.get_device().await.context("get_device failed")? {
            fnetemul_network::DeviceConnection::Ethernet(eth) => {
                let id = {
                    // NB: Use different stack APIs based on name because
                    // fuchsia.net.stack doesn't allow the name to be set, and
                    // netstack3 doesn't support fuchsia.netstack.
                    if let Some(name) = name {
                        let netstack = realm
                            .connect_to_protocol::<fnetstack::NetstackMarker>()
                            .context("failed to connect to netstack")?;
                        netstack
                            .add_ethernet_device(
                                &self.name,
                                &mut fnetstack::InterfaceConfig {
                                    name,
                                    filepath: String::new(),
                                    metric: metric.unwrap_or(0),
                                },
                                eth,
                            )
                            .await
                            .context("add_ethernet_device FIDL error")?
                            .map_err(fuchsia_zircon::Status::from_raw)
                            .map(u64::from)
                            .context("add_ethernet_device failed")?
                    } else {
                        let stack = realm
                            .connect_to_protocol::<fnet_stack::StackMarker>()
                            .context("failed to connect to stack")?;
                        stack.add_ethernet_interface(&self.name, eth).await.squash_result()?
                    }
                };
                let debug = realm
                    .connect_to_protocol::<fnet_debug::InterfacesMarker>()
                    .context("connect to protocol")?;
                let (control, server_end) = fnet_interfaces_ext::admin::Control::create_endpoints()
                    .context("create endpoints")?;
                let () = debug.get_admin(id, server_end).context("get admin")?;
                Ok((id, control, None))
            }
            fnetemul_network::DeviceConnection::NetworkDevice(netdevice) => {
                let (device, mut port_id) = to_netdevice_inner(netdevice).await?;
                let installer = realm
                    .connect_to_protocol::<fnet_interfaces_admin::InstallerMarker>()
                    .context("connect to protocol")?;
                let device_control = {
                    let (control, server_end) = fidl::endpoints::create_proxy::<
                        fnet_interfaces_admin::DeviceControlMarker,
                    >()
                    .context("create proxy")?;
                    let () =
                        installer.install_device(device, server_end).context("install device")?;
                    control
                };
                let (control, server_end) = fnet_interfaces_ext::admin::Control::create_endpoints()
                    .context("create endpoints")?;
                let () = device_control
                    .create_interface(
                        &mut port_id,
                        server_end,
                        fnet_interfaces_admin::Options {
                            name,
                            metric,
                            ..fnet_interfaces_admin::Options::EMPTY
                        },
                    )
                    .context("create interface")?;
                let id = control.get_id().await.context("get id")?;
                Ok((id, control, Some(device_control)))
            }
        }
    }

    /// Like `into_interface_realm_with_name` but with default parameters.
    pub async fn into_interface_in_realm(self, realm: &TestRealm<'a>) -> Result<TestInterface<'a>> {
        self.into_interface_in_realm_with_name(realm, Default::default()).await
    }

    /// Consumes this `TestEndpoint` and tries to add it to the Netstack in
    /// `realm`, returning a [`TestInterface`] on success.
    pub async fn into_interface_in_realm_with_name(
        self,
        realm: &TestRealm<'a>,
        if_config: InterfaceConfig<'_>,
    ) -> Result<TestInterface<'a>> {
        let (id, control, device_control) = self
            .add_to_stack(realm, if_config)
            .await
            .with_context(|| format!("failed to add {} to realm {}", self.name, realm.name))?;
        let stack = realm
            .connect_to_protocol::<fnet_stack::StackMarker>()
            .context("failed to connect to stack")?;
        let interface_state = realm
            .connect_to_protocol::<fnet_interfaces::StateMarker>()
            .context("failed to connect to interfaces state")?;
        Ok(TestInterface { endpoint: self, id, stack, interface_state, control, device_control })
    }
}

/// A [`TestEndpoint`] that is installed in a realm's Netstack.
#[must_use]
pub struct TestInterface<'a> {
    endpoint: TestEndpoint<'a>,
    id: u64,
    stack: fnet_stack::StackProxy,
    interface_state: fnet_interfaces::StateProxy,
    control: fnet_interfaces_ext::admin::Control,
    device_control: Option<fnet_interfaces_admin::DeviceControlProxy>,
}

impl<'a> std::fmt::Debug for TestInterface<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::result::Result<(), std::fmt::Error> {
        let Self { endpoint, id, stack: _, interface_state: _, control: _, device_control: _ } =
            self;
        f.debug_struct("TestInterface")
            .field("endpoint", endpoint)
            .field("id", id)
            .finish_non_exhaustive()
    }
}

impl<'a> std::ops::Deref for TestInterface<'a> {
    type Target = fnetemul_network::EndpointProxy;

    fn deref(&self) -> &Self::Target {
        &self.endpoint
    }
}

impl<'a> TestInterface<'a> {
    /// Gets the interface identifier.
    pub fn id(&self) -> u64 {
        self.id
    }

    /// Returns the endpoint associated with the interface.
    pub fn endpoint(&self) -> &TestEndpoint<'a> {
        &self.endpoint
    }

    /// Returns the interface's control handle.
    pub fn control(&self) -> &fnet_interfaces_ext::admin::Control {
        &self.control
    }

    /// Returns the interface's stack handle.
    pub fn stack(&self) -> &fnet_stack::StackProxy {
        &self.stack
    }

    /// Add a direct route from the interface to the given subnet.
    pub async fn add_subnet_route(&self, subnet: fnet::Subnet) -> Result<()> {
        let subnet = fnet_ext::apply_subnet_mask(subnet);
        let mut entry =
            fnet_stack::ForwardingEntry { subnet, device_id: self.id, next_hop: None, metric: 0 };
        self.stack.add_forwarding_entry(&mut entry).await.squash_result().with_context(|| {
            format!(
                "stack.add_forwarding_entry({:?}) for endpoint {} failed",
                entry, self.endpoint.name
            )
        })
    }

    /// Delete a direct route from the interface to the given subnet.
    pub async fn del_subnet_route(&self, subnet: fnet::Subnet) -> Result<()> {
        let subnet = fnet_ext::apply_subnet_mask(subnet);
        let mut entry =
            fnet_stack::ForwardingEntry { subnet, device_id: self.id, next_hop: None, metric: 0 };
        self.stack.del_forwarding_entry(&mut entry).await.squash_result().with_context(|| {
            format!(
                "stack.del_forwarding_entry({:?}) for endpoint {} failed",
                entry, self.endpoint.name
            )
        })
    }

    /// Gets the interface's properties.
    async fn get_properties(&self) -> Result<fnet_interfaces_ext::Properties> {
        let properties = fnet_interfaces_ext::existing(
            fnet_interfaces_ext::event_stream_from_state(&self.interface_state)?,
            fnet_interfaces_ext::InterfaceState::Unknown(self.id),
        )
        .await
        .context("failed to get existing interfaces")?;
        match properties {
            fnet_interfaces_ext::InterfaceState::Unknown(id) => Err(anyhow::anyhow!(
                "could not find interface {} for endpoint {}",
                id,
                self.endpoint.name
            )),
            fnet_interfaces_ext::InterfaceState::Known(properties) => Ok(properties),
        }
    }

    /// Gets the interface's addresses.
    pub async fn get_addrs(&self) -> Result<Vec<fnet_interfaces_ext::Address>> {
        let fnet_interfaces_ext::Properties {
            addresses,
            id: _,
            name: _,
            device_class: _,
            online: _,
            has_default_ipv4_route: _,
            has_default_ipv6_route: _,
        } = self.get_properties().await?;
        Ok(addresses)
    }

    /// Gets the interface's device name.
    pub async fn get_interface_name(&self) -> Result<String> {
        let fnet_interfaces_ext::Properties {
            name,
            addresses: _,
            id: _,
            device_class: _,
            online: _,
            has_default_ipv4_route: _,
            has_default_ipv6_route: _,
        } = self.get_properties().await?;
        Ok(name)
    }

    /// Gets a stream of interface events yielded by calling watch on a new watcher.
    pub fn get_interface_event_stream(
        &self,
    ) -> Result<impl futures::Stream<Item = std::result::Result<fnet_interfaces::Event, fidl::Error>>>
    {
        fnet_interfaces_ext::event_stream_from_state(&self.interface_state)
            .context("event stream from state")
    }

    async fn set_dhcp_client_enabled(&self, enable: bool) -> Result<()> {
        self.stack
            .set_dhcp_client_enabled(self.id, enable)
            .await
            .context("failed to call SetDhcpClientEnabled")?
            .map_err(|e| anyhow!("{:?}", e))
    }

    /// Starts DHCP on this interface.
    pub async fn start_dhcp(&self) -> Result<()> {
        self.set_dhcp_client_enabled(true).await.context("failed to start dhcp client")
    }

    /// Stops DHCP on this interface.
    pub async fn stop_dhcp(&self) -> Result<()> {
        self.set_dhcp_client_enabled(true).await.context("failed to stop dhcp client")
    }

    /// Adds an address and a subnet route, waiting until the address assignment
    /// state is `ASSIGNED`.
    pub async fn add_address_and_subnet_route(&self, subnet: fnet::Subnet) -> Result<()> {
        let (address_state_provider, server) =
            fidl::endpoints::create_proxy::<fnet_interfaces_admin::AddressStateProviderMarker>()
                .context("create proxy")?;
        let () = address_state_provider.detach().context("detach address lifetime")?;
        let () = self
            .control
            .add_address(
                &mut subnet.clone(),
                fnet_interfaces_admin::AddressParameters::EMPTY,
                server,
            )
            .context("FIDL error")?;

        let state_stream =
            fnet_interfaces_ext::admin::assignment_state_stream(address_state_provider);
        futures::pin_mut!(state_stream);
        let ((), ()) = futures::future::try_join(
            fnet_interfaces_ext::admin::wait_assignment_state(
                &mut state_stream,
                fnet_interfaces_admin::AddressAssignmentState::Assigned,
            )
            .map(|res| res.context("assignment state")),
            self.add_subnet_route(subnet).map(|res| res.context("add subnet route")),
        )
        .await?;
        Ok(())
    }

    /// Consumes this [`TestInterface`] and removes the associated interface
    /// in the Netstack, returning the device lifetime-carrying channels.
    pub async fn remove(
        self,
    ) -> Result<(fnetemul_network::EndpointProxy, Option<fnet_interfaces_admin::DeviceControlProxy>)>
    {
        let Self {
            endpoint: TestEndpoint { endpoint, name: _, _sandbox: _ },
            id,
            stack,
            interface_state: _,
            control,
            device_control,
        } = self;
        // For Network Devices, the `control` handle  is tied to the lifetime of
        // the interface; dropping it triggers interface removal in the
        // Netstack. For Ethernet devices this is a No-Op.
        std::mem::drop(control);
        // Ethernet devices should be removed with the `fuchsia.net.stack` API.
        // Note that NetDevice interfaces will be removed by this API in
        // Netstack2, but will generate errors in Netstack3. Hence preferring
        // the legacy `fuchsia.net.stack` API for Ethernet devices.
        // TODO(https://fxbug.dev/102064): Remove this once Ethernet devices are
        // no longer supported.
        match endpoint.get_device().await.context("get_device failed")? {
            fnetemul_network::DeviceConnection::Ethernet(_) => {
                let () = stack
                    .del_ethernet_interface(id)
                    .await
                    .squash_result()
                    .context("delete ethernet interface failed")?;
            }
            fnetemul_network::DeviceConnection::NetworkDevice(_) => {}
        }
        Ok((endpoint, device_control))
    }

    /// Consumes this [`TestInterface`] and removes the underlying device. The
    /// Netstack will implicitly remove the interface and clients can expect to
    /// observe a `PEER_CLOSED` event on the returned control channel.
    pub fn remove_device(
        self,
    ) -> (fnet_interfaces_ext::admin::Control, Option<fnet_interfaces_admin::DeviceControlProxy>)
    {
        let Self {
            endpoint: TestEndpoint { endpoint, name: _, _sandbox: _ },
            id: _,
            stack: _,
            interface_state: _,
            control,
            device_control,
        } = self;
        std::mem::drop(endpoint);
        (control, device_control)
    }
}

/// Get the [`socket2::Domain`] for `addr`.
fn get_socket2_domain(addr: &std::net::SocketAddr) -> fposix_socket::Domain {
    let domain = match addr {
        std::net::SocketAddr::V4(_) => fposix_socket::Domain::Ipv4,
        std::net::SocketAddr::V6(_) => fposix_socket::Domain::Ipv6,
    };

    domain
}

/// Trait describing UDP sockets that can be bound in a testing realm.
pub trait RealmUdpSocket: Sized {
    /// Creates a UDP socket in `realm` bound to `addr`.
    fn bind_in_realm<'a>(
        realm: &'a TestRealm<'a>,
        addr: std::net::SocketAddr,
    ) -> futures::future::LocalBoxFuture<'a, Result<Self>>;
}

impl RealmUdpSocket for std::net::UdpSocket {
    fn bind_in_realm<'a>(
        realm: &'a TestRealm<'a>,
        addr: std::net::SocketAddr,
    ) -> futures::future::LocalBoxFuture<'a, Result<Self>> {
        async move {
            let sock = realm
                .datagram_socket(
                    get_socket2_domain(&addr),
                    fposix_socket::DatagramSocketProtocol::Udp,
                )
                .await
                .context("failed to create socket")?;

            let () = sock.bind(&addr.into()).context("bind failed")?;

            Result::Ok(sock.into())
        }
        .boxed_local()
    }
}

impl RealmUdpSocket for fuchsia_async::net::UdpSocket {
    fn bind_in_realm<'a>(
        realm: &'a TestRealm<'a>,
        addr: std::net::SocketAddr,
    ) -> futures::future::LocalBoxFuture<'a, Result<Self>> {
        std::net::UdpSocket::bind_in_realm(realm, addr)
            .and_then(|udp| {
                futures::future::ready(
                    fuchsia_async::net::UdpSocket::from_socket(udp)
                        .context("failed to create fuchsia_async socket"),
                )
            })
            .boxed_local()
    }
}

/// Trait describing TCP listeners bound in a testing realm.
pub trait RealmTcpListener: Sized {
    /// Creates a TCP listener in `realm` bound to `addr`.
    fn listen_in_realm<'a>(
        realm: &'a TestRealm<'a>,
        addr: std::net::SocketAddr,
    ) -> futures::future::LocalBoxFuture<'a, Result<Self>> {
        Self::listen_in_realm_with(realm, addr, |_: &socket2::Socket| Ok(()))
    }

    /// Creates a TCP listener by creating a Socket2 socket in `realm`. Closure `setup` is called
    /// with the reference of the socket before the socket is bound to `addr`.
    fn listen_in_realm_with<'a>(
        realm: &'a TestRealm<'a>,
        addr: std::net::SocketAddr,
        setup: impl FnOnce(&socket2::Socket) -> Result<()> + 'a,
    ) -> futures::future::LocalBoxFuture<'a, Result<Self>>;
}

impl RealmTcpListener for std::net::TcpListener {
    fn listen_in_realm_with<'a>(
        realm: &'a TestRealm<'a>,
        addr: std::net::SocketAddr,
        setup: impl FnOnce(&socket2::Socket) -> Result<()> + 'a,
    ) -> futures::future::LocalBoxFuture<'a, Result<Self>> {
        async move {
            let sock = realm
                .stream_socket(get_socket2_domain(&addr), fposix_socket::StreamSocketProtocol::Tcp)
                .await
                .context("failed to create server socket")?;
            let () = setup(&sock)?;
            let () = sock.bind(&addr.into()).context("failed to bind server socket")?;
            // Use 128 for the listen() backlog, same as the original implementation of TcpListener
            // in Rust std (see https://doc.rust-lang.org/src/std/sys_common/net.rs.html#386).
            let () = sock.listen(128).context("failed to listen on server socket")?;

            Result::Ok(sock.into())
        }
        .boxed_local()
    }
}

impl RealmTcpListener for fuchsia_async::net::TcpListener {
    fn listen_in_realm_with<'a>(
        realm: &'a TestRealm<'a>,
        addr: std::net::SocketAddr,
        setup: impl FnOnce(&socket2::Socket) -> Result<()> + 'a,
    ) -> futures::future::LocalBoxFuture<'a, Result<Self>> {
        std::net::TcpListener::listen_in_realm_with(realm, addr, setup)
            .and_then(|listener| {
                futures::future::ready(
                    fuchsia_async::net::TcpListener::from_std(listener)
                        .context("failed to create fuchsia_async socket"),
                )
            })
            .boxed_local()
    }
}

/// Trait describing TCP streams in a testing realm.
pub trait RealmTcpStream: Sized {
    /// Creates a TCP stream in `realm` connected to `addr`.
    fn connect_in_realm<'a>(
        realm: &'a TestRealm<'a>,
        addr: std::net::SocketAddr,
    ) -> futures::future::LocalBoxFuture<'a, Result<Self>>;

    // TODO: Implement this trait for std::net::TcpStream.
}

impl RealmTcpStream for fuchsia_async::net::TcpStream {
    fn connect_in_realm<'a>(
        realm: &'a TestRealm<'a>,
        addr: std::net::SocketAddr,
    ) -> futures::future::LocalBoxFuture<'a, Result<Self>> {
        async move {
            let sock = realm
                .stream_socket(get_socket2_domain(&addr), fposix_socket::StreamSocketProtocol::Tcp)
                .await
                .context("failed to create socket")?;

            let stream = fuchsia_async::net::TcpStream::connect_from_raw(sock, addr)
                .context("failed to create client tcp stream")?
                .await
                .context("failed to connect to server")?;

            Result::Ok(stream)
        }
        .boxed_local()
    }
}

fn truncate_dropping_front(s: Cow<'_, str>, len: usize) -> Cow<'_, str> {
    match s.len().checked_sub(len) {
        None => s,
        Some(start) => {
            // NB: Drop characters from the front because it's likely that a name that
            // exceeds the length limit is the full name of a test whose suffix is more
            // informative because nesting of test cases appends suffixes.
            match s {
                Cow::Borrowed(s) => Cow::Borrowed(&s[start..]),
                Cow::Owned(mut s) => {
                    let _: std::string::Drain<'_> = s.drain(..start);
                    Cow::Owned(s)
                }
            }
        }
    }
}
