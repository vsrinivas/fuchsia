// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const IPV4_ADDR_LEN: usize = 4;
const MAC_ADDR_LEN: usize = 6;

/// A DHCP protocol message as defined in RFC 2131.
///
/// All fields in `Message` follow the naming conventions outlined in the RFC.
/// Note that `Message` does not expose `htype`, `hlen`, or `hops` fields, as
/// these fields are effectively constants.
pub struct Message {
    pub op: OpCode,
    pub xid: u32,
    pub secs: u16,
    pub bdcast_flag: bool,
    /// `ciaddr` should be stored in Big-Endian order, e.g `[192, 168, 1, 1]`. 
    pub ciaddr: [u8; IPV4_ADDR_LEN],
    /// `yiaddr` should be stored in Big-Endian order, e.g `[192, 168, 1, 1]`.
    pub yiaddr: [u8; IPV4_ADDR_LEN],
    /// `siaddr` should be stored in Big-Endian order, e.g `[192, 168, 1, 1]`.
    pub siaddr: [u8; IPV4_ADDR_LEN],
    /// `giaddr` should be stored in Big-Endian order, e.g `[192, 168, 1, 1]`.
    pub giaddr: [u8; IPV4_ADDR_LEN],
    /// `chaddr` should be stored in Big-Endian order, 
    /// e.g `[0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF]`.
    pub chaddr: [u8; MAC_ADDR_LEN],
    /// `sname` should not exceed 64 characters.
    pub sname: String,
    /// `file` should not exceed 128 characters.
    pub file: String,
    pub options: Vec<ConfigOption>
}

/// A DHCP protocol op-code as defined in RFC 2131.
///
/// `OpCode::BOOTREQUEST` should only appear in protocol messages from the
/// client, and conversely `OpCode::BOOTREPLY` should only appear in messages
/// from the server.
#[derive(Copy, Clone)]
pub enum OpCode {
    BOOTREQUEST = 1,
    BOOTREPLY = 2
}

/// A vendor extension/configuration option for DHCP protocol messages.
///
/// `ConfigOption`s can be fixed or variable length per RFC 1533. When
/// `value` is left empty, the `ConfigOption` will be treated as a fixed
/// length field.
pub struct ConfigOption {
    pub code: u8,
    pub value: Vec<u8>
}