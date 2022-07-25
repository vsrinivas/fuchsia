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

#[derive(Debug, Hash, Clone, PartialEq, Eq)]
pub enum FastbootInterface {
    Usb,
    Udp,
    Tcp,
}

/// Represents a target that an event has produced. Not to be confused with the FIDL equivalent
/// under fuchsia.developer.ffx.
#[derive(Debug, Default, Hash, Clone, PartialEq, Eq)]
pub struct TargetInfo {
    pub nodename: Option<String>,
    pub addresses: Vec<TargetAddr>,
    pub serial: Option<String>,
    pub ssh_port: Option<u16>,
    pub fastboot_interface: Option<FastbootInterface>,
    // So far this is only used in testing. It's unclear what the reasoning is
    // for the SSH host address being stored as a string rather than a struct
    // elsewhere in the code, so this is being done for the sake of congruity.
    pub ssh_host_address: Option<String>,
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
    /// An event when a target (which is not necessarily new) has been
    /// updated with more information.
    UpdatedTarget(TargetInfo),
    // TODO(awdavies): Stale target event, target shutdown event, etc.
}

#[derive(Debug, Hash, Clone, PartialEq, Eq)]
pub enum HostPipeErr {
    Unknown(String),
    PermissionDenied,
    ConnectionRefused,
    UnknownNameOrService,
    Timeout,
    KeyVerificationFailure,
    NoRouteToHost,
    NetworkUnreachable,
    InvalidArgument,
}

impl From<String> for HostPipeErr {
    fn from(s: String) -> Self {
        if s.contains("Permission denied") {
            return Self::PermissionDenied;
        }
        if s.contains("Connection refused") {
            return Self::ConnectionRefused;
        }
        if s.contains("Name or service not known") {
            return Self::UnknownNameOrService;
        }
        if s.contains("Connection timed out") {
            return Self::Timeout;
        }
        if s.contains("Host key verification failed") {
            return Self::KeyVerificationFailure;
        }
        if s.contains("No route to host") {
            return Self::NoRouteToHost;
        }
        if s.contains("Network is unreachable") {
            return Self::NetworkUnreachable;
        }
        if s.contains("Invalid argument") {
            return Self::InvalidArgument;
        }
        return Self::Unknown(s);
    }
}

impl From<&str> for HostPipeErr {
    fn from(s: &str) -> Self {
        Self::from(s.to_owned())
    }
}

#[derive(Debug, Hash, Clone, PartialEq, Eq)]
pub enum TargetEvent {
    RcsActivated,
    Rediscovered,
    SshHostPipeErr(HostPipeErr),

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
    /// Target was manually added. A Manual target may have an associated
    /// timeout, which is a time after which the target is allowed to
    /// expire. A Manual target with no timeout will never enter the
    /// "disconnected" state, as they are not discoverable, instead
    /// they return to the "manual" state on disconnection. A Manual target
    /// with a timeout will only enter the disconnected state after that
    /// timeout has elapsed and the target has become non-responsive.
    Manual(Option<Instant>),
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

    pub fn is_manual(&self) -> bool {
        matches!(self, Self::Manual(_))
    }

    pub fn is_product(&self) -> bool {
        matches!(self, Self::Rcs(_) | Self::Mdns(_))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_target_connection_state_default() {
        assert_eq!(TargetConnectionState::default(), TargetConnectionState::Disconnected);
    }

    #[test]
    fn test_target_connection_state_impl() {
        assert!(TargetConnectionState::Manual(None).is_connected());
        assert!(TargetConnectionState::Manual(None).is_manual());
        assert!(!TargetConnectionState::Manual(None).is_product());
        assert!(!TargetConnectionState::Manual(None).is_rcs());

        assert!(TargetConnectionState::Mdns(Instant::now()).is_connected());
        assert!(TargetConnectionState::Mdns(Instant::now()).is_product());
        assert!(!TargetConnectionState::Mdns(Instant::now()).is_rcs());
        assert!(!TargetConnectionState::Mdns(Instant::now()).is_manual());

        assert!(TargetConnectionState::Zedboot(Instant::now()).is_connected());
        assert!(!TargetConnectionState::Zedboot(Instant::now()).is_product());
        assert!(!TargetConnectionState::Zedboot(Instant::now()).is_rcs());
        assert!(!TargetConnectionState::Zedboot(Instant::now()).is_manual());

        assert!(TargetConnectionState::Fastboot(Instant::now()).is_connected());
        assert!(!TargetConnectionState::Fastboot(Instant::now()).is_product());
        assert!(!TargetConnectionState::Fastboot(Instant::now()).is_rcs());
        assert!(!TargetConnectionState::Fastboot(Instant::now()).is_manual());

        assert!(!TargetConnectionState::Disconnected.is_connected());
        assert!(!TargetConnectionState::Disconnected.is_product());
        assert!(!TargetConnectionState::Disconnected.is_rcs());
        assert!(!TargetConnectionState::Disconnected.is_manual());
    }

    #[test]
    fn test_host_pipe_err_from_str() {
        assert_eq!(HostPipeErr::from("Permission denied"), HostPipeErr::PermissionDenied);
        assert_eq!(HostPipeErr::from("Connection refused"), HostPipeErr::ConnectionRefused);
        assert_eq!(
            HostPipeErr::from("Name or service not known"),
            HostPipeErr::UnknownNameOrService
        );
        assert_eq!(HostPipeErr::from("Connection timed out"), HostPipeErr::Timeout);
        assert_eq!(
            HostPipeErr::from("Host key verification failedddddd"),
            HostPipeErr::KeyVerificationFailure
        );
        assert_eq!(HostPipeErr::from("There is No route to host"), HostPipeErr::NoRouteToHost);
        assert_eq!(
            HostPipeErr::from("The Network is unreachable"),
            HostPipeErr::NetworkUnreachable
        );
        assert_eq!(HostPipeErr::from("Invalid argument"), HostPipeErr::InvalidArgument);

        let unknown_str = "OIHWOFIHOIWHFW";
        assert_eq!(HostPipeErr::from(unknown_str), HostPipeErr::Unknown(String::from(unknown_str)));
    }
}
