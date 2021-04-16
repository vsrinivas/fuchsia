// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # Inspect bitfields
//!
//! This module contains the bitfield definitions of the [`Inspect VMO format`][inspect-vmo].
//!
//! [inspect-vmo]: https://fuchsia.dev/fuchsia-src/reference/diagnostics/inspect/vmo-format

use bitfield::bitfield;

bitfield! {
    /// Bitfields for writing and reading segments of the header and payload of
    /// inspect VMO blocks.
    /// Represents the header structure of an inspect VMO Block. Not to confuse with
    /// the `HEADER` block.
    pub struct BlockHeader(u64);

    /// The size of a block given as a bit shift from the minimum size.
    /// `size_in_bytes = 16 << order`. Separates blocks into classes by their (power of two) size.
    pub u8, order, set_order: 3, 0;

    /// The type of the block. Determines how the rest of the bytes are interpreted.
    /// - 0: Free
    /// - 1: Reserved
    /// - 2: Header
    /// - 3: Node
    /// - 4: Int value
    /// - 5: Uint value
    /// - 6: Double value
    /// - 7: Buffer value
    /// - 8: Extent
    /// - 9: Name
    /// - 10: Tombstone
    /// - 11: Array value
    /// - 12: Link value
    /// - 13: Bool value
    pub u8, block_type, set_block_type: 15, 8;

    /// Only for a `HEADER` block. The version number. Currently 1.
    pub u32, header_version, set_header_version: 31, 16;

    /// Only for a `HEADER` block. The magic number "INSP".
    pub u32, header_magic, set_header_magic: 63, 32;

    /// Only for `*_VALUE` blocks. The index of the `NAME` block of associated with this value.
    pub u32, value_name_index, set_value_name_index: 63, 40;

    /// Only for `*_VALUE` blocks. The index of the parent of this value.
    pub u32, value_parent_index, set_value_parent_index: 39, 16;

    /// Only for `FREE` blocks. The index of the next free block.
    pub u32, free_next_index, set_free_next_index: 39, 16;

    /// Only for `NAME` blocks. The length of the string.
    pub u16, name_length, set_name_length: 27, 16;

    /// Only for `EXTENT` blocks. The index of the next `EXTENT` block.
    pub u32, extent_next_index, set_extent_next_index: 39, 16;

    /// The raw 64 bits of the header section of the block.
    pub value, _: 63, 0;
}

bitfield! {
    /// Represents the payload of inspect VMO Blocks (except for `EXTENT` and `NAME`).
    pub struct Payload(u64);

    /// The raw 64 bits of the payload section of the block.
    pub value, _: 63, 0;

    /// Only for `BUFFER` blocks. The total size of the buffer.
    pub u32, property_total_length, set_property_total_length: 31, 0;

    /// Only for `BUFFER` blocks. The index of the first `EXTENT` block of this buffer.
    pub u32, property_extent_index, set_property_extent_index: 59, 32;

    /// Only for `BUFFER` blocks. The buffer flags of this block indicating its display format.
    /// 0: utf-8 string
    /// 1: binary array
    pub u8, property_flags, set_property_flags: 63, 60;

    /// Only for `INT/UINT/DOUBLE_VALUE` blocks. The numeric value of the block, this number has to
    /// be casted to its type for `INT` and `DOUBLE` blocks.
    pub numeric_value, set_numeric_value: 63, 0;

    /// Only for the `HEADER` block. The generation count of the header, used for implementing
    /// locking.
    pub header_generation_count, set_header_generation_count: 63, 0;

    /// Only for `ARRAY_VALUE` blocks. The type of each entry in the array (int, uint, double).
    /// 0: Int
    /// 1: Uint
    /// 2: Double
    pub u8, array_entry_type, set_array_entry_type: 3, 0;

    /// Only for `ARRAY_VALUE` blocks. The display format of the block (default, linear histogram,
    /// exponential histogram)
    /// 0: Regular array
    /// 1: Linear histogram
    /// 2: Exponential histogram
    pub u8, array_flags, set_array_flags: 7, 4;

    /// Only for `ARRAY_VALUE` blocks. The nmber of entries in the array.
    pub u8, array_slots_count, set_array_slots_count: 15, 8;

    /// Only for `LINK_VALUE` blocks. Index of the content of this link (as a `NAME` node)
    pub u32, content_index, set_content_index: 19, 0;

    /// Only for `LINK_VALUE`. Instructs readers whether to use child or inline disposition.
    /// 0: child
    /// 1: inline
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
