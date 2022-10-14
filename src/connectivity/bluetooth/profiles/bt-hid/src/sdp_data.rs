// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module contains expected values for SDP record for HIDP devices, both for confirming that
///  peers are valid HIDP devices and for testing.
use fidl_fuchsia_bluetooth_bredr as bredr;

pub fn expected_protocols() -> Option<Vec<bredr::ProtocolDescriptor>> {
    Some(vec![
        bredr::ProtocolDescriptor {
            protocol: bredr::ProtocolIdentifier::L2Cap,
            params: vec![bredr::DataElement::Uint16(bredr::PSM_HID_CONTROL)], // PSM for control channel
        },
        bredr::ProtocolDescriptor { protocol: bredr::ProtocolIdentifier::Hidp, params: vec![] },
    ])
}

#[cfg(test)]
pub mod test {
    use super::*;
    use crate::{peer_info::PeerInfo, types::DescriptorList};

    macro_rules! make_attribute {
        ($ty: ident, $id: expr, $value: expr) => {
            bredr::Attribute { id: $id, element: bredr::DataElement::$ty($value) }
        };
    }

    pub fn attributes() -> Vec<bredr::Attribute> {
        vec![
            make_attribute!(Uint16, 0x201, 1),            // Parser version
            make_attribute!(Uint8, 0x202, 2),             // Device subclass
            make_attribute!(Uint8, 0x203, 3),             // Country code
            make_attribute!(B, 0x204, true),              // Virtual cable
            make_attribute!(B, 0x205, false),             // Reconnect initiate
            make_attribute!(Sequence, 0x206, Vec::new()), // Descriptor list
            make_attribute!(B, 0x209, true),              // Battery power
            make_attribute!(B, 0x20A, false),             // Remote wake
            make_attribute!(Uint16, 0x20C, 4),            // Supervision timeout
            make_attribute!(B, 0x20D, true),              // Normally connectable
            make_attribute!(B, 0x20E, false),             // Boot device
            make_attribute!(Uint16, 0x20F, 5),            // SSR max latency
            make_attribute!(Uint16, 0210, 6),             // SSR min latency
        ]
    }

    pub fn attributes_missing_optional() -> Vec<bredr::Attribute> {
        vec![
            make_attribute!(Uint16, 0x201, 1),            // Parser version
            make_attribute!(Uint8, 0x202, 2),             // Device subclass
            make_attribute!(Uint8, 0x203, 3),             // Country code
            make_attribute!(B, 0x204, true),              // Virtual cable
            make_attribute!(B, 0x205, false),             // Reconnect initiate
            make_attribute!(Sequence, 0x206, Vec::new()), // Descriptor list
            make_attribute!(B, 0x209, true),              // Battery power
            make_attribute!(B, 0x20A, false),             // Remote wake
            make_attribute!(Uint16, 0x20C, 4),            // Supervision timeout
            make_attribute!(B, 0x20D, true),              // Normally connectable
            make_attribute!(B, 0x20E, false),             // Boot device
            make_attribute!(Uint16, 0x20F, 5),            // SSR max latency
                                                          // Missing optional attribute SSR min latency
        ]
    }

    pub fn attributes_missing_required() -> Vec<bredr::Attribute> {
        vec![
            make_attribute!(Uint16, 0x201, 1),            // Parser version
            make_attribute!(Uint8, 0x202, 2),             // Device subclass
            make_attribute!(Uint8, 0x203, 3),             // Country code
            make_attribute!(B, 0x204, true),              // Virtual cable
            make_attribute!(B, 0x205, false),             // Reconnect initiate
            make_attribute!(Sequence, 0x206, Vec::new()), // Descriptor list
            make_attribute!(B, 0x209, true),              // Battery power
            make_attribute!(B, 0x20A, false),             // Remote wake
            make_attribute!(Uint16, 0x20C, 4),            // Supervision timeout
            make_attribute!(B, 0x20D, true),              // Normally connectable
            // Missing requirted attribute boot device
            make_attribute!(Uint16, 0x20F, 5), // Ssr max latency
            make_attribute!(Uint16, 0210, 6),  // Ssr min latency
        ]
    }

    pub fn expected_peer_info() -> PeerInfo {
        PeerInfo {
            parser_version: Some(1),
            device_subclass: Some(2),
            country_code: Some(3),
            virtual_cable: Some(true),
            reconnect_initiate: Some(false),
            descriptor_list: Some(DescriptorList(Vec::new())),
            battery_power: Some(true),
            remote_wake: Some(false),
            supervision_timeout: Some(4),
            normally_connectable: Some(true),
            boot_device: Some(false),
            ssr_host_max_latency: Some(5),
            ssr_host_min_timeout: Some(6),
        }
    }
}
