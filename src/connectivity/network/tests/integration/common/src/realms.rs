// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides utilities for test realms.

use std::borrow::Cow;

use fidl::endpoints::DiscoverableProtocolMarker as _;
use fidl_fuchsia_component as fcomponent;
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
use fidl_fuchsia_net_test_realm as fntr;
use fidl_fuchsia_net_virtualization as fnet_virtualization;
use fidl_fuchsia_netemul as fnetemul;
use fidl_fuchsia_netstack as fnetstack;
use fidl_fuchsia_posix_socket as fposix_socket;
use fidl_fuchsia_posix_socket_packet as fposix_socket_packet;
use fidl_fuchsia_stash as fstash;

use async_trait::async_trait;

use crate::Result;

/// The Netstack version. Used to specify which Netstack version to use in a
/// [`KnownServiceProvider::Netstack`].
#[derive(Copy, Clone, Eq, PartialEq, Debug)]
#[allow(missing_docs)]
pub enum NetstackVersion {
    Netstack2,
    Netstack3,
    ProdNetstack2,
}

impl NetstackVersion {
    /// Gets the Fuchsia URL for this Netstack component.
    pub fn get_url(&self) -> &'static str {
        match self {
            NetstackVersion::Netstack2 => "#meta/netstack-debug.cm",
            NetstackVersion::Netstack3 => "#meta/netstack3.cm",
            NetstackVersion::ProdNetstack2 => "#meta/netstack.cm",
        }
    }

    /// Gets the services exposed by this Netstack component.
    pub fn get_services(&self) -> &[&'static str] {
        match self {
            NetstackVersion::Netstack2 | NetstackVersion::ProdNetstack2 => &[
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
                fposix_socket_packet::ProviderMarker::PROTOCOL_NAME,
            ],
            NetstackVersion::Netstack3 => &[
                fnet_interfaces::StateMarker::PROTOCOL_NAME,
                fnet_stack::StackMarker::PROTOCOL_NAME,
                fposix_socket::ProviderMarker::PROTOCOL_NAME,
                fnet_debug::InterfacesMarker::PROTOCOL_NAME,
            ],
        }
    }
}

/// The NetCfg version.
#[derive(Copy, Clone, Eq, PartialEq, Debug)]
pub enum NetCfgVersion {
    /// The basic NetCfg version.
    Basic,
    /// The advanced NetCfg version.
    Advanced,
}

/// The network manager to use in a [`KnownServiceProvider::Manager`].
#[derive(Copy, Clone, Eq, PartialEq, Debug)]
pub enum ManagementAgent {
    /// A version of netcfg.
    NetCfg(NetCfgVersion),
}

impl ManagementAgent {
    /// Gets the component name for this management agent.
    pub fn get_component_name(&self) -> &'static str {
        match self {
            Self::NetCfg(NetCfgVersion::Basic) => constants::netcfg::basic::COMPONENT_NAME,
            Self::NetCfg(NetCfgVersion::Advanced) => constants::netcfg::advanced::COMPONENT_NAME,
        }
    }

    /// Gets the URL for this network manager component.
    pub fn get_url(&self) -> &'static str {
        match self {
            Self::NetCfg(NetCfgVersion::Basic) => constants::netcfg::basic::COMPONENT_URL,
            Self::NetCfg(NetCfgVersion::Advanced) => constants::netcfg::advanced::COMPONENT_URL,
        }
    }

    /// Default arguments that should be passed to the component when run in a
    /// test realm.
    pub fn get_program_args(&self) -> &[&'static str] {
        match self {
            Self::NetCfg(NetCfgVersion::Basic) | Self::NetCfg(NetCfgVersion::Advanced) => {
                &["--min-severity", "DEBUG", "--allow-virtual-devices"]
            }
        }
    }

    /// Gets the services exposed by this management agent.
    pub fn get_services(&self) -> &[&'static str] {
        match self {
            Self::NetCfg(NetCfgVersion::Basic) => &[],
            Self::NetCfg(NetCfgVersion::Advanced) => {
                &[fnet_virtualization::ControlMarker::PROTOCOL_NAME]
            }
        }
    }
}

/// Components that provide known services used in tests.
#[derive(Clone, Eq, PartialEq, Debug)]
#[allow(missing_docs)]
pub enum KnownServiceProvider {
    Netstack(NetstackVersion),
    Manager { agent: ManagementAgent, use_dhcp_server: bool, enable_dhcpv6: bool },
    SecureStash,
    DhcpServer { persistent: bool },
    Dhcpv6Client,
    DnsResolver,
    Reachability,
    NetworkTestRealm,
    FakeClock,
}

/// Constant properties of components used in networking integration tests, such
/// as monikers and URLs.
#[allow(missing_docs)]
pub mod constants {
    pub mod netstack {
        pub const COMPONENT_NAME: &str = "netstack";
    }
    pub mod netcfg {
        pub mod basic {
            pub const COMPONENT_NAME: &str = "netcfg";
            pub const COMPONENT_URL: &str = "#meta/netcfg-basic.cm";
        }
        pub mod advanced {
            pub const COMPONENT_NAME: &str = "netcfg-advanced";
            pub const COMPONENT_URL: &str = "#meta/netcfg-advanced.cm";
        }
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
        pub const COMPONENT_URL: &str = "#meta/dns_resolver_with_fake_time.cm";
    }
    pub mod reachability {
        pub const COMPONENT_NAME: &str = "reachability";
        pub const COMPONENT_URL: &str = "#meta/reachability.cm";
    }
    pub mod network_test_realm {
        pub const COMPONENT_NAME: &str = "controller";
        pub const COMPONENT_URL: &str = "#meta/controller.cm";
    }
    pub mod fake_clock {
        pub const COMPONENT_NAME: &str = "fake_clock";
        pub const COMPONENT_URL: &str = "#meta/fake_clock.cm";
    }
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
                source: Some(fnetemul::ChildSource::Component(version.get_url().to_string())),
                exposes: Some(
                    version.get_services().iter().map(|service| service.to_string()).collect(),
                ),
                uses: Some(fnetemul::ChildUses::Capabilities(
                    std::iter::once(fnetemul::Capability::LogSink(fnetemul::Empty {}))
                        .chain(match version {
                            // NB: intentionally do not route SecureStore; it is
                            // intentionally not available in all tests to
                            // ensure that its absence is handled gracefully.
                            // Note also that netstack-debug does not have a use
                            // declaration for this protocol for the same
                            // reason.
                            NetstackVersion::Netstack2 | NetstackVersion::Netstack3 => {
                                itertools::Either::Left(std::iter::empty())
                            }
                            NetstackVersion::ProdNetstack2 => {
                                itertools::Either::Right(IntoIterator::into_iter([
                                    fnetemul::Capability::ChildDep(protocol_dep::<
                                        fstash::SecureStoreMarker,
                                    >(
                                        constants::secure_stash::COMPONENT_NAME,
                                    )),
                                    fnetemul::Capability::StorageDep(fnetemul::StorageDep {
                                        variant: Some(fnetemul::StorageVariant::Cache),
                                        path: Some("/cache".to_string()),
                                        ..fnetemul::StorageDep::EMPTY
                                    }),
                                ]))
                            }
                        })
                        .collect(),
                )),
                ..fnetemul::ChildDef::EMPTY
            },
            KnownServiceProvider::Manager { agent, use_dhcp_server, enable_dhcpv6 } => {
                fnetemul::ChildDef {
                    name: Some(agent.get_component_name().to_string()),
                    source: Some(fnetemul::ChildSource::Component(agent.get_url().to_string())),
                    program_args: Some(
                        agent
                            .get_program_args()
                            .iter()
                            .cloned()
                            .chain(std::iter::once("--config-data"))
                            .chain(std::iter::once(if *enable_dhcpv6 {
                                "/pkg/netcfg/dhcpv6.json"
                            } else {
                                "/pkg/netcfg/empty.json"
                            }))
                            .map(Into::into)
                            .collect(),
                    ),
                    exposes: Some(
                        agent.get_services().iter().map(|service| service.to_string()).collect(),
                    ),
                    uses: Some(fnetemul::ChildUses::Capabilities(
                        (*use_dhcp_server)
                            .then(|| {
                                fnetemul::Capability::ChildDep(protocol_dep::<
                                    fnet_dhcp::Server_Marker,
                                >(
                                    constants::dhcp_server::COMPONENT_NAME,
                                ))
                            })
                            .into_iter()
                            .chain(
                                (*enable_dhcpv6)
                                    .then(|| {
                                        fnetemul::Capability::ChildDep(protocol_dep::<
                                            fnet_dhcpv6::ClientProviderMarker,
                                        >(
                                            constants::dhcpv6_client::COMPONENT_NAME,
                                        ))
                                    })
                                    .into_iter(),
                            )
                            .chain(IntoIterator::into_iter([
                                fnetemul::Capability::LogSink(fnetemul::Empty {}),
                                fnetemul::Capability::ChildDep(protocol_dep::<
                                    fnet_filter::FilterMarker,
                                >(
                                    constants::netstack::COMPONENT_NAME,
                                )),
                                fnetemul::Capability::ChildDep(protocol_dep::<
                                    fnet_interfaces::StateMarker,
                                >(
                                    constants::netstack::COMPONENT_NAME,
                                )),
                                fnetemul::Capability::ChildDep(protocol_dep::<
                                    fnet_interfaces_admin::InstallerMarker,
                                >(
                                    constants::netstack::COMPONENT_NAME,
                                )),
                                fnetemul::Capability::ChildDep(protocol_dep::<
                                    fnet_stack::StackMarker,
                                >(
                                    constants::netstack::COMPONENT_NAME,
                                )),
                                fnetemul::Capability::ChildDep(protocol_dep::<
                                    fnetstack::NetstackMarker,
                                >(
                                    constants::netstack::COMPONENT_NAME,
                                )),
                                fnetemul::Capability::ChildDep(protocol_dep::<
                                    fnet_name::LookupAdminMarker,
                                >(
                                    constants::dns_resolver::COMPONENT_NAME,
                                )),
                                fnetemul::Capability::NetemulDevfs(fnetemul::DevfsDep {
                                    name: Some(constants::netcfg::DEV_CLASS_ETHERNET.to_string()),
                                    subdir: Some(
                                        constants::netcfg::CLASS_ETHERNET_PATH.to_string(),
                                    ),
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
                                // TODO(https://fxbug.dev/74532): We won't need to reach out to
                                // debug once we don't have Ethernet interfaces anymore.
                                fnetemul::Capability::ChildDep(protocol_dep::<
                                    fnet_debug::InterfacesMarker,
                                >(
                                    constants::netstack::COMPONENT_NAME,
                                )),
                            ]))
                            .collect(),
                    )),
                    eager: Some(true),
                    ..fnetemul::ChildDef::EMPTY
                }
            }
            KnownServiceProvider::SecureStash => fnetemul::ChildDef {
                name: Some(constants::secure_stash::COMPONENT_NAME.to_string()),
                source: Some(fnetemul::ChildSource::Component(
                    constants::secure_stash::COMPONENT_URL.to_string(),
                )),
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
                source: Some(fnetemul::ChildSource::Component(
                    constants::dhcp_server::COMPONENT_URL.to_string(),
                )),
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
                source: Some(fnetemul::ChildSource::Component(
                    constants::dhcpv6_client::COMPONENT_URL.to_string(),
                )),
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
                source: Some(fnetemul::ChildSource::Component(
                    constants::dns_resolver::COMPONENT_URL.to_string(),
                )),
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
                    fnetemul::Capability::ChildDep(protocol_dep::<
                        fidl_fuchsia_testing::FakeClockMarker,
                    >(
                        constants::fake_clock::COMPONENT_NAME
                    )),
                ])),
                ..fnetemul::ChildDef::EMPTY
            },
            KnownServiceProvider::Reachability => fnetemul::ChildDef {
                name: Some(constants::reachability::COMPONENT_NAME.to_string()),
                source: Some(fnetemul::ChildSource::Component(
                    constants::reachability::COMPONENT_URL.to_string(),
                )),
                uses: Some(fnetemul::ChildUses::Capabilities(vec![
                    fnetemul::Capability::LogSink(fnetemul::Empty {}),
                    fnetemul::Capability::ChildDep(protocol_dep::<fnet_interfaces::StateMarker>(
                        constants::netstack::COMPONENT_NAME,
                    )),
                    fnetemul::Capability::ChildDep(protocol_dep::<fnet_stack::StackMarker>(
                        constants::netstack::COMPONENT_NAME,
                    )),
                    fnetemul::Capability::ChildDep(protocol_dep::<fposix_socket::ProviderMarker>(
                        constants::netstack::COMPONENT_NAME,
                    )),
                ])),
                eager: Some(true),
                ..fnetemul::ChildDef::EMPTY
            },
            KnownServiceProvider::NetworkTestRealm => fnetemul::ChildDef {
                name: Some(constants::network_test_realm::COMPONENT_NAME.to_string()),
                source: Some(fnetemul::ChildSource::Component(
                    constants::network_test_realm::COMPONENT_URL.to_string(),
                )),
                exposes: Some(vec![
                    fntr::ControllerMarker::PROTOCOL_NAME.to_string(),
                    fcomponent::RealmMarker::PROTOCOL_NAME.to_string(),
                ]),
                uses: Some(fnetemul::ChildUses::Capabilities(vec![
                    fnetemul::Capability::LogSink(fnetemul::Empty {}),
                    fnetemul::Capability::ChildDep(protocol_dep::<fnet_stack::StackMarker>(
                        constants::netstack::COMPONENT_NAME,
                    )),
                    fnetemul::Capability::ChildDep(protocol_dep::<fnet_debug::InterfacesMarker>(
                        constants::netstack::COMPONENT_NAME,
                    )),
                    fnetemul::Capability::ChildDep(protocol_dep::<fnet_interfaces::StateMarker>(
                        constants::netstack::COMPONENT_NAME,
                    )),
                    fnetemul::Capability::NetemulDevfs(fnetemul::DevfsDep {
                        name: Some(constants::netcfg::DEV_CLASS_ETHERNET.to_string()),
                        subdir: Some(constants::netcfg::CLASS_ETHERNET_PATH.to_string()),
                        ..fnetemul::DevfsDep::EMPTY
                    }),
                ])),
                ..fnetemul::ChildDef::EMPTY
            },
            KnownServiceProvider::FakeClock => fnetemul::ChildDef {
                name: Some(constants::fake_clock::COMPONENT_NAME.to_string()),
                source: Some(fnetemul::ChildSource::Component(
                    constants::fake_clock::COMPONENT_URL.to_string(),
                )),
                exposes: Some(vec![
                    fidl_fuchsia_testing::FakeClockMarker::PROTOCOL_NAME.to_string(),
                    fidl_fuchsia_testing::FakeClockControlMarker::PROTOCOL_NAME.to_string(),
                ]),
                uses: Some(fnetemul::ChildUses::Capabilities(vec![fnetemul::Capability::LogSink(
                    fnetemul::Empty {},
                )])),
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

/// Uninstantiable type that represents netcfg_basic's implementation of a network manager.
#[derive(Copy, Clone)]
pub enum NetCfgBasic {}

impl Manager for NetCfgBasic {
    const MANAGEMENT_AGENT: ManagementAgent = ManagementAgent::NetCfg(NetCfgVersion::Basic);
}

/// Uninstantiable type that represents netcfg_advanced's implementation of a
/// network manager.
#[derive(Copy, Clone)]
pub enum NetCfgAdvanced {}

impl Manager for NetCfgAdvanced {
    const MANAGEMENT_AGENT: ManagementAgent = ManagementAgent::NetCfg(NetCfgVersion::Advanced);
}

/// Extensions to `netemul::TestSandbox`.
#[async_trait]
pub trait TestSandboxExt {
    /// Creates a realm with Netstack services.
    fn create_netstack_realm<'a, N, S>(&'a self, name: S) -> Result<netemul::TestRealm<'a>>
    where
        N: Netstack,
        S: Into<Cow<'a, str>>;

    /// Creates a realm with the base Netstack services plus additional ones in
    /// `children`.
    fn create_netstack_realm_with<'a, N, S, I>(
        &'a self,
        name: S,
        children: I,
    ) -> Result<netemul::TestRealm<'a>>
    where
        S: Into<Cow<'a, str>>,
        N: Netstack,
        I: IntoIterator,
        I::Item: Into<fnetemul::ChildDef>;
}

#[async_trait]
impl TestSandboxExt for netemul::TestSandbox {
    /// Creates a realm with Netstack services.
    fn create_netstack_realm<'a, N, S>(&'a self, name: S) -> Result<netemul::TestRealm<'a>>
    where
        N: Netstack,
        S: Into<Cow<'a, str>>,
    {
        self.create_netstack_realm_with::<N, _, _>(name, std::iter::empty::<fnetemul::ChildDef>())
    }

    /// Creates a realm with the base Netstack services plus additional ones in
    /// `children`.
    fn create_netstack_realm_with<'a, N, S, I>(
        &'a self,
        name: S,
        children: I,
    ) -> Result<netemul::TestRealm<'a>>
    where
        S: Into<Cow<'a, str>>,
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
