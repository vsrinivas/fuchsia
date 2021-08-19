// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use addr::TargetAddr;
use rcs::RcsConnection;
use std::net::SocketAddr;
use std::time::Instant;

pub trait TryIntoTargetInfo: Sized {
    type Error;

    /// Attempts, given a source socket address, to determine whether the
    /// received message was from a Fuchsia target, and if so, what kind. Attempts
    /// to fill in as much information as possible given the message, consuming
    /// the underlying object in the process.
    fn try_into_target_info(self, src: SocketAddr) -> Result<TargetInfo, Self::Error>;
}

#[derive(Debug, Default, Hash, Clone, PartialEq, Eq)]
pub struct TargetInfo {
    pub nodename: Option<String>,
    pub addresses: Vec<TargetAddr>,
    pub serial: Option<String>,
    pub ssh_port: Option<u16>,
    pub is_fastboot: bool,
}

#[derive(Debug, Hash, Clone, PartialEq, Eq)]
pub enum WireTrafficType {
    // It's simpler to leave this here than to sprinkle a few dozen linux-only
    // invocations throughout the daemon code.
    #[allow(dead_code)]
    Mdns(TargetInfo),
    Fastboot(TargetInfo),
    Zedboot(TargetInfo),
}

/// Encapsulates an event that occurs on the daemon.
#[derive(Debug, Hash, Clone, PartialEq, Eq)]
pub enum DaemonEvent {
    WireTraffic(WireTrafficType),
    /// A peer with the contained NodeId has been observed on the
    /// Overnet mesh.
    OvernetPeer(u64),
    /// A peer with the contained NodeId has been dropped from the
    /// Overnet mesh (there are no remaining known routes to this peer).
    OvernetPeerLost(u64),
    NewTarget(TargetInfo),
    // TODO(awdavies): Stale target event, target shutdown event, etc.
}

#[derive(Debug, Hash, Clone, PartialEq, Eq)]
pub enum TargetEvent {
    RcsActivated,
    Rediscovered,

    /// LHS is previous state, RHS is current state.
    ConnectionStateChanged(TargetConnectionState, TargetConnectionState),
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum TargetConnectionState {
    /// Default state: no connection, pending rediscovery.
    Disconnected,
    /// Contains the last known ping from mDNS.
    Mdns(Instant),
    /// Contains an actual connection to RCS.
    Rcs(RcsConnection),
    /// Target was manually added. Targets that are manual never enter
    /// the "disconnected" state, as they are not discoverable, instead
    /// they return to the "manual" state on disconnection.
    Manual,
    /// Contains the last known interface update with a Fastboot serial number.
    Fastboot(Instant),
    /// Contains the last known interface update with a Fastboot serial number.
    Zedboot(Instant),
}

impl Default for TargetConnectionState {
    fn default() -> Self {
        TargetConnectionState::Disconnected
    }
}

impl TargetConnectionState {
    pub fn is_connected(&self) -> bool {
        match self {
            Self::Disconnected => false,
            _ => true,
        }
    }

    pub fn is_rcs(&self) -> bool {
        match self {
            TargetConnectionState::Rcs(_) => true,
            _ => false,
        }
    }
}
