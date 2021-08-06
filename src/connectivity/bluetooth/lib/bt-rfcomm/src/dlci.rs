// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::format_err;
use std::convert::TryFrom;

use crate::frame::FrameParseError;
use crate::{RfcommError, Role};

/// Identifier for a direct link connection (DLC) between devices.
///
/// Use the provided `u8::try_from` implementation to construct a valid DLCI.
///
/// The DLCI is 6 bits wide and is split into a direction bit and a
/// 5-bit Server Channel number.
/// DLCIs 1 and 62-63 are reserved and never used in RFCOMM.
/// See RFCOMM 5.4.
#[derive(Clone, Copy, Hash, Eq, Debug, PartialEq)]
pub struct DLCI(u8);

impl DLCI {
    /// The control channel for the RFCOMM Multiplexer.
    pub const MUX_CONTROL_DLCI: DLCI = DLCI(0);
    /// The minimum user-space DLCI.
    const MIN_USER_DLCI: DLCI = DLCI(2);
    /// The maximum user-space DLCI.
    const MAX_USER_DLCI: DLCI = DLCI(61);

    pub fn is_mux_control(&self) -> bool {
        *self == Self::MUX_CONTROL_DLCI
    }

    pub fn is_user(&self) -> bool {
        self.0 >= Self::MIN_USER_DLCI.0 && self.0 <= Self::MAX_USER_DLCI.0
    }

    /// Returns Ok(()) if the DLCI belongs to the side of the session with the
    /// given `role` - this is only applicable to User DLCIs.
    ///
    /// The DLCI space is divided into two equal parts. RFCOMM 5.2 states:
    /// "...this partitions the DLCI value space such that server applications on the non-
    /// initiating device are reachable on DLCIs 2,4,6,...,60, and server applications on
    /// the initiating device are reachable on DLCIs 3,5,7,...,61."
    pub fn validate(&self, role: Role) -> Result<(), RfcommError> {
        if !self.is_user() {
            return Err(RfcommError::InvalidDLCI(*self));
        }

        let valid_bit = match role {
            Role::Responder => 0,
            Role::Initiator => 1,
            role => {
                return Err(RfcommError::InvalidRole(role));
            }
        };

        if self.0 % 2 == valid_bit {
            Ok(())
        } else {
            Err(RfcommError::InvalidDLCI(*self))
        }
    }

    /// Returns true if the DLCI is initiated by this device.
    /// Returns an Error if the provided `role` is invalid or if the DLCI is not
    /// a user DLCI.
    pub fn initiator(&self, role: Role) -> Result<bool, RfcommError> {
        if !self.is_user() {
            return Err(RfcommError::InvalidDLCI(*self));
        }

        // A DLCI is considered initiated by us if the direction bit is the same as the expected
        // direction bit associated with the role of the remote peer. See RFCOMM 5.4 for the
        // expected value of the direction bit for a particular DLCI.
        match role.opposite_role() {
            Role::Responder => Ok(self.0 % 2 == 0),
            Role::Initiator => Ok(self.0 % 2 == 1),
            role => {
                return Err(RfcommError::InvalidRole(role));
            }
        }
    }
}

impl TryFrom<u8> for DLCI {
    type Error = FrameParseError;

    fn try_from(value: u8) -> Result<DLCI, Self::Error> {
        if value != DLCI::MUX_CONTROL_DLCI.0
            && (value < DLCI::MIN_USER_DLCI.0 || value > DLCI::MAX_USER_DLCI.0)
        {
            return Err(FrameParseError::InvalidDLCI(value));
        }
        Ok(DLCI(value))
    }
}

impl From<DLCI> for u8 {
    fn from(value: DLCI) -> u8 {
        value.0
    }
}

impl TryFrom<DLCI> for ServerChannel {
    type Error = RfcommError;

    fn try_from(dlci: DLCI) -> Result<ServerChannel, Self::Error> {
        if !dlci.is_user() {
            return Err(RfcommError::InvalidDLCI(dlci));
        }

        // The ServerChannel is the upper 5 bits of the 6-bit DLCI. See RFCOMM 5.4.
        ServerChannel::try_from(dlci.0 >> 1)
    }
}

/// The Server Channel number associated with an RFCOMM channel.
///
/// Use the provided `u8::try_from` implementation to construct a valid ServerChannel.
///
/// Server Channels are 5 bits wide; they are the 5 most significant bits of the
/// DLCI.
/// Server Channels 0 and 31 are reserved. See RFCOMM 5.4 for the definition and
/// usage.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct ServerChannel(u8);

impl ServerChannel {
    const MAX: ServerChannel = ServerChannel(30);
    const MIN: ServerChannel = ServerChannel(1);

    /// Returns an iterator over all the Server Channels.
    pub fn all() -> impl Iterator<Item = ServerChannel> {
        (Self::MIN.0..=Self::MAX.0).map(|x| ServerChannel(x))
    }

    /// Converts the ServerChannel to a DLCI for the provided `role`.
    /// Defined in RFCOMM 5.4.
    pub fn to_dlci(&self, role: Role) -> Result<DLCI, RfcommError> {
        let direction_bit = match role {
            Role::Initiator => 1,
            Role::Responder => 0,
            r => {
                return Err(RfcommError::InvalidRole(r));
            }
        };

        let v = (self.0 << 1) | direction_bit;
        DLCI::try_from(v).map_err(RfcommError::from)
    }
}

impl TryFrom<u8> for ServerChannel {
    type Error = RfcommError;
    fn try_from(src: u8) -> Result<ServerChannel, Self::Error> {
        if src < Self::MIN.0 || src > Self::MAX.0 {
            return Err(RfcommError::Other(format_err!("Out of range: {:?}", src).into()));
        }
        Ok(ServerChannel(src))
    }
}

impl From<ServerChannel> for u8 {
    fn from(value: ServerChannel) -> u8 {
        value.0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use matches::assert_matches;

    #[test]
    fn test_create_dlci() {
        let v1 = 10;
        let dlci = DLCI::try_from(v1);
        let expected_sc1 = ServerChannel::try_from(5).unwrap();
        assert!(dlci.is_ok());
        assert_eq!(ServerChannel::try_from(dlci.unwrap()).unwrap(), expected_sc1);

        let v2 = 0;
        let dlci = DLCI::try_from(v2).unwrap();
        assert_matches!(ServerChannel::try_from(dlci), Err(RfcommError::InvalidDLCI(_)));

        let v3 = 2;
        let dlci = DLCI::try_from(v3);
        let expected_sc3 = ServerChannel::try_from(1).unwrap();
        assert!(dlci.is_ok());
        assert_eq!(ServerChannel::try_from(dlci.unwrap()).unwrap(), expected_sc3);

        let v4 = 61;
        let dlci = DLCI::try_from(v4);
        let expected_sc4 = ServerChannel::try_from(30).unwrap();
        assert!(dlci.is_ok());
        assert_eq!(ServerChannel::try_from(dlci.unwrap()).unwrap(), expected_sc4);

        let v5 = 1;
        let dlci = DLCI::try_from(v5);
        assert!(dlci.is_err());

        let v6 = 62;
        let dlci = DLCI::try_from(v6);
        assert!(dlci.is_err());

        let v7 = 63;
        let dlci = DLCI::try_from(v7);
        assert!(dlci.is_err());
    }

    #[test]
    fn validate_dlci_as_initiator_role() {
        let role = Role::Initiator;

        let dlci = DLCI::MUX_CONTROL_DLCI;
        assert_matches!(dlci.validate(role), Err(RfcommError::InvalidDLCI(_)));

        let dlci = DLCI::MIN_USER_DLCI;
        assert_matches!(dlci.validate(role), Err(RfcommError::InvalidDLCI(_)));

        let dlci = DLCI::try_from(9).unwrap();
        assert!(dlci.validate(role).is_ok());
    }

    #[test]
    fn validate_dlci_as_responder_role() {
        let role = Role::Responder;

        let dlci = DLCI::MUX_CONTROL_DLCI;
        assert_matches!(dlci.validate(role), Err(RfcommError::InvalidDLCI(_)));

        let dlci = DLCI::try_from(7).unwrap();
        assert_matches!(dlci.validate(role), Err(RfcommError::InvalidDLCI(_)));

        let dlci = DLCI::try_from(10).unwrap();
        assert!(dlci.validate(role).is_ok());
    }

    #[test]
    fn validate_dlci_with_invalid_role_returns_error() {
        let role = Role::Unassigned;
        let dlci = DLCI::try_from(10).unwrap();
        assert_matches!(dlci.validate(role), Err(RfcommError::InvalidRole(_)));

        let role = Role::Negotiating;
        let dlci = DLCI::try_from(11).unwrap();
        assert_matches!(dlci.validate(role), Err(RfcommError::InvalidRole(_)));
    }

    #[test]
    fn dlci_check_is_initiator() {
        let dlci = DLCI::MUX_CONTROL_DLCI;
        assert_matches!(dlci.initiator(Role::Initiator), Err(RfcommError::InvalidDLCI(_)));
        assert_matches!(dlci.initiator(Role::Responder), Err(RfcommError::InvalidDLCI(_)));

        let dlci = DLCI::try_from(20).unwrap();
        assert_matches!(dlci.initiator(Role::Initiator), Ok(true));
        assert_matches!(dlci.initiator(Role::Responder), Ok(false));

        let dlci = DLCI::try_from(25).unwrap();
        assert_matches!(dlci.initiator(Role::Initiator), Ok(false));
        assert_matches!(dlci.initiator(Role::Responder), Ok(true));
    }

    #[test]
    fn dlci_check_as_initiator_with_invalid_role_returns_error() {
        let role = Role::Unassigned;
        let dlci = DLCI::try_from(10).unwrap();
        assert_matches!(dlci.initiator(role), Err(RfcommError::InvalidRole(_)));

        let role = Role::Negotiating;
        let dlci = DLCI::try_from(11).unwrap();
        assert_matches!(dlci.initiator(role), Err(RfcommError::InvalidRole(_)));
    }

    #[test]
    fn convert_server_channel_to_dlci_invalid_role() {
        let invalid_role = Role::Unassigned;
        let server_channel = ServerChannel::try_from(10).unwrap();
        assert_matches!(server_channel.to_dlci(invalid_role), Err(_));

        let invalid_role = Role::Negotiating;
        let server_channel = ServerChannel::try_from(13).unwrap();
        assert_matches!(server_channel.to_dlci(invalid_role), Err(_));
    }

    #[test]
    fn convert_server_channel_to_dlci_success() {
        let server_channel = ServerChannel::try_from(5).unwrap();
        let expected_dlci = DLCI::try_from(11).unwrap();
        assert_eq!(server_channel.to_dlci(Role::Initiator).unwrap(), expected_dlci);

        let expected_dlci = DLCI::try_from(10).unwrap();
        assert_eq!(server_channel.to_dlci(Role::Responder).unwrap(), expected_dlci);

        let server_channel = ServerChannel::MIN;
        let expected_dlci = DLCI::try_from(2).unwrap();
        assert_eq!(server_channel.to_dlci(Role::Responder).unwrap(), expected_dlci);

        let server_channel = ServerChannel::MAX;
        let expected_dlci = DLCI::try_from(61).unwrap();
        assert_eq!(server_channel.to_dlci(Role::Initiator).unwrap(), expected_dlci);
    }

    #[test]
    fn server_channel_from_primitive() {
        let normal = 10;
        let sc = ServerChannel::try_from(normal);
        assert!(sc.is_ok());

        let invalid = 0;
        let sc = ServerChannel::try_from(invalid);
        assert_matches!(sc, Err(_));

        let too_large = 31;
        let sc = ServerChannel::try_from(too_large);
        assert_matches!(sc, Err(_));

        let u8_max = std::u8::MAX;
        let sc = ServerChannel::try_from(u8_max);
        assert_matches!(sc, Err(_));
    }
}
