// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_bluetooth_bredr as bredr,
    fuchsia_bluetooth::profile::{DataElement, ProtocolDescriptor},
};

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
