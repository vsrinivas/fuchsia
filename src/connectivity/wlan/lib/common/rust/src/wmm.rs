// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::mac;

/// Given an ethernet payload, extract DS field and map it to User Priority if it's an IP packet.
/// Otherwise, return None.
/// Note that this function does not check that the payload is a valid IP packet. It only requires
/// that the payload is long enough to contain the DS field.
pub fn derive_tid(ether_type: u16, payload: &[u8]) -> Option<u8> {
    if ether_type == mac::ETHER_TYPE_IPV4 || ether_type == mac::ETHER_TYPE_IPV6 {
        // DSCP is the 6 most significant bits of the DS field
        return get_ds_field(ether_type, payload).map(|ds| dscp_to_up(ds >> 2));
    }
    None
}

/// Given the 6-bit DSCP from IPv4 or IPv6 header, convert it to User Priority
/// This follows RFC 8325 - https://tools.ietf.org/html/rfc8325#section-4.3
/// For list of DSCP, see https://www.iana.org/assignments/dscp-registry/dscp-registry.xhtml
pub fn dscp_to_up(dscp: u8) -> u8 {
    match dscp {
        // Network Control - CS6, CS7
        0b110000 | 0b111000 => 7,
        // Telephony - EF
        0b101110 => 6,
        // VOICE-ADMIT - VA
        0b101100 => 6,
        // Signaling - CS5
        0b101000 => 5,
        // Multimedia Conferencing - AF41, AF42, AF43
        0b100010 | 0b100100 | 0b100110 => 4,
        // Real-Time Interactive - CS4
        0b100000 => 4,
        // Multimedia Streaming - AF31, AF32, AF33
        0b011010 | 0b011100 | 0b011110 => 4,
        // Broadcast Video - CS3
        0b011000 => 4,
        // Low-Latency Data - AF21, AF22, AF23
        0b010010 | 0b010100 | 0b010110 => 3,
        // Low-Priority Data - CS1
        0b001000 => 1,
        // OAM, High-Throughput Data, Standard, and unused code points
        _ => 0,
    }
}

fn get_ds_field(ether_type: u16, payload: &[u8]) -> Option<u8> {
    if payload.len() < 2 {
        return None;
    }
    match ether_type {
        mac::ETHER_TYPE_IPV4 => Some(payload[1]),
        mac::ETHER_TYPE_IPV6 => Some(((payload[0] & 0x0f) << 4) | ((payload[1] & 0xf0) >> 4)),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_derive_tid_ipv4() {
        let tid = derive_tid(mac::ETHER_TYPE_IPV4, &[0xff, 0b10110000]);
        assert_eq!(tid, Some(6));
    }

    #[test]
    fn test_derive_tid_ipv6() {
        let tid = derive_tid(mac::ETHER_TYPE_IPV6, &[0b11110101, 0b10000000]);
        assert_eq!(tid, Some(3));
    }

    #[test]
    fn test_derive_tid_payload_too_short() {
        assert!(derive_tid(mac::ETHER_TYPE_IPV4, &[0xff]).is_none());
    }

    #[test]
    fn test_dscp_to_up() {
        assert_eq!(dscp_to_up(0b110000), 7);
        // [OAM] CS2; [High-Throughput Data] AF11, AF12, AF13; [Standard] DF (i.e. CS0)
        assert_eq!(dscp_to_up(0b010000), 0);
        assert_eq!(dscp_to_up(0b001010), 0);
        assert_eq!(dscp_to_up(0b001100), 0);
        assert_eq!(dscp_to_up(0b001110), 0);
        assert_eq!(dscp_to_up(0b001110), 0);
        assert_eq!(dscp_to_up(0b000000), 0);

        // Unused code point
        assert_eq!(dscp_to_up(0b111110), 0);
    }
}
