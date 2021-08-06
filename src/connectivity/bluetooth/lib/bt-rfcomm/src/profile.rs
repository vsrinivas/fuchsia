// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth_bredr as bredr;
use fuchsia_bluetooth::profile::{DataElement, ProtocolDescriptor};
use std::convert::TryFrom;

use crate::dlci::ServerChannel;

/// Returns a ProtocolDescriptorList for an RFCOMM service with the provided `server_channel`.
pub fn build_rfcomm_protocol(server_channel: ServerChannel) -> Vec<ProtocolDescriptor> {
    // The PSM for L2CAP is empty when RFCOMM is used.
    vec![
        ProtocolDescriptor { protocol: bredr::ProtocolIdentifier::L2Cap, params: vec![] },
        ProtocolDescriptor {
            protocol: bredr::ProtocolIdentifier::Rfcomm,
            params: vec![DataElement::Uint8(server_channel.into())],
        },
    ]
}

/// Returns true if the provided `protocol` is RFCOMM.
///
/// Protocols are generally specified by a vector of ProtocolDescriptors which
/// are ordered from lowest level (typically L2CAP) to highest.
pub fn is_rfcomm_protocol(protocol: &Vec<ProtocolDescriptor>) -> bool {
    protocol.iter().any(|descriptor| descriptor.protocol == bredr::ProtocolIdentifier::Rfcomm)
}

/// Returns the ServerChannel number from the provided `protocol` or None if it
/// does not exist.
pub fn server_channel_from_protocol(protocol: &Vec<ProtocolDescriptor>) -> Option<ServerChannel> {
    for descriptor in protocol {
        if descriptor.protocol == bredr::ProtocolIdentifier::Rfcomm {
            // If the Protocol is RFCOMM, there should be one element with the Server Channel.
            if descriptor.params.len() != 1 {
                return None;
            }

            if let DataElement::Uint8(sc) = descriptor.params[0] {
                return ServerChannel::try_from(sc).ok();
            }
            return None;
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn server_channel_from_l2cap_is_none() {
        // Just L2CAP - should be no server channel.
        let protocol = vec![ProtocolDescriptor {
            protocol: bredr::ProtocolIdentifier::L2Cap,
            params: vec![DataElement::Uint16(bredr::PSM_AVDTP)],
        }];
        assert_eq!(server_channel_from_protocol(&protocol), None);
    }

    #[test]
    fn server_channel_from_empty_rfcomm_is_none() {
        // RFCOMM but un-allocated Server Channel.
        let protocol = vec![
            ProtocolDescriptor { protocol: bredr::ProtocolIdentifier::L2Cap, params: vec![] },
            ProtocolDescriptor { protocol: bredr::ProtocolIdentifier::Rfcomm, params: vec![] },
        ];
        assert_eq!(server_channel_from_protocol(&protocol), None);
    }

    #[test]
    fn server_channel_from_invalid_rfcomm_is_none() {
        // RFCOMM but invalidly formatted.
        let protocol = vec![
            ProtocolDescriptor { protocol: bredr::ProtocolIdentifier::L2Cap, params: vec![] },
            ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::Rfcomm,
                params: vec![DataElement::Uint16(100)], // Should be Uint8
            },
        ];
        assert_eq!(server_channel_from_protocol(&protocol), None);
    }

    #[test]
    fn invalid_server_channel_number_is_none() {
        // Valid RFCOMM but ServerChannel number is invalid.
        let protocol = vec![
            ProtocolDescriptor { protocol: bredr::ProtocolIdentifier::L2Cap, params: vec![] },
            ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::Rfcomm,
                params: vec![DataElement::Uint8(200)], // Too large
            },
        ];
        assert_eq!(server_channel_from_protocol(&protocol), None);
    }

    #[test]
    fn server_channel_from_valid_rfcomm_is_present() {
        // RFCOMM service with assigned server channel.
        let sc = 10;
        let protocol = vec![
            ProtocolDescriptor { protocol: bredr::ProtocolIdentifier::L2Cap, params: vec![] },
            ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::Rfcomm,
                params: vec![DataElement::Uint8(sc)],
            },
        ];
        let expected = ServerChannel::try_from(sc).ok();
        assert_eq!(server_channel_from_protocol(&protocol), expected);
    }

    #[test]
    fn server_channel_from_only_rfcomm_protocol_is_present() {
        // While unusual, getting the server channel from a protocol descriptor list that only
        // contains RFCOMM is ok.
        let sc = 12;
        let protocol = vec![ProtocolDescriptor {
            protocol: bredr::ProtocolIdentifier::Rfcomm,
            params: vec![DataElement::Uint8(sc)],
        }];
        let expected = ServerChannel::try_from(sc).ok();
        assert_eq!(server_channel_from_protocol(&protocol), expected);
    }
}
