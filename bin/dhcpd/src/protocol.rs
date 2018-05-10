// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(warnings)]
#![allow(unused)] // TODO(atait): Remove once there are non-test clients

const IPV4_ADDR_LEN: usize = 4;
const MAC_ADDR_LEN: usize = 6;

const OPTIONS_START_IDX: usize = 236;

const ETHERNET_HTYPE: u8 = 1;
const ETHERNET_HLEN: u8 = 6;
const HOPS_DEFAULT: u8 = 0;

const UNUSED_CHADDR_BYTES: usize = 10;

const SNAME_LEN: usize = 64;
const FILE_LEN: usize = 128;

const END_OP: u8 = 255;

const ONE_BYTE_LEN: usize = 8;
const TWO_BYTE_LEN: usize = 16;
const THREE_BYTE_LEN: usize = 24;

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
    pub options: Vec<ConfigOption>,
}

impl Message {
    /// Instantiates a new `Message` with default field values.
    pub fn new() -> Message {
        let msg = Message {
            op: OpCode::BOOTREQUEST,
            xid: 0,
            secs: 0,
            bdcast_flag: false,
            ciaddr: [0; IPV4_ADDR_LEN],
            yiaddr: [0; IPV4_ADDR_LEN],
            siaddr: [0; IPV4_ADDR_LEN],
            giaddr: [0; IPV4_ADDR_LEN],
            chaddr: [0; MAC_ADDR_LEN],
            sname: String::from(""),
            file: String::from(""),
            options: Vec::new(),
        };
        msg
    }

    /// Consumes the calling `Message` to serialize it into a buffer of bytes.
    pub fn serialize(&self) -> Vec<u8> {
        let mut buffer = Vec::with_capacity(OPTIONS_START_IDX);
        buffer.push(self.op as u8);
        buffer.push(ETHERNET_HTYPE);
        buffer.push(ETHERNET_HLEN);
        buffer.push(HOPS_DEFAULT);
        buffer.push((self.xid >> THREE_BYTE_LEN) as u8);
        buffer.push((self.xid >> TWO_BYTE_LEN) as u8);
        buffer.push((self.xid >> ONE_BYTE_LEN) as u8);
        buffer.push(self.xid as u8);
        buffer.push((self.secs >> ONE_BYTE_LEN) as u8);
        buffer.push(self.secs as u8);
        if self.bdcast_flag {
            // Set most significant bit.
            buffer.push(128u8);
        } else {
            buffer.push(0u8);
        }
        buffer.push(0u8);
        buffer.extend_from_slice(&self.ciaddr);
        buffer.extend_from_slice(&self.yiaddr);
        buffer.extend_from_slice(&self.siaddr);
        buffer.extend_from_slice(&self.giaddr);
        buffer.extend_from_slice(&self.chaddr);
        buffer.extend_from_slice(&[0u8; UNUSED_CHADDR_BYTES]);
        self.trunc_string_to_n_and_push(&self.sname, SNAME_LEN, &mut buffer);
        self.trunc_string_to_n_and_push(&self.file, FILE_LEN, &mut buffer);
        buffer.extend_from_slice(&self.serialize_options());
        buffer
    }

    fn trunc_string_to_n_and_push(&self, s: &str, n: usize, buffer: &mut Vec<u8>) {
        if s.len() > n {
            let truncated = s.split_at(n);
            buffer.extend(truncated.0.as_bytes());
            return;
        }
        buffer.extend(s.as_bytes());
        let unused_bytes = n - s.len();
        let old_len = buffer.len();
        buffer.resize(old_len + unused_bytes, 0);
    }

    fn serialize_options(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        for option in &self.options {
            option.serialize_to(&mut bytes);
        }
        bytes.push(END_OP);
        bytes
    }
}

#[test]
fn test_serialize_returns_correct_bytes() {
    let mut msg = Message::new();
    msg.xid = 42;
    msg.secs = 1024;
    msg.yiaddr = [192, 168, 1, 1];
    msg.sname = String::from("relay.example.com");
    msg.file = String::from("boot.img");
    msg.options.push(ConfigOption {
        code: 1u8,
        value: vec![255, 255, 255, 0],
    });

    let bytes = msg.serialize();

    assert_eq!(bytes.len(), 243);
    assert_eq!(bytes[0], 1u8);
    assert_eq!(bytes[1], 1u8);
    assert_eq!(bytes[2], 6u8);
    assert_eq!(bytes[3], 0u8);
    assert_eq!(bytes[7], 42u8);
    assert_eq!(bytes[8], 4u8);
    assert_eq!(bytes[16], 192u8);
    assert_eq!(bytes[17], 168u8);
    assert_eq!(bytes[18], 1u8);
    assert_eq!(bytes[19], 1u8);
    assert_eq!(bytes[44], 'r' as u8);
    assert_eq!(bytes[60], 'm' as u8);
    assert_eq!(bytes[61], 0u8);
    assert_eq!(bytes[108], 'b' as u8);
    assert_eq!(bytes[115], 'g' as u8);
    assert_eq!(bytes[116], 0u8);
    assert_eq!(bytes[bytes.len() - 1], 255u8);
}

/// A DHCP protocol op-code as defined in RFC 2131.
///
/// `OpCode::BOOTREQUEST` should only appear in protocol messages from the
/// client, and conversely `OpCode::BOOTREPLY` should only appear in messages
/// from the server.
#[derive(Copy, Clone)]
pub enum OpCode {
    BOOTREQUEST = 1,
    BOOTREPLY = 2,
}

/// A vendor extension/configuration option for DHCP protocol messages.
///
/// `ConfigOption`s can be fixed or variable length per RFC 1533. When
/// `value` is left empty, the `ConfigOption` will be treated as a fixed
/// length field.
pub struct ConfigOption {
    pub code: u8,
    pub value: Vec<u8>,
}

impl ConfigOption {
    fn serialize_to(&self, output: &mut Vec<u8>) {
        if !self.is_valid_code() {
            return;
        }
        output.push(self.code);
        let len = self.value.len() as u8;
        if len > 0 {
            output.push(len);
        }
        output.extend(&self.value);
    }

    fn is_valid_code(&self) -> bool {
        // code is between 0 and 61, inclusive, or is 255
        self.code <= 61 || self.code == 255
    }
}

#[test]
fn test_serialize_with_valid_option_returns_correct_bytes() {
    let opt = ConfigOption {
        code: 1,
        value: vec![255, 255, 255, 0],
    };
    let mut bytes = Vec::new();
    opt.serialize_to(&mut bytes);
    assert_eq!(bytes.len(), 6);
    assert_eq!(bytes[0], 1);
    assert_eq!(bytes[1], 4);
    assert_eq!(bytes[2], 255);
    assert_eq!(bytes[3], 255);
    assert_eq!(bytes[4], 255);
    assert_eq!(bytes[5], 0);
}

#[test]
fn test_serialize_with_invalid_code_returns_none() {
    let opt = ConfigOption {
        code: 100,
        value: vec![42],
    };
    let mut bytes = Vec::new();
    opt.serialize_to(&mut bytes);
    assert!(bytes.is_empty());
}

#[test]
fn test_serialize_with_fixed_len_option_returns_correct_bytes() {
    let opt = ConfigOption {
        code: 255,
        value: vec![],
    };
    let mut bytes = Vec::new();
    opt.serialize_to(&mut bytes);
    assert_eq!(bytes.len(), 1);
    assert_eq!(bytes[0], 255);
}
