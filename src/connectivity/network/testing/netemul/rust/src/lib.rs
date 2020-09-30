// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs, unreachable_patterns)]

//! Netemul utilities.

use std::convert::TryInto as _;
use std::path::{Path, PathBuf};

use fidl_fuchsia_hardware_ethernet as hw_eth;
use fidl_fuchsia_hardware_network as hw_net;
use fidl_fuchsia_net as net;
use fidl_fuchsia_net_dhcp as net_dhcp;
use fidl_fuchsia_net_interfaces as net_interfaces;
use fidl_fuchsia_net_stack as net_stack;
use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use fidl_fuchsia_netemul_environment as netemul_environment;
use fidl_fuchsia_netemul_network as netemul_network;
use fidl_fuchsia_netemul_sandbox as netemul_sandbox;
use fidl_fuchsia_netstack as netstack;
use fidl_fuchsia_posix_socket as posix_socket;
use fidl_fuchsia_sys as sys;
use fuchsia_zircon as zx;

use anyhow::Context as _;
use futures::future::{FutureExt as _, TryFutureExt as _};

type Result<T = ()> = std::result::Result<T, anyhow::Error>;

/// The default MTU used in in netemul endpoint configurations.
pub const DEFAULT_MTU: u16 = 1500;

/// Abstraction for different endpoint backing types.
pub trait Endpoint: Copy + Clone {
    /// The backing [`EndpointBacking`] for this `Endpoint`.
    ///
    /// [`EndpointBacking`]: netemul_network::EndpointBacking
    const NETEMUL_BACKING: netemul_network::EndpointBacking;

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
    /// [`EndpointConfig`]: netemul_network::EndpointConfig
    fn make_config(mtu: u16, mac: Option<net::MacAddress>) -> netemul_network::EndpointConfig {
        netemul_network::EndpointConfig {
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
    const NETEMUL_BACKING: netemul_network::EndpointBacking =
        netemul_network::EndpointBacking::Ethertap;
    const DEV_PATH: &'static str = "class/ethernet";
}

/// A Network Device implementation of `Endpoint`.
#[derive(Copy, Clone)]
pub enum NetworkDevice {}

impl Endpoint for NetworkDevice {
    const NETEMUL_BACKING: netemul_network::EndpointBacking =
        netemul_network::EndpointBacking::NetworkDevice;
    const DEV_PATH: &'static str = "class/network";
}

/// Helper definition to help type system identify `None` as `IntoIterator`
/// where `Item: Into<netemul_environment::LaunchService`.
pub const NO_SERVICES: Option<netemul_environment::LaunchService> = None;

/// A test sandbox backed by a [`netemul_sandbox::SandboxProxy`].
///
/// `TestSandbox` provides various utility methods to set up network
/// environments for use in testing. The lifetime of the `TestSandbox` is tied
/// to the netemul sandbox itself, dropping it will cause all the created
/// environments, networks, and endpoints to be destroyed.
#[must_use]
pub struct TestSandbox {
    sandbox: netemul_sandbox::SandboxProxy,
}

impl TestSandbox {
    /// Creates a new empty sandbox.
    pub fn new() -> Result<TestSandbox> {
        let sandbox =
            fuchsia_component::client::connect_to_service::<netemul_sandbox::SandboxMarker>()
                .context("failed to connect to sandbox service")?;
        Ok(TestSandbox { sandbox })
    }

    /// Creates an environment with `name` and `services`.
    pub fn create_environment<I>(
        &self,
        name: impl Into<String>,
        services: I,
    ) -> Result<TestEnvironment<'_>>
    where
        I: IntoIterator,
        I::Item: Into<netemul_environment::LaunchService>,
    {
        let (environment, server) =
            fidl::endpoints::create_proxy::<netemul_environment::ManagedEnvironmentMarker>()?;
        let name = name.into();
        let () = self.sandbox.create_environment(
            server,
            netemul_environment::EnvironmentOptions {
                name: Some(name.clone()),
                services: Some(services.into_iter().map(Into::into).collect()),
                devices: None,
                inherit_parent_launch_services: None,
                logger_options: Some(netemul_environment::LoggerOptions {
                    enabled: Some(true),
                    klogs_enabled: None,
                    filter_options: None,
                    syslog_output: Some(true),
                }),
            },
        )?;
        Ok(TestEnvironment { environment, name, _sandbox: self })
    }

    /// Creates an environment with no services.
    pub fn create_empty_environment(&self, name: impl Into<String>) -> Result<TestEnvironment<'_>> {
        self.create_environment(name, NO_SERVICES)
    }

    /// Connects to the sandbox's `NetworkContext`.
    fn get_network_context(&self) -> Result<netemul_network::NetworkContextProxy> {
        let (ctx, server) =
            fidl::endpoints::create_proxy::<netemul_network::NetworkContextMarker>()?;
        let () = self.sandbox.get_network_context(server)?;
        Ok(ctx)
    }

    /// Connects to the sandbox's `NetworkManager`.
    fn get_network_manager(&self) -> Result<netemul_network::NetworkManagerProxy> {
        let ctx = self.get_network_context()?;
        let (network_manager, server) =
            fidl::endpoints::create_proxy::<netemul_network::NetworkManagerMarker>()?;
        let () = ctx.get_network_manager(server)?;
        Ok(network_manager)
    }

    /// Connects to the sandbox's `EndpointManager`.
    fn get_endpoint_manager(&self) -> Result<netemul_network::EndpointManagerProxy> {
        let ctx = self.get_network_context()?;
        let (ep_manager, server) =
            fidl::endpoints::create_proxy::<netemul_network::EndpointManagerMarker>()?;
        let () = ctx.get_endpoint_manager(server)?;
        Ok(ep_manager)
    }

    /// Creates a new empty network with default configurations and `name`.
    pub async fn create_network(&self, name: impl Into<String>) -> Result<TestNetwork<'_>> {
        let name = name.into();
        let netm = self.get_network_manager()?;
        let (status, network) = netm
            .create_network(
                &name,
                netemul_network::NetworkConfig { latency: None, packet_loss: None, reorder: None },
            )
            .await
            .context("create_network FIDL error")?;
        let () = zx::Status::ok(status).context("create_network failed")?;
        let network = network
            .ok_or_else(|| anyhow::anyhow!("create_network didn't return a valid network"))?
            .into_proxy()?;
        Ok(TestNetwork { network, name, sandbox: self })
    }

    /// Creates a new unattached endpoint with default configurations and `name`.
    pub async fn create_endpoint<E, S>(&self, name: S) -> Result<TestEndpoint<'_>>
    where
        S: Into<String>,
        E: Endpoint,
    {
        self.create_endpoint_with(name, E::make_config(DEFAULT_MTU, None)).await
    }

    /// Creates a new unattached endpoint with the provided configuration.
    pub async fn create_endpoint_with(
        &self,
        name: impl Into<String>,
        mut config: netemul_network::EndpointConfig,
    ) -> Result<TestEndpoint<'_>> {
        let name = name.into();
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

/// Interface configuration used by [`TestEnvironment::join_network`].
pub enum InterfaceConfig {
    /// Interface is configured with a static address.
    StaticIp(net::Subnet),
    /// Interface is configured to use DHCP to obtain an address.
    Dhcp,
    /// No address configuration is performed.
    None,
}

/// An environment within a netemul sandbox.
#[must_use]
pub struct TestEnvironment<'a> {
    environment: netemul_environment::ManagedEnvironmentProxy,
    name: String,
    _sandbox: &'a TestSandbox,
}

impl<'a> TestEnvironment<'a> {
    /// Connects to a service within the environment.
    pub fn connect_to_service<S>(&self) -> Result<S::Proxy>
    where
        S: fidl::endpoints::ServiceMarker + fidl::endpoints::DiscoverableService,
    {
        let (proxy, server) = zx::Channel::create()?;
        let () = self.environment.connect_to_service(S::SERVICE_NAME, server)?;
        let proxy = fuchsia_async::Channel::from_channel(proxy)?;
        Ok(<S::Proxy as fidl::endpoints::Proxy>::from_channel(proxy))
    }

    /// Gets this environment's launcher.
    ///
    /// All applications launched within a netemul environment will have their
    /// output (stdout, stderr, syslog) decorated with the environment name.
    pub fn get_launcher(&self) -> Result<sys::LauncherProxy> {
        let (launcher, server) = fidl::endpoints::create_proxy::<sys::LauncherMarker>()
            .context("failed to create launcher proxy")?;
        let () = self.environment.get_launcher(server)?;
        Ok(launcher)
    }

    /// Like [`join_network_with`], but uses default endpoint configurations.
    pub async fn join_network<E, S>(
        &self,
        network: &TestNetwork<'a>,
        ep_name: S,
        config: InterfaceConfig,
    ) -> Result<TestInterface<'a>>
    where
        E: Endpoint,
        S: Into<String>,
    {
        let endpoint =
            network.create_endpoint::<E, _>(ep_name).await.context("failed to create endpoint")?;
        self.install_endpoint(endpoint, config).await
    }

    /// Joins `network` with by creating an endpoint with `ep_config` and
    /// installing it into the environment with `if_config`.
    ///
    /// `join_network_with` is a helper to create a new endpoint `ep_name`
    /// attached to `network` and configure it with `if_config`. Returns a
    /// [`TestInterface`] which is already added to this environment's netstack,
    /// link online, enabled, and configured according to `config`.
    ///
    /// Note that this environment needs a Netstack for this operation to
    /// succeed.
    pub async fn join_network_with(
        &self,
        network: &TestNetwork<'a>,
        ep_name: impl Into<String>,
        ep_config: netemul_network::EndpointConfig,
        if_config: InterfaceConfig,
    ) -> Result<TestInterface<'a>> {
        let endpoint = network
            .create_endpoint_with(ep_name, ep_config)
            .await
            .context("failed to create endpoint")?;
        self.install_endpoint(endpoint, if_config).await
    }

    /// Installs and configures `endpoint` in this enviroment with `config`.
    pub async fn install_endpoint(
        &self,
        endpoint: TestEndpoint<'a>,
        config: InterfaceConfig,
    ) -> Result<TestInterface<'a>> {
        let interface =
            endpoint.into_interface_in_environment(self).await.context("failed to add endpoint")?;
        let () = interface.set_link_up(true).await.context("failed to start endpoint")?;
        let () = match config {
            InterfaceConfig::StaticIp(addr) => {
                interface.add_ip_addr(addr).await.context("failed to add static IP")?
            }
            InterfaceConfig::Dhcp => {
                interface.start_dhcp().await.context("failed to start DHCP")?;
            }
            InterfaceConfig::None => (),
        };
        let () = interface.enable_interface().await.context("failed to enable interface")?;

        // Wait for Netstack to observe interface up so callers can safely
        // assume the state of the world on return.
        let interface_state = self
            .connect_to_service::<net_interfaces::StateMarker>()
            .context("failed to connect to fuchsia.net.interfaces/State")?;
        let () = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
            fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)?,
            &mut fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(interface.id()),
            |properties| {
                if properties.online? {
                    Some(())
                } else {
                    None
                }
            },
        )
        .await
        .context("failed to observe interface up")?;

        Ok(interface)
    }

    /// Adds a device to the environment's virtual device filesystem.
    pub fn add_virtual_device(&self, e: &TestEndpoint<'_>, path: &Path) -> Result {
        let path = path
            .to_str()
            .with_context(|| format!("convert {} to str", path.display()))?
            .to_string();
        let (device, device_server_end) =
            fidl::endpoints::create_endpoints::<netemul_network::DeviceProxy_Marker>()
                .context("create endpoints")?;
        e.get_proxy_(device_server_end).context("get proxy")?;

        self.environment
            .add_device(&mut netemul_environment::VirtualDevice { path, device })
            .context("add device")
    }

    /// Removes a device from the environment's virtual device filesystem.
    pub fn remove_virtual_device(&self, path: &Path) -> Result {
        let path = path.to_str().with_context(|| format!("convert {} to str", path.display()))?;
        self.environment.remove_device(path).context("remove device")
    }

    /// Creates a Datagram [`socket2::Socket`] backed by the implementation of
    /// `fuchsia.posix.socket/Provider` in this environment.
    pub async fn datagram_socket(
        &self,
        domain: fidl_fuchsia_posix_socket::Domain,
        proto: fidl_fuchsia_posix_socket::DatagramSocketProtocol,
    ) -> Result<socket2::Socket> {
        let socket_provider = self
            .connect_to_service::<posix_socket::ProviderMarker>()
            .context("failed to connect to socket provider")?;
        let sock = socket_provider
            .datagram_socket(domain, proto)
            .await
            .context("failed to call socket")?
            .map_err(|e| std::io::Error::from_raw_os_error(e.into_primitive()))
            .context("failed to create socket")?;

        Ok(fdio::create_fd(sock.into()).context("failed to create fd")?)
    }

    /// Creates a Stream [`socket2::Socket`] backed by the implementation of
    /// `fuchsia.posix.socket/Provider` in this environment.
    pub async fn stream_socket(
        &self,
        domain: fidl_fuchsia_posix_socket::Domain,
        proto: fidl_fuchsia_posix_socket::StreamSocketProtocol,
    ) -> Result<socket2::Socket> {
        let socket_provider = self
            .connect_to_service::<posix_socket::ProviderMarker>()
            .context("failed to connect to socket provider")?;
        let sock = socket_provider
            .stream_socket(domain, proto)
            .await
            .context("failed to call socket")?
            .map_err(|e| std::io::Error::from_raw_os_error(e.into_primitive()))
            .context("failed to create socket")?;

        Ok(fdio::create_fd(sock.into()).context("failed to create fd")?)
    }
}

/// A virtual Network.
///
/// `TestNetwork` is a single virtual broadcast domain backed by Netemul.
/// Created through [`TestSandbox::create_network`].
#[must_use]
pub struct TestNetwork<'a> {
    network: netemul_network::NetworkProxy,
    name: String,
    sandbox: &'a TestSandbox,
}

impl<'a> TestNetwork<'a> {
    /// Attaches `ep` to this network.
    pub async fn attach_endpoint(&self, ep: &TestEndpoint<'a>) -> Result<()> {
        let status =
            self.network.attach_endpoint(&ep.name).await.context("attach_endpoint FIDL error")?;
        let () = zx::Status::ok(status).context("attach_endpoint failed")?;
        Ok(())
    }

    /// Creates a new endpoint with `name` attached to this network.
    pub async fn create_endpoint<E, S>(&self, name: S) -> Result<TestEndpoint<'a>>
    where
        E: Endpoint,
        S: Into<String>,
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

    /// Creates a new endpoint with `name` and `config` attached to this
    /// network.
    pub async fn create_endpoint_with(
        &self,
        name: impl Into<String>,
        config: netemul_network::EndpointConfig,
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
            fidl::endpoints::create_proxy::<netemul_network::FakeEndpointMarker>()
                .context("failed to create launcher proxy")?;
        let () = self.network.create_fake_endpoint(server)?;
        return Ok(TestFakeEndpoint { endpoint, _sandbox: self.sandbox });
    }
}

/// A virtual network endpoint backed by Netemul.
#[must_use]
pub struct TestEndpoint<'a> {
    endpoint: netemul_network::EndpointProxy,
    name: String,
    _sandbox: &'a TestSandbox,
}

impl<'a> std::ops::Deref for TestEndpoint<'a> {
    type Target = netemul_network::EndpointProxy;

    fn deref(&self) -> &Self::Target {
        &self.endpoint
    }
}

/// A virtual fake network endpoint backed by Netemul.
#[must_use]
pub struct TestFakeEndpoint<'a> {
    endpoint: netemul_network::FakeEndpointProxy,
    _sandbox: &'a TestSandbox,
}

impl<'a> std::ops::Deref for TestFakeEndpoint<'a> {
    type Target = netemul_network::FakeEndpointProxy;

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

impl<'a> TestEndpoint<'a> {
    /// Gets access to this device's virtual Ethernet device.
    ///
    /// Note that an error is returned if the Endpoint is not a
    /// [`netemul_network::DeviceConnection::Ethernet`].
    pub async fn get_ethernet(&self) -> Result<fidl::endpoints::ClientEnd<hw_eth::DeviceMarker>> {
        match self
            .get_device()
            .await
            .with_context(|| format!("failed to get device connection for {}", self.name))?
        {
            netemul_network::DeviceConnection::Ethernet(e) => Ok(e),
            netemul_network::DeviceConnection::NetworkDevice(_) => {
                Err(anyhow::anyhow!("Endpoint {} is not an Ethernet device", self.name))
            }
        }
    }

    /// Gets access to this device's virtual Network device.
    ///
    /// Note that an error is returned if the Endpoint is not a
    /// [`netemul_network::DeviceConnection::NetworkDevice`].
    pub async fn get_netdevice(
        &self,
    ) -> Result<(
        fidl::endpoints::ClientEnd<hw_net::DeviceMarker>,
        fidl::endpoints::ClientEnd<hw_net::MacAddressingMarker>,
    )> {
        match self
            .get_device()
            .await
            .with_context(|| format!("failed to get device connection for {}", self.name))?
        {
            netemul_network::DeviceConnection::NetworkDevice(n) => {
                Self::connect_netdevice_protocols(n)
            }
            netemul_network::DeviceConnection::Ethernet(_) => {
                Err(anyhow::anyhow!("Endpoint {} is not a Network Device", self.name))
            }
        }
    }

    /// Helper function to retrieve the protocols from a netdevice client end.
    fn connect_netdevice_protocols(
        netdevice: fidl::endpoints::ClientEnd<hw_net::DeviceInstanceMarker>,
    ) -> Result<(
        fidl::endpoints::ClientEnd<hw_net::DeviceMarker>,
        fidl::endpoints::ClientEnd<hw_net::MacAddressingMarker>,
    )> {
        let netdevice: hw_net::DeviceInstanceProxy = netdevice.into_proxy()?;
        let (device, device_server_end) =
            fidl::endpoints::create_endpoints::<hw_net::DeviceMarker>()?;
        let (mac, mac_server_end) =
            fidl::endpoints::create_endpoints::<hw_net::MacAddressingMarker>()?;
        let () = netdevice.get_device(device_server_end)?;
        let () = netdevice.get_mac_addressing(mac_server_end)?;
        Ok((device, mac))
    }

    /// Adds this endpoint to `stack`, returning the interface identifier.
    pub async fn add_to_stack(&self, stack: &net_stack::StackProxy) -> Result<u64> {
        Ok(match self.get_device().await.context("get_device failed")? {
            netemul_network::DeviceConnection::Ethernet(eth) => {
                stack.add_ethernet_interface(&self.name, eth).await.squash_result()?
            }
            netemul_network::DeviceConnection::NetworkDevice(netdevice) => {
                let (device, mac) = Self::connect_netdevice_protocols(netdevice)?;
                stack
                    .add_interface(
                        net_stack::InterfaceConfig { name: None, topopath: None, metric: None },
                        &mut net_stack::DeviceDefinition::Ethernet(
                            net_stack::EthernetDeviceDefinition {
                                network_device: device,
                                mac: mac,
                            },
                        ),
                    )
                    .await
                    .squash_result()?
            }
        })
    }

    /// Consumes this `TestEndpoint` and tries to add it to the Netstack in
    /// `environment`, returning a [`TestInterface`] on success.
    pub async fn into_interface_in_environment(
        self,
        environment: &TestEnvironment<'a>,
    ) -> Result<TestInterface<'a>> {
        let stack = environment.connect_to_service::<net_stack::StackMarker>()?;
        let netstack = environment.connect_to_service::<netstack::NetstackMarker>()?;
        let id = self.add_to_stack(&stack).await.with_context(|| {
            format!("failed to add {} to environment {}", self.name, environment.name)
        })?;
        Ok(TestInterface { endpoint: self, id, stack, netstack })
    }
}

/// A [`TestEndpoint`] that is installed in an environment's Netstack.
#[must_use]
pub struct TestInterface<'a> {
    endpoint: TestEndpoint<'a>,
    id: u64,
    stack: net_stack::StackProxy,
    netstack: netstack::NetstackProxy,
}

impl<'a> std::ops::Deref for TestInterface<'a> {
    type Target = netemul_network::EndpointProxy;

    fn deref(&self) -> &Self::Target {
        &self.endpoint
    }
}

impl<'a> TestInterface<'a> {
    /// Gets the interface identifier.
    pub fn id(&self) -> u64 {
        self.id
    }

    /// Enable interface.
    ///
    /// Equivalent to `stack.enable_interface(test_interface.id())`.
    pub async fn enable_interface(&self) -> Result<()> {
        self.stack.enable_interface(self.id).await.squash_result().with_context(|| {
            format!("stack.enable_interface for endpoint {} failed", self.endpoint.name)
        })
    }

    /// Add interface address.
    ///
    /// Equivalent to `stack.add_interface_address(test_interface.id(), &mut addr)`.
    pub async fn add_ip_addr(&self, mut addr: net::Subnet) -> Result<()> {
        self.stack.add_interface_address(self.id, &mut addr).await.squash_result().with_context(
            || {
                format!(
                    "stack.add_interface_address({}, {:?}) for endpoint {} failed",
                    self.id, addr, self.endpoint.name
                )
            },
        )
    }

    /// Gets the interface's info.
    pub async fn get_info(&self) -> Result<net_stack::InterfaceInfo> {
        self.stack.get_interface_info(self.id).await.squash_result().with_context(|| {
            format!(
                "stack.get_interface_info({}) for endpoint {} failed",
                self.id, self.endpoint.name
            )
        })
    }

    /// Gets the interface's addresses.
    pub async fn get_addrs(&self) -> Result<Vec<net::Subnet>> {
        Ok(self.get_info().await?.properties.addresses)
    }

    /// Starts DHCP on this interface.
    pub async fn start_dhcp(&self) -> Result<()> {
        let (dhcp_client, server_end) =
            fidl::endpoints::create_proxy::<net_dhcp::ClientMarker>()
                .context("failed to create endpoints for fuchsia.net.dhcp.Client")?;

        let () = self
            .netstack
            .get_dhcp_client(
                self.id.try_into().with_context(|| {
                    format!("interface ID should fit in a u32; ID = {}", self.id)
                })?,
                server_end,
            )
            .await
            .context("failed to call netstack.get_dhcp_client")?
            .map_err(zx::Status::from_raw)
            .context("failed to get dhcp client")?;

        let () = dhcp_client
            .start()
            .await
            .context("failed to call dhcp_client.start")?
            .map_err(zx::Status::from_raw)
            .context("failed to start dhcp client")?;

        Ok(())
    }
}

/// Get the [`socket2::Domain`] for `addr`.
fn get_socket2_domain(addr: &std::net::SocketAddr) -> fidl_fuchsia_posix_socket::Domain {
    let domain = match addr {
        std::net::SocketAddr::V4(_) => fidl_fuchsia_posix_socket::Domain::Ipv4,
        std::net::SocketAddr::V6(_) => fidl_fuchsia_posix_socket::Domain::Ipv6,
    };

    domain
}

/// Trait describing UDP sockets that can be bound in a testing environment.
pub trait EnvironmentUdpSocket: Sized {
    /// Creates a UDP socket in `env` bound to `addr`.
    fn bind_in_env<'a>(
        env: &'a TestEnvironment<'a>,
        addr: std::net::SocketAddr,
    ) -> futures::future::LocalBoxFuture<'a, Result<Self>>;
}

impl EnvironmentUdpSocket for std::net::UdpSocket {
    fn bind_in_env<'a>(
        env: &'a TestEnvironment<'a>,
        addr: std::net::SocketAddr,
    ) -> futures::future::LocalBoxFuture<'a, Result<Self>> {
        async move {
            let sock = env
                .datagram_socket(
                    get_socket2_domain(&addr),
                    fidl_fuchsia_posix_socket::DatagramSocketProtocol::Udp,
                )
                .await
                .context("failed to create socket")?;

            let () = sock.bind(&addr.into()).context("bind failed")?;

            Result::Ok(sock.into_udp_socket())
        }
        .boxed_local()
    }
}

impl EnvironmentUdpSocket for fuchsia_async::net::UdpSocket {
    fn bind_in_env<'a>(
        env: &'a TestEnvironment<'a>,
        addr: std::net::SocketAddr,
    ) -> futures::future::LocalBoxFuture<'a, Result<Self>> {
        std::net::UdpSocket::bind_in_env(env, addr)
            .and_then(|udp| {
                futures::future::ready(
                    fuchsia_async::net::UdpSocket::from_socket(udp)
                        .context("failed to create fuchsia_async socket"),
                )
            })
            .boxed_local()
    }
}

/// Trait describing TCP listeners bound in a testing environment.
pub trait EnvironmentTcpListener: Sized {
    /// Creates a TCP listener in `env` bound to `addr`.
    fn listen_in_env<'a>(
        env: &'a TestEnvironment<'a>,
        addr: std::net::SocketAddr,
    ) -> futures::future::LocalBoxFuture<'a, Result<Self>>;
}

impl EnvironmentTcpListener for std::net::TcpListener {
    fn listen_in_env<'a>(
        env: &'a TestEnvironment<'a>,
        addr: std::net::SocketAddr,
    ) -> futures::future::LocalBoxFuture<'a, Result<Self>> {
        async move {
            let sock = env
                .stream_socket(
                    get_socket2_domain(&addr),
                    fidl_fuchsia_posix_socket::StreamSocketProtocol::Tcp,
                )
                .await
                .context("failed to create server socket")?;

            let () = sock.bind(&addr.into()).context("failed to bind server socket")?;
            // Use 128 for the listen() backlog, same as the original implementation of TcpListener
            // in Rust std (see https://doc.rust-lang.org/src/std/sys_common/net.rs.html#386).
            let () = sock.listen(128).context("failed to listen on server socket")?;

            Result::Ok(sock.into_tcp_listener())
        }
        .boxed_local()
    }
}

impl EnvironmentTcpListener for fuchsia_async::net::TcpListener {
    fn listen_in_env<'a>(
        env: &'a TestEnvironment<'a>,
        addr: std::net::SocketAddr,
    ) -> futures::future::LocalBoxFuture<'a, Result<Self>> {
        std::net::TcpListener::listen_in_env(env, addr)
            .and_then(|listener| {
                futures::future::ready(
                    fuchsia_async::net::TcpListener::from_std(listener)
                        .context("failed to create fuchsia_async socket"),
                )
            })
            .boxed_local()
    }
}

/// Trait describing TCP streams in a testing environment.
pub trait EnvironmentTcpStream: Sized {
    /// Creates a TCP stream in `env` connected to `addr`.
    fn connect_in_env<'a>(
        env: &'a TestEnvironment<'a>,
        addr: std::net::SocketAddr,
    ) -> futures::future::LocalBoxFuture<'a, Result<Self>>;

    // TODO: Implement this trait for std::net::TcpStream.
}

impl EnvironmentTcpStream for fuchsia_async::net::TcpStream {
    fn connect_in_env<'a>(
        env: &'a TestEnvironment<'a>,
        addr: std::net::SocketAddr,
    ) -> futures::future::LocalBoxFuture<'a, Result<Self>> {
        async move {
            let sock = env
                .stream_socket(
                    get_socket2_domain(&addr),
                    fidl_fuchsia_posix_socket::StreamSocketProtocol::Tcp,
                )
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
