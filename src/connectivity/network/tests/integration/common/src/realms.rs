// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides utilities for test realms.

use async_trait::async_trait;
use fidl::endpoints::DiscoverableProtocolMarker as _;
use fidl_fuchsia_net_debug as fnet_debug;
use fidl_fuchsia_net_dhcp as fnet_dhcp;
use fidl_fuchsia_net_dhcpv6 as fnet_dhcpv6;
use fidl_fuchsia_net_filter as fnet_filter;
use fidl_fuchsia_net_interfaces as fnet_interfaces;
use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use fidl_fuchsia_net_name as fnet_name;
use fidl_fuchsia_net_neighbor as fnet_neighbor;
use fidl_fuchsia_net_routes as fnet_routes;
use fidl_fuchsia_net_stack as fnet_stack;
use fidl_fuchsia_netemul as fnetemul;
use fidl_fuchsia_netstack as fnetstack;
use fidl_fuchsia_posix_socket as fposix_socket;
use fidl_fuchsia_stash as fstash;

use crate::Result;

/// The Netstack version. Used to specify which Netstack version to use in a
/// [`KnownServiceProvider::Netstack`].
#[derive(Copy, Clone, Eq, PartialEq, Debug)]
#[allow(missing_docs)]
pub enum NetstackVersion {
    Netstack2,
    Netstack3,
}

impl NetstackVersion {
    /// Gets the Fuchsia URL for this Netstack component.
    pub fn get_url(&self) -> &'static str {
        match self {
            NetstackVersion::Netstack2 => "#meta/netstack-debug.cm",
            NetstackVersion::Netstack3 => "#meta/netstack3.cm",
        }
    }

    /// Gets the services exposed by this Netstack component.
    pub fn get_services(&self) -> &[&'static str] {
        match self {
            NetstackVersion::Netstack2 => &[
                fnet_filter::FilterMarker::PROTOCOL_NAME,
                fnet_interfaces_admin::InstallerMarker::PROTOCOL_NAME,
                fnet_interfaces::StateMarker::PROTOCOL_NAME,
                fnet_debug::InterfacesMarker::PROTOCOL_NAME,
                fnet_neighbor::ControllerMarker::PROTOCOL_NAME,
                fnet_neighbor::ViewMarker::PROTOCOL_NAME,
                fnet_routes::StateMarker::PROTOCOL_NAME,
                fnet_stack::LogMarker::PROTOCOL_NAME,
                fnet_stack::StackMarker::PROTOCOL_NAME,
                fnetstack::NetstackMarker::PROTOCOL_NAME,
                fposix_socket::ProviderMarker::PROTOCOL_NAME,
            ],
            NetstackVersion::Netstack3 => &[
                fnet_interfaces::StateMarker::PROTOCOL_NAME,
                fnet_stack::StackMarker::PROTOCOL_NAME,
                fposix_socket::ProviderMarker::PROTOCOL_NAME,
            ],
        }
    }
}

/// The network manager to use in a [`KnownServiceProvider::Manager`].
#[derive(Copy, Clone, Eq, PartialEq, Debug)]
#[allow(missing_docs)]
pub enum ManagementAgent {
    NetCfg,
}

impl ManagementAgent {
    /// Gets the URL for this network manager component.
    pub fn get_url(&self) -> &'static str {
        match self {
            ManagementAgent::NetCfg => "#meta/netcfg.cm",
        }
    }

    /// Default arguments that should be passed to the component when run in a
    /// test realm.
    pub fn get_program_args(&self) -> &[&'static str] {
        &[
            "--min-severity",
            "DEBUG",
            "--allow-virtual-devices",
            "--config-data",
            "/pkg/netcfg/empty.json",
        ]
    }
}

/// Components that provide known services used in tests.
#[derive(Clone, Eq, PartialEq, Debug)]
#[allow(missing_docs)]
pub enum KnownServiceProvider {
    Netstack(NetstackVersion),
    Manager(ManagementAgent),
    SecureStash,
    DhcpServer { persistent: bool },
    Dhcpv6Client,
    DnsResolver,
    Reachability,
}

/// Constant properties of components used in networking integration tests, such
/// as monikers and URLs.
#[allow(missing_docs)]
pub mod constants {
    pub mod netstack {
        pub const COMPONENT_NAME: &str = "netstack";
    }
    pub mod netcfg {
        pub const COMPONENT_NAME: &str = "netcfg";
        // These capability names and filepaths should match the devfs capabilities used by netcfg
        // in its component manifest, i.e. netcfg.cml.
        pub const DEV_CLASS_ETHERNET: &str = "dev-class-ethernet";
        pub const CLASS_ETHERNET_PATH: &str = "class/ethernet";
        pub const DEV_CLASS_NETWORK: &str = "dev-class-network";
        pub const CLASS_NETWORK_PATH: &str = "class/network";
    }
    pub mod secure_stash {
        pub const COMPONENT_NAME: &str = "stash_secure";
        pub const COMPONENT_URL: &str = "#meta/stash_secure.cm";
    }
    pub mod dhcp_server {
        pub const COMPONENT_NAME: &str = "dhcpd";
        pub const COMPONENT_URL: &str = "#meta/dhcpd.cm";
    }
    pub mod dhcpv6_client {
        pub const COMPONENT_NAME: &str = "dhcpv6-client";
        pub const COMPONENT_URL: &str = "#meta/dhcpv6-client.cm";
    }
    pub mod dns_resolver {
        pub const COMPONENT_NAME: &str = "dns_resolver";
        pub const COMPONENT_URL: &str = "#meta/dns_resolver.cm";
    }
    pub mod reachability {
        pub const COMPONENT_NAME: &str = "reachability";
        pub const COMPONENT_URL: &str = "#meta/reachability.cm";
    }
}

fn use_log_sink() -> Option<fnetemul::ChildUses> {
    Some(fnetemul::ChildUses::Capabilities(vec![fnetemul::Capability::LogSink(fnetemul::Empty {})]))
}

fn protocol_dep<P>(component_name: &'static str) -> fnetemul::ChildDep
where
    P: fidl::endpoints::DiscoverableProtocolMarker,
{
    fnetemul::ChildDep {
        name: Some(component_name.into()),
        capability: Some(fnetemul::ExposedCapability::Protocol(P::PROTOCOL_NAME.to_string())),
        ..fnetemul::ChildDep::EMPTY
    }
}

impl<'a> From<&'a KnownServiceProvider> for fnetemul::ChildDef {
    fn from(s: &'a KnownServiceProvider) -> Self {
        match s {
            KnownServiceProvider::Netstack(version) => fnetemul::ChildDef {
                name: Some(constants::netstack::COMPONENT_NAME.to_string()),
                url: Some(version.get_url().to_string()),
                exposes: Some(
                    version.get_services().iter().map(|service| service.to_string()).collect(),
                ),
                uses: use_log_sink(),
                ..fnetemul::ChildDef::EMPTY
            },
            KnownServiceProvider::Manager(management_agent) => fnetemul::ChildDef {
                name: Some(constants::netcfg::COMPONENT_NAME.to_string()),
                url: Some(management_agent.get_url().to_string()),
                program_args: Some(
                    management_agent.get_program_args().iter().cloned().map(Into::into).collect(),
                ),
                uses: Some(fnetemul::ChildUses::Capabilities(vec![
                    fnetemul::Capability::LogSink(fnetemul::Empty {}),
                    fnetemul::Capability::ChildDep(protocol_dep::<fnet_filter::FilterMarker>(
                        constants::netstack::COMPONENT_NAME,
                    )),
                    fnetemul::Capability::ChildDep(protocol_dep::<fnet_interfaces::StateMarker>(
                        constants::netstack::COMPONENT_NAME,
                    )),
                    fnetemul::Capability::ChildDep(protocol_dep::<fnet_stack::StackMarker>(
                        constants::netstack::COMPONENT_NAME,
                    )),
                    fnetemul::Capability::ChildDep(protocol_dep::<fnetstack::NetstackMarker>(
                        constants::netstack::COMPONENT_NAME,
                    )),
                    fnetemul::Capability::ChildDep(protocol_dep::<fnet_dhcp::Server_Marker>(
                        constants::dhcp_server::COMPONENT_NAME,
                    )),
                    fnetemul::Capability::ChildDep(
                        protocol_dep::<fnet_dhcpv6::ClientProviderMarker>(
                            constants::dhcpv6_client::COMPONENT_NAME,
                        ),
                    ),
                    fnetemul::Capability::ChildDep(protocol_dep::<fnet_name::LookupAdminMarker>(
                        constants::dns_resolver::COMPONENT_NAME,
                    )),
                    fnetemul::Capability::NetemulDevfs(fnetemul::DevfsDep {
                        name: Some(constants::netcfg::DEV_CLASS_ETHERNET.to_string()),
                        subdir: Some(constants::netcfg::CLASS_ETHERNET_PATH.to_string()),
                        ..fnetemul::DevfsDep::EMPTY
                    }),
                    fnetemul::Capability::NetemulDevfs(fnetemul::DevfsDep {
                        name: Some(constants::netcfg::DEV_CLASS_NETWORK.to_string()),
                        subdir: Some(constants::netcfg::CLASS_NETWORK_PATH.to_string()),
                        ..fnetemul::DevfsDep::EMPTY
                    }),
                    fnetemul::Capability::StorageDep(fnetemul::StorageDep {
                        variant: Some(fnetemul::StorageVariant::Data),
                        path: Some("/data".to_string()),
                        ..fnetemul::StorageDep::EMPTY
                    }),
                ])),
                eager: Some(true),
                ..fnetemul::ChildDef::EMPTY
            },
            KnownServiceProvider::SecureStash => fnetemul::ChildDef {
                name: Some(constants::secure_stash::COMPONENT_NAME.to_string()),
                url: Some(constants::secure_stash::COMPONENT_URL.to_string()),
                exposes: Some(vec![fstash::SecureStoreMarker::PROTOCOL_NAME.to_string()]),
                uses: Some(fnetemul::ChildUses::Capabilities(vec![
                    fnetemul::Capability::LogSink(fnetemul::Empty {}),
                    fnetemul::Capability::StorageDep(fnetemul::StorageDep {
                        variant: Some(fnetemul::StorageVariant::Data),
                        path: Some("/data".to_string()),
                        ..fnetemul::StorageDep::EMPTY
                    }),
                ])),
                ..fnetemul::ChildDef::EMPTY
            },
            KnownServiceProvider::DhcpServer { persistent } => fnetemul::ChildDef {
                name: Some(constants::dhcp_server::COMPONENT_NAME.to_string()),
                url: Some(constants::dhcp_server::COMPONENT_URL.to_string()),
                exposes: Some(vec![fnet_dhcp::Server_Marker::PROTOCOL_NAME.to_string()]),
                uses: Some(fnetemul::ChildUses::Capabilities(vec![
                    fnetemul::Capability::LogSink(fnetemul::Empty {}),
                    fnetemul::Capability::ChildDep(protocol_dep::<fnet_name::LookupMarker>(
                        constants::dns_resolver::COMPONENT_NAME,
                    )),
                    fnetemul::Capability::ChildDep(
                        protocol_dep::<fnet_neighbor::ControllerMarker>(
                            constants::netstack::COMPONENT_NAME,
                        ),
                    ),
                    fnetemul::Capability::ChildDep(protocol_dep::<fposix_socket::ProviderMarker>(
                        constants::netstack::COMPONENT_NAME,
                    )),
                    // TODO(https://fxbug.dev/82890): only include SecureStash as a dependency of
                    // DhcpServer if it is started in persistent mode.
                    fnetemul::Capability::ChildDep(protocol_dep::<fstash::SecureStoreMarker>(
                        constants::secure_stash::COMPONENT_NAME,
                    )),
                ])),
                program_args: if *persistent {
                    Some(vec![String::from("--persistent")])
                } else {
                    None
                },
                ..fnetemul::ChildDef::EMPTY
            },
            KnownServiceProvider::Dhcpv6Client => fnetemul::ChildDef {
                name: Some(constants::dhcpv6_client::COMPONENT_NAME.to_string()),
                url: Some(constants::dhcpv6_client::COMPONENT_URL.to_string()),
                exposes: Some(vec![fnet_dhcpv6::ClientProviderMarker::PROTOCOL_NAME.to_string()]),
                uses: Some(fnetemul::ChildUses::Capabilities(vec![
                    fnetemul::Capability::LogSink(fnetemul::Empty {}),
                    fnetemul::Capability::ChildDep(protocol_dep::<fposix_socket::ProviderMarker>(
                        constants::netstack::COMPONENT_NAME,
                    )),
                ])),
                ..fnetemul::ChildDef::EMPTY
            },
            KnownServiceProvider::DnsResolver => fnetemul::ChildDef {
                name: Some(constants::dns_resolver::COMPONENT_NAME.to_string()),
                url: Some(constants::dns_resolver::COMPONENT_URL.to_string()),
                exposes: Some(vec![
                    fnet_name::LookupAdminMarker::PROTOCOL_NAME.to_string(),
                    fnet_name::LookupMarker::PROTOCOL_NAME.to_string(),
                ]),
                uses: Some(fnetemul::ChildUses::Capabilities(vec![
                    fnetemul::Capability::LogSink(fnetemul::Empty {}),
                    fnetemul::Capability::ChildDep(protocol_dep::<fnet_routes::StateMarker>(
                        constants::netstack::COMPONENT_NAME,
                    )),
                    fnetemul::Capability::ChildDep(protocol_dep::<fposix_socket::ProviderMarker>(
                        constants::netstack::COMPONENT_NAME,
                    )),
                ])),
                ..fnetemul::ChildDef::EMPTY
            },
            KnownServiceProvider::Reachability => fnetemul::ChildDef {
                name: Some(constants::reachability::COMPONENT_NAME.to_string()),
                url: Some(constants::reachability::COMPONENT_URL.to_string()),
                uses: Some(fnetemul::ChildUses::Capabilities(vec![
                    fnetemul::Capability::LogSink(fnetemul::Empty {}),
                    fnetemul::Capability::ChildDep(protocol_dep::<fnet_interfaces::StateMarker>(
                        constants::netstack::COMPONENT_NAME,
                    )),
                    fnetemul::Capability::ChildDep(protocol_dep::<fnetstack::NetstackMarker>(
                        constants::netstack::COMPONENT_NAME,
                    )),
                    fnetemul::Capability::ChildDep(protocol_dep::<fposix_socket::ProviderMarker>(
                        constants::netstack::COMPONENT_NAME,
                    )),
                ])),
                eager: Some(true),
                ..fnetemul::ChildDef::EMPTY
            },
        }
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
    /// The management agent to be used.
    const MANAGEMENT_AGENT: ManagementAgent;
}

/// Uninstantiable type that represents NetCfg's implementation of a network manager.
#[derive(Copy, Clone)]
pub enum NetCfg {}

impl Manager for NetCfg {
    const MANAGEMENT_AGENT: ManagementAgent = ManagementAgent::NetCfg;
}

/// Extensions to `netemul::TestSandbox`.
#[async_trait]
pub trait TestSandboxExt {
    /// Creates a realm with Netstack services.
    fn create_netstack_realm<N, S>(&self, name: S) -> Result<netemul::TestRealm<'_>>
    where
        N: Netstack,
        S: Into<String>;

    /// Creates a realm with the base Netstack services plus additional ones in
    /// `children`.
    fn create_netstack_realm_with<N, S, I>(
        &self,
        name: S,
        children: I,
    ) -> Result<netemul::TestRealm<'_>>
    where
        S: Into<String>,
        N: Netstack,
        I: IntoIterator,
        I::Item: Into<fnetemul::ChildDef>;
}

#[async_trait]
impl TestSandboxExt for netemul::TestSandbox {
    /// Creates a realm with Netstack services.
    fn create_netstack_realm<N, S>(&self, name: S) -> Result<netemul::TestRealm<'_>>
    where
        N: Netstack,
        S: Into<String>,
    {
        self.create_netstack_realm_with::<N, _, _>(name, std::iter::empty::<fnetemul::ChildDef>())
    }

    /// Creates a realm with the base Netstack services plus additional ones in
    /// `children`.
    fn create_netstack_realm_with<N, S, I>(
        &self,
        name: S,
        children: I,
    ) -> Result<netemul::TestRealm<'_>>
    where
        S: Into<String>,
        N: Netstack,
        I: IntoIterator,
        I::Item: Into<fnetemul::ChildDef>,
    {
        self.create_realm(
            name,
            [KnownServiceProvider::Netstack(N::VERSION)]
                .iter()
                .map(fnetemul::ChildDef::from)
                .chain(children.into_iter().map(Into::into)),
        )
    }
}
