// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _, Result},
    errors,
    fidl::prelude::*,
    fidl_fuchsia_developer_ffx as ffx,
    fidl_fuchsia_developer_remotecontrol::{
        ConnectError, IdentifyHostError, RemoteControlMarker, RemoteControlProxy,
    },
    fidl_fuchsia_overnet_protocol::NodeId,
    futures::StreamExt,
    futures::TryFutureExt,
    hoist::{Hoist, OvernetInstance},
    selectors::VerboseError,
    std::hash::{Hash, Hasher},
    std::time::Duration,
    timeout::timeout,
    timeout::TimeoutError,
};

#[derive(Debug, Clone)]
pub struct RcsConnection {
    pub hoist: Hoist,
    pub proxy: RemoteControlProxy,
    pub overnet_id: NodeId,
}

impl Hash for RcsConnection {
    fn hash<H>(&self, state: &mut H)
    where
        H: Hasher,
    {
        self.overnet_id.id.hash(state)
    }
}

impl PartialEq for RcsConnection {
    fn eq(&self, other: &Self) -> bool {
        self.overnet_id == other.overnet_id
    }
}

impl Eq for RcsConnection {}

impl RcsConnection {
    pub fn new(hoist: Hoist, id: &mut NodeId) -> Result<Self> {
        let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
        let _result = RcsConnection::connect_to_service(&hoist, id, s)?;
        let proxy = RemoteControlProxy::new(
            fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?,
        );

        Ok(Self { hoist, proxy, overnet_id: id.clone() })
    }

    pub fn copy_to_channel(&mut self, channel: fidl::Channel) -> Result<()> {
        RcsConnection::connect_to_service(&self.hoist, &mut self.overnet_id, channel)
    }

    fn connect_to_service(
        hoist: &Hoist,
        overnet_id: &mut NodeId,
        channel: fidl::Channel,
    ) -> Result<()> {
        let svc = hoist.connect_as_service_consumer()?;
        svc.connect_to_service(overnet_id, RemoteControlMarker::PROTOCOL_NAME, channel)
            .map_err(|e| anyhow!("Error connecting to Rcs: {}", e))
    }

    // Primarily For testing.
    pub fn new_with_proxy(hoist: &Hoist, proxy: RemoteControlProxy, id: &NodeId) -> Self {
        Self { hoist: hoist.clone(), proxy, overnet_id: id.clone() }
    }
}

#[derive(Debug)]
pub enum RcsConnectionError {
    /// There is something wrong with the FIDL connection.
    FidlConnectionError(fidl::Error),
    /// There was a timeout trying to communicate with RCS.
    ConnectionTimeoutError(TimeoutError),
    /// There is an error from within Rcs itself.
    RemoteControlError(IdentifyHostError),

    /// There is an error with the output from Rcs.
    TargetError(anyhow::Error),
}

impl std::fmt::Display for RcsConnectionError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            RcsConnectionError::FidlConnectionError(ferr) => {
                write!(f, "fidl connection error: {}", ferr)
            }
            RcsConnectionError::ConnectionTimeoutError(_) => write!(f, "timeout error"),
            RcsConnectionError::RemoteControlError(ierr) => write!(f, "internal error: {:?}", ierr),
            RcsConnectionError::TargetError(error) => write!(f, "general error: {}", error),
        }
    }
}

const KNOCK_TIMEOUT: Duration = Duration::from_secs(1);

#[derive(thiserror::Error, Debug)]
pub enum KnockRcsError {
    #[error("FIDL error {0:?}")]
    FidlError(#[from] fidl::Error),
    #[error("Creating FIDL channel: {0:?}")]
    ChannelError(#[from] fidl::handle::Status),
    #[error("Connecting to RCS {0:?}")]
    RcsConnectError(ConnectError),
    #[error("Could not knock service from RCS")]
    FailedToKnock,
}

/// Attempts to "knock" RCS.
///
/// This can be used to verify whether it is up and running, or as a control flow to ensure that
/// RCS is up and running before continuing time-sensitive operations.
pub async fn knock_rcs(rcs_proxy: &RemoteControlProxy) -> Result<(), ffx::TargetConnectionError> {
    knock_rcs_impl(rcs_proxy).await.map_err(|e| match e {
        KnockRcsError::FidlError(e) => {
            tracing::warn!("FIDL error: {:?}", e);
            ffx::TargetConnectionError::FidlCommunicationError
        }
        KnockRcsError::ChannelError(e) => {
            tracing::warn!("RCS connect channel err: {:?}", e);
            ffx::TargetConnectionError::FidlCommunicationError
        }
        KnockRcsError::RcsConnectError(c) => {
            tracing::warn!("RCS failed connecting to itself for knocking: {:?}", c);
            ffx::TargetConnectionError::RcsConnectionError
        }
        KnockRcsError::FailedToKnock => ffx::TargetConnectionError::FailedToKnockService,
    })
}

async fn knock_rcs_impl(rcs_proxy: &RemoteControlProxy) -> Result<(), KnockRcsError> {
    let (knock_client, knock_remote) = fidl::handle::Channel::create()?;
    let knock_client = fuchsia_async::Channel::from_channel(knock_client)?;
    let knock_client = fidl::client::Client::new(knock_client, "knock_client");
    rcs_proxy
        .connect(
            selectors::parse_selector::<VerboseError>(
                "core/remote-control:expose:fuchsia.developer.remotecontrol.RemoteControl",
            )
            .unwrap(),
            knock_remote,
        )
        .await?
        .map_err(|e| KnockRcsError::RcsConnectError(e))?;
    let mut event_receiver = knock_client.take_event_receiver();
    let res = timeout(KNOCK_TIMEOUT, event_receiver.next()).await;
    match res {
        Err(_) => Ok(()), // timeout is fine here, it means the connection wasn't lost.
        Ok(r) => r.ok_or(KnockRcsError::FailedToKnock).map(drop),
    }
}

pub async fn connect_with_timeout(
    dur: Duration,
    selector: &str,
    rcs_proxy: &RemoteControlProxy,
    server_end: fidl::Channel,
) -> Result<()> {
    timeout::timeout(dur, rcs_proxy.connect(selectors::parse_selector::<VerboseError>(selector)?, server_end)
        .map_ok_or_else(|e| Result::<(), anyhow::Error>::Err(anyhow::anyhow!(e)), |fidl_result| {
            fidl_result.map(|_| ()).map_err(|e| {
                    match e {
                        ConnectError::NoMatchingServices => {
                            errors::ffx_error!(format!(
"The plugin service selector '{}' did not match any services on the target.

It is possible that the expected component is either not built into the system image, or that the
package server has not been setup.

For users, ensure your Fuchsia device is registered with ffx. To do this you can run:

$ ffx target repository register -r $IMAGE_TYPE --alias fuchsia.com

For plugin developers, it may be possible that the moniker you're attempting to connect to is
incorrect. You can use `ffx component select moniker '<selector>'` to explore the component topology
of your target device to fix this selector if this is the case.

If you believe you have encountered a bug after walking through the above please report it at
http://fxbug.dev/new/ffx+User+Bug",
                            selector)).into()
                        }
                        ConnectError::MultipleMatchingServices => {
                            errors::ffx_error!(
                                format!(
"Plugin service selectors must match exactly one service, but '{}' matched multiple services on the target.
If you are not developing this plugin, then this is a bug. Please report it at http://fxbug.dev/new/ffx+User+Bug.

Plugin developers: you can use `ffx component select moniker '{}'` to see which services matched the provided selector.",
                                    selector, selector)).into()
                        }
                        _ => {
                            anyhow::anyhow!(
                                format!("This service dependency exists but connecting to it failed with error {:?}. Selector: {}.", e, selector)
                            )
                        }
                    }
                })
        })).await.map_err(|_| errors::ffx_error!("Timed out connecting to service: '{}'.
This is likely due to a sudden shutdown or disconnect of the target.
If you have encountered what you think is a bug, Please report it at http://fxbug.dev/new/ffx+User+Bug

To diagnose the issue, use `ffx doctor`.", selector).into()).and_then(|r| r)
}
