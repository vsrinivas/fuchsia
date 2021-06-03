// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use addr::TargetAddr;
use std::net::SocketAddr;

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
    OvernetPeer(u64),
    NewTarget(TargetInfo),
    // TODO(awdavies): Stale target event, target shutdown event, etc.
}
