// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! PCAPNG block definitions.

use {
    packet::BufferView,
    zerocopy::{AsBytes, FromBytes, Unaligned},
};

use crate::Result;

// The `is_valid_block` calls on each block should pass with good test vectors.
#[test]
fn good_format_test() -> Result {
    let shb = SectionHeader {
        btype: 0x0A0D0D0A,
        tot_length: 68,
        magic: 0x1A2B3C4D,
        major: 1,
        minor: 0,
        section_length: -1,
        tot_length2: 68,
    };

    let idb = InterfaceDescription {
        btype: 1,
        tot_length: 24,
        linktype: 1,
        reserved: 0xFFFF,
        snaplen: 0xABCD,
        tot_length2: 24,
    };

    let spb = SimplePacket { btype: 3, tot_length: 56, packet_length: 40 };

    assert!(shb.is_valid_block().is_ok());
    assert!(idb.is_valid_block().is_ok());
    assert!(spb.is_valid_block().is_ok());
    Ok(())
}

// The `is_valid_block` calls on each block should fail given invalid data.
#[test]
fn bad_format_test() -> Result {
    let shb: [u8; SECTION_HEADER_LENGTH] = [37; SECTION_HEADER_LENGTH];
    let idb: [u8; INTERFACE_DESCRIPTION_LENGTH] = [24; INTERFACE_DESCRIPTION_LENGTH];
    let spb: [u8; SIMPLE_PACKET_LENGTH] = [42; SIMPLE_PACKET_LENGTH];

    let mut shb_slice = &mut &shb[..];
    let mut idb_slice = &mut &idb[..];
    let mut spb_slice = &mut &spb[..];

    let section_header = shb_slice.take_obj_front::<SectionHeader>().unwrap();
    let interface_description = idb_slice.take_obj_front::<InterfaceDescription>().unwrap();
    let simple_packet = spb_slice.take_obj_front::<SimplePacket>().unwrap();

    assert!(section_header.into_ref().is_valid_block().is_err());
    assert!(interface_description.into_ref().is_valid_block().is_err());
    assert!(simple_packet.into_ref().is_valid_block().is_err());

    Ok(())
}

// Consume section header and interface description blocks.
// Returns unconsumed left-over `buf_mut`.
pub fn consume_shb_idb<'a>(mut buf_mut: &'a mut &'a [u8]) -> Result<&'a mut &'a [u8]> {
    let section_header = buf_mut
        .take_obj_front::<SectionHeader>()
        .expect("Data cannot be read as a section header block!");
    section_header.into_ref().is_valid_block()?;

    let interface_description = buf_mut
        .take_obj_front::<InterfaceDescription>()
        .expect("Data cannot be read as an interface description block!");
    interface_description.into_ref().is_valid_block()?;
    Ok(buf_mut)
}

// Parse a simple packet block.
// Returns (unconsumed left-over `buf_mut`, packet, packet size).
pub fn parse_simple_packet<'a>(
    mut buf_mut: &'a mut &'a [u8],
) -> Result<(&'a mut &'a [u8], &'a [u8], usize)> {
    let simple_packet_slice = buf_mut.take_obj_front::<SimplePacket>().unwrap();
    let simple_packet = simple_packet_slice.into_ref();
    simple_packet.is_valid_block()?;

    let packet = buf_mut.take_front(with_padding(simple_packet.packet_length) as usize).unwrap();
    // The trailing total length of each simple packet block must equal the one at the start.
    let simple_packet_trailer = buf_mut.take_obj_front::<SimplePacketTrailer>().unwrap();
    let tot_length = simple_packet.tot_length;
    let tot_length2 = simple_packet_trailer.tot_length2;
    assert_eq!(tot_length, tot_length2);

    Ok((buf_mut, packet, simple_packet.packet_length as usize))
}

// Macros to check values and generate `Errors`.
macro_rules! check_align {
    ($value:expr) => {
        if ($value) % 4 == 0 {
            Ok(())
        } else {
            Err(anyhow::anyhow!(
                "{} = {} is not a multiple of 4 octets.",
                stringify!($value),
                $value
            ))
        }
    };
}

macro_rules! min_value {
    ($value:expr, $min:expr) => {
        if ($value) >= ($min) {
            Ok(())
        } else {
            Err(anyhow::anyhow!("{} = {} is not at least {}.", stringify!($value), $value, $min))
        }
    };
}

macro_rules! eq_value {
    ($value:ident, $exp:expr) => {
        if ($value) == ($exp) {
            Ok(())
        } else {
            Err(anyhow::anyhow!(
                "Incorrect value of {}, got {}, expected {}.",
                stringify!($value),
                $value,
                $exp
            ))
        }
    };
}

// Trait for a PCAPNG block. All length values are in octets.
pub trait PcapngBlock {
    // Minimum valid length for the block.
    fn minimum_length(&self) -> u32;

    // Total length for the block.
    fn total_length(&self) -> u32;

    // True if total length is valid.
    fn has_valid_total_length(&self) -> Result {
        let total_length = self.total_length();
        check_align!(total_length)?;
        min_value!(total_length, self.minimum_length())
    }

    // Assert that the block data layout meets the specifications.
    fn is_valid_block(&self) -> Result;
}

// PCAPNG section header block with no options.
// Must be `Unaligned` to work with packet parsing library.
#[repr(packed)]
#[derive(FromBytes, AsBytes, Unaligned)]
pub struct SectionHeader {
    pub(crate) btype: u32,
    pub(crate) tot_length: u32,
    pub(crate) magic: u32,
    pub(crate) major: u16,
    pub(crate) minor: u16,
    pub(crate) section_length: i64,
    pub(crate) tot_length2: u32,
}
pub const SECTION_HEADER_LENGTH: usize = 28; // octets.

// PCAPNG interface description block.
#[repr(packed)]
#[derive(FromBytes, AsBytes, Unaligned)]
pub struct InterfaceDescription {
    pub(crate) btype: u32,
    pub(crate) tot_length: u32,
    pub(crate) linktype: u16,
    pub(crate) reserved: u16,
    pub(crate) snaplen: u32,
    pub(crate) tot_length2: u32,
}
pub const INTERFACE_DESCRIPTION_LENGTH: usize = 20; // octets.

// PCAPNG simple packet block.
// This is the header portion. In a capture it will be followed by packet data, padded to a multiple
// of 4 octets, then `tot_length` again.
#[repr(packed)]
#[derive(FromBytes, AsBytes, Unaligned)]
pub struct SimplePacket {
    pub(crate) btype: u32,
    pub(crate) tot_length: u32,
    pub(crate) packet_length: u32,
}
pub const SIMPLE_PACKET_LENGTH: usize = 12; // octets.

// The trailing total length for the simple packet block.
#[repr(packed)]
#[derive(FromBytes, AsBytes, Unaligned)]
pub struct SimplePacketTrailer {
    pub(crate) tot_length2: u32,
}

impl PcapngBlock for SectionHeader {
    fn minimum_length(&self) -> u32 {
        SECTION_HEADER_LENGTH as u32
    }

    fn total_length(&self) -> u32 {
        self.tot_length
    }

    fn is_valid_block(&self) -> Result {
        let btype = self.btype; // Avoid borrow from unaligned struct in `assert_eq`.
        eq_value!(btype, 0x0A0D0D0A)?; // Magic value.

        self.has_valid_total_length()?;

        let magic = self.magic;
        eq_value!(magic, 0x1A2B3C4D)?; // Magic value.

        let major = self.major;
        eq_value!(major, 1)?; // Magic value.

        let minor = self.minor;
        eq_value!(minor, 0)?; // Magic value.

        let section_length = self.section_length;
        if section_length >= 0 {
            check_align!(section_length)?;
        } else {
            eq_value!(section_length, -1)?;
        }

        let (tot_length2, tot_length) = (self.tot_length2, self.tot_length);
        eq_value!(tot_length2, tot_length)
    }
}

impl PcapngBlock for InterfaceDescription {
    fn minimum_length(&self) -> u32 {
        INTERFACE_DESCRIPTION_LENGTH as u32
    }
    fn total_length(&self) -> u32 {
        self.tot_length
    }

    fn is_valid_block(&self) -> Result {
        let btype = self.btype;
        eq_value!(btype, 1)?; // Magic value.

        self.has_valid_total_length()?;

        let linktype = self.linktype;
        eq_value!(linktype, 1)?; // Magic value for Ethernet linktype.

        // No checks required for `reserved` and `snaplen`, but refer so they are not dead code.
        let _ = (self.reserved, self.snaplen);

        let (tot_length2, tot_length) = (self.tot_length2, self.tot_length);
        eq_value!(tot_length2, tot_length)
    }
}

/// Round up `packet_length` in octets to a multiple of 4 (32 bits).
pub fn with_padding(packet_length: u32) -> u32 {
    ((packet_length + 3) / 4) * 4
}

impl PcapngBlock for SimplePacket {
    fn minimum_length(&self) -> u32 {
        // + 4 for second block total length field after packet data.
        SIMPLE_PACKET_LENGTH as u32 + 4
    }
    fn total_length(&self) -> u32 {
        self.tot_length
    }

    fn is_valid_block(&self) -> Result {
        let btype = self.btype;
        eq_value!(btype, 3)?; // Magic value.

        self.has_valid_total_length()?;
        let total_length = self.tot_length;
        let packet_length = self.packet_length;
        eq_value!(total_length, with_padding(packet_length) + self.minimum_length())
    }
}
