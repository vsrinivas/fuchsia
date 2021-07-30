// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides utilities for test realms.

use {
    anyhow::Context as _, async_trait::async_trait,
    fidl::endpoints::DiscoverableProtocolMarker as _, fidl_fuchsia_net_filter as fnet_filter,
    fidl_fuchsia_net_interfaces as fnet_interfaces, fidl_fuchsia_net_name as fnet_name,
    fidl_fuchsia_net_neighbor as fnet_neighbor, fidl_fuchsia_net_routes as fnet_routes,
    fidl_fuchsia_net_stack as fnet_stack, fidl_fuchsia_netemul as fnetemul,
    fidl_fuchsia_netstack as fnetstack, fidl_fuchsia_posix_socket as fposix_socket,
    fidl_fuchsia_stash as fstash,
};

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
                fnet_interfaces::StateMarker::PROTOCOL_NAME,
                fnet_neighbor::ControllerMarker::PROTOCOL_NAME,
                fnet_neighbor::ViewMarker::PROTOCOL_NAME,
                fnet_routes::StateMarker::PROTOCOL_NAME,
                fnet_stack::LogMarker::PROTOCOL_NAME,
                fnet_stack::StackMarker::PROTOCOL_NAME,
                fnetstack::NetstackMarker::PROTOCOL_NAME,
                fposix_socket::ProviderMarker::PROTOCOL_NAME,
            ],
            NetstackVersion::Netstack3 => &[
                fnet_stack::StackMarker::PROTOCOL_NAME,
                fposix_socket::ProviderMarker::PROTOCOL_NAME,
            ],
        }
    }
}

/// Components that provide known services used in tests.
#[derive(Clone, Eq, PartialEq, Debug)]
#[allow(missing_docs)]
pub enum KnownServiceProvider {
    Netstack(NetstackVersion),
    SecureStash,
    DhcpServer { persistent: bool },
    Dhcpv6Client,
    LookupAdmin,
    Reachability,
}

/// Constant properties of components used in networking integration tests, such
/// as monikers and URLs.
//
// TODO(https://fxbug.dev/77202): when migrating netstack integration tests to
// netemul-v2, include these components in the test package as necessary, and
// update their URLs here to their v2 versions.
#[allow(missing_docs)]
pub mod constants {
    pub mod netstack {
        pub const COMPONENT_NAME: &str = "netstack";
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
        pub const COMPONENT_URL: &str =
            "TODO(https://fxbug.dev/77202): specify a CFv2 component manifest for dhcpv6 client";
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
                exposes: Some(
                    vec![fidl_fuchsia_net_dhcp::Server_Marker::PROTOCOL_NAME.to_string()],
                ),
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
                exposes: Some(vec![
                    fidl_fuchsia_net_dhcpv6::ClientProviderMarker::PROTOCOL_NAME.to_string()
                ]),
                uses: Some(fnetemul::ChildUses::Capabilities(vec![
                    fnetemul::Capability::LogSink(fnetemul::Empty {}),
                    fnetemul::Capability::ChildDep(protocol_dep::<fposix_socket::ProviderMarker>(
                        constants::netstack::COMPONENT_NAME,
                    )),
                ])),
                ..fnetemul::ChildDef::EMPTY
            },
            KnownServiceProvider::LookupAdmin => fnetemul::ChildDef {
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
    const PKG_URL: &'static str = "#meta/netcfg-netemul.cmx";
    // Specify an empty config file for NetCfg when it is run in netemul.
    const TESTING_ARGS: &'static [&'static str] = &["--config-data", "netcfg/empty.json"];
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

    /// Helper function to create a new Netstack realm and connect to a
    /// netstack service on it.
    fn new_netstack<N, P, S>(&self, name: S) -> Result<(netemul::TestRealm<'_>, P::Proxy)>
    where
        N: Netstack,
        P: fidl::endpoints::DiscoverableProtocolMarker,
        S: Into<String>;

    /// Helper function to create a new Netstack realm and a new unattached
    /// endpoint.
    async fn new_netstack_and_device<N, E, P, S>(
        &self,
        name: S,
    ) -> Result<(netemul::TestRealm<'_>, P::Proxy, netemul::TestEndpoint<'_>)>
    where
        N: Netstack,
        E: netemul::Endpoint,
        P: fidl::endpoints::DiscoverableProtocolMarker,
        S: Into<String> + Copy + Send;
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

    /// Helper function to create a new Netstack realm and connect to a netstack
    /// service on it.
    fn new_netstack<N, P, S>(&self, name: S) -> Result<(netemul::TestRealm<'_>, P::Proxy)>
    where
        N: Netstack,
        P: fidl::endpoints::DiscoverableProtocolMarker,
        S: Into<String>,
    {
        let realm =
            self.create_netstack_realm::<N, _>(name).context("failed to create test realm")?;
        let netstack_proxy =
            realm.connect_to_protocol::<P>().context("failed to connect to netstack")?;
        Ok((realm, netstack_proxy))
    }

    /// Helper function to create a new Netstack realm and a new unattached
    /// endpoint.
    async fn new_netstack_and_device<N, E, P, S>(
        &self,
        name: S,
    ) -> Result<(netemul::TestRealm<'_>, P::Proxy, netemul::TestEndpoint<'_>)>
    where
        N: Netstack,
        E: netemul::Endpoint,
        P: fidl::endpoints::DiscoverableProtocolMarker,
        S: Into<String> + Copy + Send,
    {
        let (realm, stack) = self.new_netstack::<N, P, _>(name)?;
        let endpoint =
            self.create_endpoint::<E, _>(name).await.context("failed to create endpoint")?;
        Ok((realm, stack, endpoint))
    }
}
