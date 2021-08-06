// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitfield::bitfield;
use std::convert::TryFrom;

use crate::frame::{error::FrameParseError, FrameTypeMarker};
use crate::DLCI;

/// The Address field is the first byte in the frame. See GSM 7.10 Section 5.2.1.2.
pub(crate) const FRAME_ADDRESS_IDX: usize = 0;
bitfield! {
    pub struct AddressField(u8);
    impl Debug;
    pub bool, ea_bit, set_ea_bit: 0;
    pub bool, cr_bit, set_cr_bit: 1;
    pub u8, dlci_raw, set_dlci: 7, 2;
}

impl AddressField {
    /// Returns the DLCI specified by the Address Field.
    pub fn dlci(&self) -> Result<DLCI, FrameParseError> {
        DLCI::try_from(self.dlci_raw())
    }
}

/// The Control field is the second byte in the frame. See GSM 7.10 Section 5.2.1.3.
pub(crate) const FRAME_CONTROL_IDX: usize = 1;
bitfield! {
    pub struct ControlField(u8);
    impl Debug;
    pub bool, poll_final, set_poll_final: 4;
    pub u8, frame_type_raw, set_frame_type: 7, 0;
}

impl ControlField {
    /// Returns the frame type specified by the Control Field.
    pub fn frame_type(&self) -> Result<FrameTypeMarker, FrameParseError> {
        // The P/F bit is ignored when determining Frame Type. See RFCOMM 4.2 and GSM 7.10
        // Section 5.2.1.3.
        const FRAME_TYPE_MASK: u8 = 0b11101111;
        FrameTypeMarker::try_from(self.frame_type_raw() & FRAME_TYPE_MASK)
    }
}

/// The Information field is the third byte in the frame. See GSM 7.10 Section 5.2.1.4.
pub(crate) const FRAME_INFORMATION_IDX: usize = 2;

/// The information field can be represented as two E/A padded octets, each 7-bits wide.
/// This shift is used to access the upper bits of a two-octet field.
/// See GSM 7.10 Section 5.2.1.4.
pub(crate) const INFORMATION_SECOND_OCTET_SHIFT: usize = 7;

bitfield! {
    pub struct InformationField(u8);
    impl Debug;
    pub bool, ea_bit, set_ea_bit: 0;
    pub u8, length, set_length_inner: 7, 1;
}

impl InformationField {
    pub fn set_length(&mut self, length: u8) {
        // The length is only 7 bits wide.
        let mask = 0b1111111;
        self.set_length_inner(length & mask);
    }
}
