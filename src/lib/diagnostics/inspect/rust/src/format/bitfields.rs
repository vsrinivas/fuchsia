// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(missing_docs)]

use bitfield::bitfield;

bitfield! {
    /// Bitfields for writing and reading segments of the header and payload of
    /// inspect VMO blocks.
    /// Represents the header structure of an inspect VMO Block. Not to confuse with
    /// the HEADER block.
    pub struct BlockHeader(u64);
    pub u8, order, set_order: 3, 0;
    pub u8, block_type, set_block_type: 15, 8;

    // Only for a HEADER block
    pub u32, header_version, set_header_version: 31, 16;
    pub u32, header_magic, set_header_magic: 63, 32;

    // Only for *_VALUE blocks
    pub u32, value_name_index, set_value_name_index: 63, 40;
    pub u32, value_parent_index, set_value_parent_index: 39, 16;

    // Only for FREE blocks
    pub u32, free_next_index, set_free_next_index: 39, 16;

    // Only for NAME blocks
    pub u16, name_length, set_name_length: 27, 16;

    // Only for EXTENT blocks
    pub u32, extent_next_index, set_extent_next_index: 39, 16;

    pub value, _: 63, 0;
}

#[allow(missing_docs)]
bitfield! {
    /// Represents the payload of inspect VMO Blocks (except for EXTENT and NAME).
    pub struct Payload(u64);
    pub value, _: 63, 0;

    // Only for PROPERTY blocks
    pub u32, property_total_length, set_property_total_length: 31, 0;
    pub u32, property_extent_index, set_property_extent_index: 59, 32;
    pub u8, property_flags, set_property_flags: 63, 60;

    // Only for INT/UINT/DOUBLE_VALUE blocks
    pub numeric_value, set_numeric_value: 63, 0;

    // Only for HEADER block
    pub header_generation_count, set_header_generation_count: 63, 0;

    // Only for ARRAY_VALUE blocks.
    pub u8, array_entry_type, set_array_entry_type: 3, 0;
    pub u8, array_flags, set_array_flags: 7, 4;
    pub u8, array_slots_count, set_array_slots_count: 15, 8;

    // Only for LINK_VALUE blocks.
    pub u32, content_index, set_content_index: 19, 0;
    pub u8, disposition_flags, set_disposition_flags: 63, 60;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_header() {
        let mut header = BlockHeader(0);
        let magic = 0x494e5350;
        header.set_order(13);
        header.set_block_type(3);
        header.set_header_version(1);
        header.set_header_magic(magic);
        assert_eq!(header.order(), 13);
        assert_eq!(header.header_version(), 1);
        assert_eq!(header.header_magic(), magic);
        assert_eq!(header.value(), 0x494e53500001030d);
    }

    #[test]
    fn test_payload() {
        let mut payload = Payload(0);
        payload.set_property_total_length(0xab);
        payload.set_property_extent_index(0x1234);
        payload.set_property_flags(3);
        assert_eq!(payload.property_total_length(), 0xab);
        assert_eq!(payload.property_extent_index(), 0x1234);
        assert_eq!(payload.property_flags(), 3);
        assert_eq!(payload.value(), 0x30001234000000ab);
    }
}
