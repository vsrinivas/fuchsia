// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryInto as _;
use std::path::{Path, PathBuf};

use fidl_fuchsia_net as net;
use fidl_fuchsia_net_filter;
use fidl_fuchsia_net_stack as net_stack;
use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use fidl_fuchsia_netemul_environment as netemul_environment;
use fidl_fuchsia_netemul_network as netemul_network;
use fidl_fuchsia_netemul_sandbox as netemul_sandbox;
use fuchsia_zircon as zx;

use anyhow::Context as _;
use futures::{FutureExt as _, TryFutureExt as _};

use crate::Result;

/// Helper definition to help type system identify `None` as `IntoIterator`
/// where `Item: Into<netemul_environment::LaunchService`.
const NO_SERVICES: Option<netemul_environment::LaunchService> = None;

/// The Netstack version. Used to specify which Netstack version to use in a
/// Netstack-served [`KnownServices`].
#[derive(Copy, Clone, Eq, PartialEq, Debug)]
pub enum NetstackVersion {
    Netstack2,
    Netstack3,
}

impl NetstackVersion {
    /// Gets the Fuchsia URL for this Netstack component.
    pub fn get_url(&self) -> &'static str {
        match self {
            NetstackVersion::Netstack2 => {
                "fuchsia-pkg://fuchsia.com/netstack#meta/netstack_debug.cmx"
            }
            NetstackVersion::Netstack3 => {
                "fuchsia-pkg://fuchsia.com/netstack-integration-tests#meta/netstack3.cmx"
            }
        }
    }

    pub fn get_name(&self) -> &'static str {
        match self {
            NetstackVersion::Netstack2 => "ns2",
            NetstackVersion::Netstack3 => "ns3",
        }
    }
}

/// Known services used in tests.
#[derive(Copy, Clone, Eq, PartialEq, Debug)]
pub enum KnownServices {
    Stack(NetstackVersion),
    Netstack(NetstackVersion),
    SocketProvider(NetstackVersion),
    Filter(NetstackVersion),
    Stash,
    MockCobalt,
    SecureStash,
    DhcpServer,
    LookupAdmin,
}

impl KnownServices {
    /// Gets the service name and its Fuchsia package URL.
    pub fn get_name_url(&self) -> (&'static str, &'static str) {
        match self {
            KnownServices::Stack(v) => (<net_stack::StackMarker as fidl::endpoints::DiscoverableService>::SERVICE_NAME,
                                        v.get_url()),
            KnownServices::MockCobalt => (<fidl_fuchsia_cobalt::LoggerFactoryMarker as fidl::endpoints::DiscoverableService>::SERVICE_NAME,
                                          fuchsia_component::fuchsia_single_component_package_url!("mock_cobalt")),
            KnownServices::Netstack(v) => (<fidl_fuchsia_netstack::NetstackMarker as fidl::endpoints::DiscoverableService>::SERVICE_NAME,
                                           v.get_url()),
            KnownServices::Stash => (
                <fidl_fuchsia_stash::StoreMarker as fidl::endpoints::DiscoverableService>::SERVICE_NAME,
                fuchsia_component::fuchsia_single_component_package_url!("stash")),
            KnownServices::SocketProvider(v) => (<fidl_fuchsia_posix_socket::ProviderMarker as fidl::endpoints::DiscoverableService>::SERVICE_NAME,
                                                 v.get_url()),
            KnownServices::Filter(v) => (<fidl_fuchsia_net_filter::FilterMarker as fidl::endpoints::DiscoverableService>::SERVICE_NAME,
                                                 v.get_url()),
            KnownServices::SecureStash => (<fidl_fuchsia_stash::SecureStoreMarker as fidl::endpoints::DiscoverableService>::SERVICE_NAME,
                                           "fuchsia-pkg://fuchsia.com/stash#meta/stash_secure.cmx"),
            KnownServices::DhcpServer => (<fidl_fuchsia_net_dhcp::Server_Marker as fidl::endpoints::DiscoverableService>::SERVICE_NAME,
                                          "fuchsia-pkg://fuchsia.com/netstack-integration-tests#meta/dhcpd.cmx"),
            KnownServices::LookupAdmin => (<fidl_fuchsia_net_name::LookupAdminMarker as fidl::endpoints::DiscoverableService>::SERVICE_NAME,
                                            "fuchsia-pkg://fuchsia.com/netstack-integration-tests#meta/dns-resolver.cmx")
        }
    }

    /// Gets the service name.
    pub fn get_name(&self) -> &'static str {
        self.get_name_url().0
    }

    /// Gets the service URL.
    pub fn get_url(&self) -> &'static str {
        self.get_name_url().1
    }

    /// Transforms into a [`netemul_environment::LaunchService`] with no
    /// arguments.
    pub fn into_launch_service(self) -> netemul_environment::LaunchService {
        self.into_launch_service_with_arguments::<Option<String>>(None)
    }

    /// Transforms into a [`netemul_environment::LaunchService`] with no
    /// arguments with `args`.
    pub fn into_launch_service_with_arguments<I>(
        self,
        args: I,
    ) -> netemul_environment::LaunchService
    where
        I: IntoIterator,
        I::Item: Into<String>,
    {
        let (name, url) = self.get_name_url();
        netemul_environment::LaunchService {
            name: name.to_string(),
            url: url.to_string(),
            arguments: Some(args.into_iter().map(Into::into).collect()),
        }
    }
}

impl<'a> From<&'a KnownServices> for netemul_environment::LaunchService {
    fn from(s: &'a KnownServices) -> Self {
        s.into_launch_service()
    }
}

/// Abstraction for a Fuchsia component which offers network stack services.
pub trait Netstack: Copy + Clone {
    /// The Netstack version.
    const VERSION: NetstackVersion;
}

/// Uninstantiable type that represents Netstack2's implementation of a
/// network stack.
#[derive(Copy, Clone)]
pub enum Netstack2 {}

impl Netstack for Netstack2 {
    const VERSION: NetstackVersion = NetstackVersion::Netstack2;
}

/// Uninstantiable type that represents Netstack3's implementation of a
/// network stack.
#[derive(Copy, Clone)]
pub enum Netstack3 {}

impl Netstack for Netstack3 {
    const VERSION: NetstackVersion = NetstackVersion::Netstack3;
}

/// Abstraction for a Fuchsia component which offers network configuration services.
pub trait Manager: Copy + Clone {
    /// The Fuchsia package URL to the component.
    const PKG_URL: &'static str;

    /// Default arguments that should be passed to the component when run under a test
    /// environment.
    const TESTING_ARGS: &'static [&'static str];

    /// Returns `TESTING_ARGS` as a type that [`fuchsia_component::client::launch`]
    /// accepts.
    fn testing_args() -> Option<Vec<String>> {
        Some(Self::TESTING_ARGS.iter().cloned().map(String::from).collect())
    }
}

/// Uninstantiable type that represents NetCfg's implementation of a network manager.
#[derive(Copy, Clone)]
pub enum NetCfg {}

impl Manager for NetCfg {
    // Note, netcfg.cmx must never be used in a Netemul environment as it breaks
    // hermeticity.
    const PKG_URL: &'static str =
        "fuchsia-pkg://fuchsia.com/netstack-integration-tests#meta/netcfg-netemul.cmx";
    // Specify an empty config file for NetCfg when it is run in netemul.
    const TESTING_ARGS: &'static [&'static str] = &["--config-data", "netcfg/empty.json"];
}

/// Uninstantiable type that represents NetworkManager's implementation of a network manager.
#[derive(Copy, Clone)]
pub enum NetworkManager {}

impl Manager for NetworkManager {
    // Note, network-manager.cmx must never be used in a Netemul environment as it breaks
    // hermeticity.
    const PKG_URL: &'static str =
        "fuchsia-pkg://fuchsia.com/netstack-integration-tests#meta/network-manager-netemul.cmx";
    const TESTING_ARGS: &'static [&'static str] = &[];
}

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

/// Abstraction for different endpoint backing types.
pub trait Endpoint: Copy + Clone {
    const NETEMUL_BACKING: netemul_network::EndpointBacking;

    /// The relative path from the root device directory where devices of this `Endpoint`
    /// can be discovered.
    const DEV_PATH: &'static str;

    /// Returns the path to an endpoint with the given name in this `Endpoint`'s
    /// device directory.
    fn dev_path(name: &str) -> PathBuf {
        Path::new(Self::DEV_PATH).join(name)
    }
}

#[derive(Copy, Clone)]
pub enum Ethernet {}

impl Endpoint for Ethernet {
    const NETEMUL_BACKING: netemul_network::EndpointBacking =
        netemul_network::EndpointBacking::Ethertap;
    const DEV_PATH: &'static str = "class/ethernet";
}

#[derive(Copy, Clone)]
pub enum NetworkDevice {}

impl Endpoint for NetworkDevice {
    const NETEMUL_BACKING: netemul_network::EndpointBacking =
        netemul_network::EndpointBacking::NetworkDevice;
    const DEV_PATH: &'static str = "class/network";
}

impl TestSandbox {
    /// Creates a new empty sandbox.
    pub fn new() -> Result<TestSandbox> {
        let sandbox = fuchsia_component::client::connect_to_service::<
            fidl_fuchsia_netemul_sandbox::SandboxMarker,
        >()
        .context("failed to connect to sandbox service")?;
        Ok(TestSandbox { sandbox })
    }

    /// Creates an environment with `name` and `services`.
    ///
    /// To create an environment with Netstack services see
    /// [`TestSandbox::create_netstack_environment`].
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

    /// Creates an environment with Netstack services.
    pub fn create_netstack_environment<N, S>(&self, name: S) -> Result<TestEnvironment<'_>>
    where
        N: Netstack,
        S: Into<String>,
    {
        self.create_netstack_environment_with::<N, _, _>(name, NO_SERVICES)
    }

    /// Creates an environment with the base Netstack services plus additional
    /// ones in `services`.
    pub fn create_netstack_environment_with<N, S, I>(
        &self,
        name: S,
        services: I,
    ) -> Result<TestEnvironment<'_>>
    where
        S: Into<String>,
        N: Netstack,
        I: IntoIterator,
        I::Item: Into<netemul_environment::LaunchService>,
    {
        self.create_environment(
            name,
            [
                KnownServices::Filter(N::VERSION),
                KnownServices::Stack(N::VERSION),
                KnownServices::Netstack(N::VERSION),
                KnownServices::SocketProvider(N::VERSION),
                KnownServices::MockCobalt,
                KnownServices::Stash,
            ]
            .iter()
            .map(netemul_environment::LaunchService::from)
            .chain(services.into_iter().map(Into::into)),
        )
    }

    /// Connects to the sandbox's NetworkContext.
    fn get_network_context(&self) -> Result<netemul_network::NetworkContextProxy> {
        let (ctx, server) =
            fidl::endpoints::create_proxy::<netemul_network::NetworkContextMarker>()?;
        let () = self.sandbox.get_network_context(server)?;
        Ok(ctx)
    }

    /// Connects to the sandbox's NetworkManager.
    fn get_network_manager(&self) -> Result<netemul_network::NetworkManagerProxy> {
        let ctx = self.get_network_context()?;
        let (network_manager, server) =
            fidl::endpoints::create_proxy::<netemul_network::NetworkManagerMarker>()?;
        let () = ctx.get_network_manager(server)?;
        Ok(network_manager)
    }

    /// Connects to the sandbox's EndpointManager.
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
        self.create_endpoint_with(
            name,
            fidl_fuchsia_netemul_network::EndpointConfig {
                mtu: 1500,
                mac: None,
                backing: E::NETEMUL_BACKING,
            },
        )
        .await
    }

    /// Creates a new unattached endpoint with the provided configuration.
    pub async fn create_endpoint_with(
        &self,
        name: impl Into<String>,
        mut config: fidl_fuchsia_netemul_network::EndpointConfig,
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

    /// Helper function to create a new Netstack environment and connect to a
    /// netstack service on it.
    pub fn new_netstack<N, S, T>(&self, name: T) -> Result<(TestEnvironment<'_>, S::Proxy)>
    where
        N: Netstack,
        S: fidl::endpoints::ServiceMarker + fidl::endpoints::DiscoverableService,
        T: Into<String>,
    {
        let env = self
            .create_netstack_environment::<N, _>(name)
            .context("failed to create test environment")?;
        let netstack_proxy =
            env.connect_to_service::<S>().context("failed to connect to netstack")?;
        Ok((env, netstack_proxy))
    }

    /// Helper function to create a new Netstack environment and a new
    /// unattached endpoint.
    pub async fn new_netstack_and_device<N, E, S, T>(
        &self,
        name: T,
    ) -> Result<(TestEnvironment<'_>, S::Proxy, TestEndpoint<'_>)>
    where
        N: Netstack,
        E: Endpoint,
        S: fidl::endpoints::ServiceMarker + fidl::endpoints::DiscoverableService,
        T: Into<String> + Copy,
    {
        let (env, stack) = self.new_netstack::<N, S, _>(name)?;
        let endpoint =
            self.create_endpoint::<E, _>(name).await.context("failed to create endpoint")?;
        Ok((env, stack, endpoint))
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
    pub fn get_launcher(&self) -> Result<fidl_fuchsia_sys::LauncherProxy> {
        let (launcher, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_sys::LauncherMarker>()
                .context("failed to create launcher proxy")?;
        let () = self.environment.get_launcher(server)?;
        Ok(launcher)
    }

    /// Joins `network` with `config`.
    ///
    /// `join_network` is a helper to create a new endpoint `ep_name` attached
    /// to `network` and configure it with `config`. Returns a [`TestInterface`]
    /// which is already added to this environment's netstack, link
    /// online, enabled,  and configured according to `config`.
    ///
    /// Note that this environment needs a Netstack for this operation to
    /// succeed. See [`TestSandbox::create_netstack_environment`] to create an
    /// environment with all the Netstack services.
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

    /// Creates a [`socket2::Socket`] backed by the implementation of
    /// `fuchsia.posix.socket/Provider` in this environment.
    pub async fn socket(
        &self,
        domain: socket2::Domain,
        type_: socket2::Type,
        protocol: Option<socket2::Protocol>,
    ) -> Result<socket2::Socket> {
        let socket_provider = self
            .connect_to_service::<fidl_fuchsia_posix_socket::ProviderMarker>()
            .context("failed to connect to socket provider")?;
        let sock = socket_provider
            .socket2(
                libc::c_int::from(domain).try_into().context("invalid domain")?,
                libc::c_int::from(type_).try_into().context("invalid type")?,
                protocol
                    .map(libc::c_int::from)
                    .unwrap_or(0)
                    .try_into()
                    .context("invalid protocol")?,
            )
            .await
            .context("failed to call socket")?
            .map_err(std::io::Error::from_raw_os_error)
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
        Ok(zx::Status::ok(
            self.network.attach_endpoint(&ep.name).await.context("attach_endpoint FIDL error")?,
        )
        .context("attach_endpoint failed")?)
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
    pub fn frame_stream(
        &self,
    ) -> impl futures::Stream<Item = std::result::Result<(Vec<u8>, u64), fidl::Error>> + '_ {
        futures::stream::try_unfold(&self.endpoint, |ep| ep.read().map_ok(move |r| Some((r, ep))))
    }
}

impl<'a> TestEndpoint<'a> {
    /// Gets access to this device's virtual Ethernet device.
    ///
    /// Note that an error is returned if the Endpoint is a
    /// [`netemul_network::DeviceConnection::NetworkDevice`].
    pub async fn get_ethernet(
        &self,
    ) -> Result<fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_ethernet::DeviceMarker>> {
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

    /// Adds this endpoint to `stack`, returning the interface identifier.
    pub async fn add_to_stack(&self, stack: &net_stack::StackProxy) -> Result<u64> {
        Ok(match self.get_device().await.context("get_device failed")? {
            netemul_network::DeviceConnection::Ethernet(eth) => {
                stack.add_ethernet_interface(&self.name, eth).await.squash_result()?
            }
            netemul_network::DeviceConnection::NetworkDevice(netdevice) => {
                let netdevice: fidl_fuchsia_hardware_network::DeviceInstanceProxy =
                    netdevice.into_proxy()?;
                let (device, device_server_end) = fidl::endpoints::create_endpoints::<
                    fidl_fuchsia_hardware_network::DeviceMarker,
                >()?;
                let (mac, mac_server_end) = fidl::endpoints::create_endpoints::<
                    fidl_fuchsia_hardware_network::MacAddressingMarker,
                >()?;
                let () = netdevice.get_device(device_server_end)?;
                let () = netdevice.get_mac_addressing(mac_server_end)?;
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
        let netstack = environment.connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>()?;
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
    netstack: fidl_fuchsia_netstack::NetstackProxy,
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
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_dhcp::ClientMarker>()
                .context("failed to create endpoints for fuchsia.net.dhcp.Client")?;

        let () = self
            .netstack
            .get_dhcp_client(self.id.try_into().expect("should fit"), server_end)
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
            let domain = match &addr {
                std::net::SocketAddr::V4(_) => socket2::Domain::ipv4(),
                std::net::SocketAddr::V6(_) => socket2::Domain::ipv6(),
            };
            let sock = env
                .socket(domain, socket2::Type::dgram(), None)
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
