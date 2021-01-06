// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The direct link connection identifier (DLCI) definitions.
mod dlci;
pub use dlci::{ServerChannel, DLCI};

/// The error type used throughout this library.
mod error;
pub use error::Error as RfcommError;

/// The definitions for RFCOMM frames - the basic unit of data in RFCOMM.
pub mod frame;

/// Convenience helpers for profiles that request RFCOMM.
pub mod profile;

/// The largest frame size that this RFCOMM implementation accepts. Currently, the default
/// max TX size of the underlying fuchsia_bluetooth::Channel.
pub const MAX_RFCOMM_FRAME_SIZE: usize = fuchsia_bluetooth::types::Channel::DEFAULT_MAX_TX;

/// The Role assigned to a device in an RFCOMM Session.
#[derive(Copy, Clone, Debug, PartialEq)]
pub enum Role {
    /// RFCOMM Session has not started up the start-up procedure.
    Unassigned,
    /// The start-up procedure is in progress, and so the role is being negotiated.
    Negotiating,
    /// The device that starts up the multiplexer control channel is considered
    /// the initiator.
    Initiator,
    /// The device that responds to the start-up procedure.
    Responder,
}

impl Role {
    /// Returns the Role opposite to the current Role.
    pub fn opposite_role(&self) -> Self {
        match self {
            Role::Initiator => Role::Responder,
            Role::Responder => Role::Initiator,
            role => *role,
        }
    }

    /// Returns true if the multiplexer has started - namely, a role has been assigned.
    pub fn is_multiplexer_started(&self) -> bool {
        *self == Role::Initiator || *self == Role::Responder
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_convert_to_opposite_role() {
        let role = Role::Initiator;
        assert_eq!(role.opposite_role(), Role::Responder);

        let role = Role::Responder;
        assert_eq!(role.opposite_role(), Role::Initiator);

        let role = Role::Unassigned;
        assert_eq!(role.opposite_role(), Role::Unassigned);

        let role = Role::Negotiating;
        assert_eq!(role.opposite_role(), Role::Negotiating);
    }
}
