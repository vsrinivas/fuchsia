// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/76549): Replace with GN config once available in an ffx_plugin.
#![deny(unused_results)]

use anyhow::Context as _;
use ffx_core::ffx_plugin;
use fidl::endpoints::ProtocolMarker;
use fidl_fuchsia_developer_remotecontrol as fremotecontrol;
use fidl_fuchsia_net_filter as ffilter;
use fidl_fuchsia_net_neighbor as fneighbor;
use fidl_fuchsia_net_stack as fstack;
use fidl_fuchsia_netstack as fnetstack;

const STACK_SELECTOR: &str = "core/network/netstack:expose:fuchsia.net.stack.Stack";
const NETSTACK_SELECTOR: &str = "core/network/netstack:expose:fuchsia.netstack.Netstack";
const FILTER_SELECTOR: &str = "core/network/netstack:expose:fuchsia.net.filter.Filter";
const LOG_SELECTOR: &str = "core/network/netstack:expose:fuchsia.net.stack.Log";
const NEIGHBOR_CONTROLLER_SELECTOR: &str =
    "core/network/netstack:expose:fuchsia.net.neighbor.Controller";
const NEIGHBOR_VIEW_SELECTOR: &str = "core/network/netstack:expose:fuchsia.net.neighbor.View";

struct FfxConnector {
    remote_control: fremotecontrol::RemoteControlProxy,
}

async fn remotecontrol_connect<S: ProtocolMarker>(
    remote_control: &fremotecontrol::RemoteControlProxy,
    selector: &str,
) -> Result<S::Proxy, anyhow::Error> {
    let (proxy, server_end) = fidl::endpoints::create_proxy::<S>()
        .with_context(|| format!("failed to create proxy to {}", S::NAME))?;
    let _: fremotecontrol::ServiceMatch = remote_control
        .connect(selectors::parse_selector(selector)?, server_end.into_channel())
        .await?
        .map_err(|e| {
            anyhow::anyhow!("failed to connect to {} as {}: {:?}", S::NAME, selector, e)
        })?;
    Ok(proxy)
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fstack::StackMarker> for FfxConnector {
    async fn connect(
        &self,
    ) -> Result<<fstack::StackMarker as ProtocolMarker>::Proxy, anyhow::Error> {
        let Self { remote_control } = &self;
        remotecontrol_connect::<fstack::StackMarker>(remote_control, STACK_SELECTOR).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fnetstack::NetstackMarker> for FfxConnector {
    async fn connect(
        &self,
    ) -> Result<<fnetstack::NetstackMarker as ProtocolMarker>::Proxy, anyhow::Error> {
        let Self { remote_control } = &self;
        remotecontrol_connect::<fnetstack::NetstackMarker>(remote_control, NETSTACK_SELECTOR).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<ffilter::FilterMarker> for FfxConnector {
    async fn connect(
        &self,
    ) -> Result<<ffilter::FilterMarker as ProtocolMarker>::Proxy, anyhow::Error> {
        let Self { remote_control } = &self;
        remotecontrol_connect::<ffilter::FilterMarker>(remote_control, FILTER_SELECTOR).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fstack::LogMarker> for FfxConnector {
    async fn connect(&self) -> Result<<fstack::LogMarker as ProtocolMarker>::Proxy, anyhow::Error> {
        let Self { remote_control } = &self;
        remotecontrol_connect::<fstack::LogMarker>(remote_control, LOG_SELECTOR).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fneighbor::ControllerMarker> for FfxConnector {
    async fn connect(
        &self,
    ) -> Result<<fneighbor::ControllerMarker as ProtocolMarker>::Proxy, anyhow::Error> {
        let Self { remote_control } = &self;
        remotecontrol_connect::<fneighbor::ControllerMarker>(
            remote_control,
            NEIGHBOR_CONTROLLER_SELECTOR,
        )
        .await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fneighbor::ViewMarker> for FfxConnector {
    async fn connect(
        &self,
    ) -> Result<<fneighbor::ViewMarker as ProtocolMarker>::Proxy, anyhow::Error> {
        let Self { remote_control } = &self;
        remotecontrol_connect::<fneighbor::ViewMarker>(remote_control, NEIGHBOR_VIEW_SELECTOR).await
    }
}

#[async_trait::async_trait]
impl net_cli::NetCliDepsConnector for FfxConnector {
    async fn connect_device(&self, _devfs_path: &str) -> Result<net_cli::Device, anyhow::Error> {
        // TODO(https://fxbug.dev/77130): Find a way to get device entries when running on the host.
        Err(anyhow::anyhow!(
            "Cannot connect to devfs on a remote target. See https://fxbug.dev/77130"
        ))
    }
}

#[ffx_plugin()]
pub async fn net(
    remote_control: fremotecontrol::RemoteControlProxy,
    cmd: ffx_net_args::Command,
) -> Result<(), anyhow::Error> {
    let ffx_net_args::Command { cmd } = cmd;
    net_cli::do_root(net_cli::Command { cmd }, &FfxConnector { remote_control }).await
}
