// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/76549): Replace with GN config once available in an ffx_plugin.
#![deny(unused_results)]

use anyhow::Context as _;
use errors::FfxError;
use ffx_core::ffx_plugin;
use fidl::endpoints::ProtocolMarker;
use fidl_fuchsia_developer_remotecontrol as fremotecontrol;
use fidl_fuchsia_net_debug as fdebug;
use fidl_fuchsia_net_dhcp as fdhcp;
use fidl_fuchsia_net_filter as ffilter;
use fidl_fuchsia_net_interfaces as finterfaces;
use fidl_fuchsia_net_name as fname;
use fidl_fuchsia_net_neighbor as fneighbor;
use fidl_fuchsia_net_stack as fstack;
use fidl_fuchsia_netstack as fnetstack;

const DEBUG_SELECTOR_SUFFIX: &str = "/netstack:expose:fuchsia.net.debug.Interfaces";
const DHCPD_SELECTOR_SUFFIX: &str = "/dhcpd:expose:fuchsia.net.dhcp.Server";
const DNS_SELECTOR_SUFFIX: &str = "/dns-resolver:expose:fuchsia.net.name.Lookup";
const FILTER_SELECTOR_SUFFIX: &str = "/netstack:expose:fuchsia.net.filter.Filter";
const INTERFACES_SELECTOR_SUFFIX: &str = "/netstack:expose:fuchsia.net.interfaces.State";
const NEIGHBOR_CONTROLLER_SELECTOR_SUFFIX: &str =
    "/netstack:expose:fuchsia.net.neighbor.Controller";
const NEIGHBOR_VIEW_SELECTOR_SUFFIX: &str = "/netstack:expose:fuchsia.net.neighbor.View";
const LOG_SELECTOR_SUFFIX: &str = "/netstack:expose:fuchsia.net.stack.Log";
const STACK_SELECTOR_SUFFIX: &str = "/netstack:expose:fuchsia.net.stack.Stack";
const NETSTACK_SELECTOR_SUFFIX: &str = "/netstack:expose:fuchsia.netstack.Netstack";
const NETWORK_REALM: &str = "core/network";

struct FfxConnector<'a> {
    remote_control: fremotecontrol::RemoteControlProxy,
    realm: &'a str,
}

impl FfxConnector<'_> {
    async fn remotecontrol_connect<S: ProtocolMarker>(
        &self,
        selector_suffix: &str,
    ) -> Result<S::Proxy, anyhow::Error> {
        let Self { remote_control, realm } = &self;
        let unparsed_selector = format!("{}{}", realm, selector_suffix);
        let (proxy, server_end) = fidl::endpoints::create_proxy::<S>()
            .with_context(|| format!("failed to create proxy to {}", S::DEBUG_NAME))?;
        let selector = selectors::parse_selector::<selectors::VerboseError>(&unparsed_selector)
            .with_context(|| format!("failed to parse selector {}", &unparsed_selector))?;
        let _: fremotecontrol::ServiceMatch =
            remote_control.connect(selector, server_end.into_channel()).await?.map_err(|e| {
                anyhow::anyhow!(
                    "failed to connect to {} as {}: {:?}",
                    S::DEBUG_NAME,
                    unparsed_selector,
                    e
                )
            })?;
        Ok(proxy)
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fdebug::InterfacesMarker> for FfxConnector<'_> {
    async fn connect(
        &self,
    ) -> Result<<fdebug::InterfacesMarker as ProtocolMarker>::Proxy, anyhow::Error> {
        self.remotecontrol_connect::<fdebug::InterfacesMarker>(DEBUG_SELECTOR_SUFFIX).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fdhcp::Server_Marker> for FfxConnector<'_> {
    async fn connect(
        &self,
    ) -> Result<<fdhcp::Server_Marker as ProtocolMarker>::Proxy, anyhow::Error> {
        self.remotecontrol_connect::<fdhcp::Server_Marker>(DHCPD_SELECTOR_SUFFIX).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<ffilter::FilterMarker> for FfxConnector<'_> {
    async fn connect(
        &self,
    ) -> Result<<ffilter::FilterMarker as ProtocolMarker>::Proxy, anyhow::Error> {
        self.remotecontrol_connect::<ffilter::FilterMarker>(FILTER_SELECTOR_SUFFIX).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<finterfaces::StateMarker> for FfxConnector<'_> {
    async fn connect(
        &self,
    ) -> Result<<finterfaces::StateMarker as ProtocolMarker>::Proxy, anyhow::Error> {
        self.remotecontrol_connect::<finterfaces::StateMarker>(INTERFACES_SELECTOR_SUFFIX).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fneighbor::ControllerMarker> for FfxConnector<'_> {
    async fn connect(
        &self,
    ) -> Result<<fneighbor::ControllerMarker as ProtocolMarker>::Proxy, anyhow::Error> {
        self.remotecontrol_connect::<fneighbor::ControllerMarker>(
            NEIGHBOR_CONTROLLER_SELECTOR_SUFFIX,
        )
        .await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fneighbor::ViewMarker> for FfxConnector<'_> {
    async fn connect(
        &self,
    ) -> Result<<fneighbor::ViewMarker as ProtocolMarker>::Proxy, anyhow::Error> {
        self.remotecontrol_connect::<fneighbor::ViewMarker>(NEIGHBOR_VIEW_SELECTOR_SUFFIX).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fstack::LogMarker> for FfxConnector<'_> {
    async fn connect(&self) -> Result<<fstack::LogMarker as ProtocolMarker>::Proxy, anyhow::Error> {
        self.remotecontrol_connect::<fstack::LogMarker>(LOG_SELECTOR_SUFFIX).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fstack::StackMarker> for FfxConnector<'_> {
    async fn connect(
        &self,
    ) -> Result<<fstack::StackMarker as ProtocolMarker>::Proxy, anyhow::Error> {
        self.remotecontrol_connect::<fstack::StackMarker>(STACK_SELECTOR_SUFFIX).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fnetstack::NetstackMarker> for FfxConnector<'_> {
    async fn connect(
        &self,
    ) -> Result<<fnetstack::NetstackMarker as ProtocolMarker>::Proxy, anyhow::Error> {
        self.remotecontrol_connect::<fnetstack::NetstackMarker>(NETSTACK_SELECTOR_SUFFIX).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fname::LookupMarker> for FfxConnector<'_> {
    async fn connect(
        &self,
    ) -> Result<<fname::LookupMarker as ProtocolMarker>::Proxy, anyhow::Error> {
        self.remotecontrol_connect::<fname::LookupMarker>(DNS_SELECTOR_SUFFIX).await
    }
}

#[async_trait::async_trait]
impl net_cli::NetCliDepsConnector for FfxConnector<'_> {
    async fn connect_device(&self, _devfs_path: &str) -> Result<net_cli::Device, anyhow::Error> {
        // TODO(https://fxbug.dev/77130): Find a way to get device entries when running on the host.
        Err(anyhow::anyhow!(
            "Cannot connect to devfs on a remote target. See https://fxbug.dev/77130"
        ))
    }
}

const EXIT_FAILURE: i32 = 1;

#[ffx_plugin()]
pub async fn net(
    remote_control: fremotecontrol::RemoteControlProxy,
    cmd: ffx_net_args::Command,
) -> Result<(), anyhow::Error> {
    let ffx_net_args::Command { cmd, realm } = cmd;
    let realm = realm.as_deref().unwrap_or(NETWORK_REALM);
    net_cli::do_root(net_cli::Command { cmd }, &FfxConnector { remote_control, realm })
        .await
        .map_err(|e| match net_cli::underlying_user_facing_error(&e) {
            Some(net_cli::UserFacingError { msg }) => {
                FfxError::Error(anyhow::Error::msg(msg), EXIT_FAILURE).into()
            }
            None => e,
        })
}
