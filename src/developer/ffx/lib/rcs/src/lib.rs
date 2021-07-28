// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _, Result},
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_developer_remotecontrol::{
        IdentifyHostError, RemoteControlMarker, RemoteControlProxy,
    },
    fidl_fuchsia_overnet_protocol::NodeId,
    hoist::OvernetInstance as _,
    std::hash::{Hash, Hasher},
    timeout::TimeoutError,
};

#[derive(Debug, Clone)]
pub struct RcsConnection {
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
    pub fn new(id: &mut NodeId) -> Result<Self> {
        let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
        let _result = RcsConnection::connect_to_service(id, s)?;
        let proxy = RemoteControlProxy::new(
            fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?,
        );

        Ok(Self { proxy, overnet_id: id.clone() })
    }

    pub fn copy_to_channel(&mut self, channel: fidl::Channel) -> Result<()> {
        RcsConnection::connect_to_service(&mut self.overnet_id, channel)
    }

    fn connect_to_service(overnet_id: &mut NodeId, channel: fidl::Channel) -> Result<()> {
        let svc = hoist::hoist().connect_as_service_consumer()?;
        svc.connect_to_service(overnet_id, RemoteControlMarker::NAME, channel)
            .map_err(|e| anyhow!("Error connecting to Rcs: {}", e))
    }

    // Primarily For testing.
    pub fn new_with_proxy(proxy: RemoteControlProxy, id: &NodeId) -> Self {
        Self { proxy, overnet_id: id.clone() }
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
