// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(warnings)]
#![allow(unused)] // TODO(atait): Remove once there are non-test clients

extern crate byteorder;

const IPV4_ADDR_LEN: usize = 4;
const MAC_ADDR_LEN: usize = 6;

const OP_IDX: usize = 0;
const HTYPE_IDX: usize = 1;
const HLEN_IDX: usize = 2;
const HOPS_IDX: usize = 3;
const XID_IDX: usize = 4;
const SECS_IDX: usize = 8;
const FLAGS_IDX: usize = 10;
const CIADDR_IDX: usize = 12;
const YIADDR_IDX: usize = 16;
const SIADDR_IDX: usize = 20;
const GIADDR_IDX: usize = 24;
const CHADDR_IDX: usize = 28;
const SNAME_IDX: usize = 44;
const FILE_IDX: usize = 108;
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
#[derive(Debug, PartialEq)]
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
    pub fn new() -> Self {
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

    /// Instantiates a new `Message` from a byte buffer conforming to the DHCP
    /// protocol as defined RFC 2131. Returns `None` if the buffer is malformed.
    /// Any malformed configuration options will be skipped over, leaving only
    /// well formed `ConfigOption`s in the final `Message`.
    pub fn from_buffer(buf: &[u8]) -> Option<Self> {
        use self::byteorder::{BigEndian, ByteOrder};

        if buf.len() < OPTIONS_START_IDX {
            return None;
        }

        let mut msg = Message::new();
        let op = buf.get(OP_IDX)?;
        msg.op = OpCode::from(*op)?;
        msg.xid = BigEndian::read_u32(&buf[XID_IDX..SECS_IDX]);
        msg.secs = BigEndian::read_u16(&buf[SECS_IDX..FLAGS_IDX]);
        msg.bdcast_flag = buf[FLAGS_IDX] > 0;
        Message::buf_into_addr(&buf[CIADDR_IDX..YIADDR_IDX], &mut msg.ciaddr);
        Message::buf_into_addr(&buf[YIADDR_IDX..SIADDR_IDX], &mut msg.yiaddr);
        Message::buf_into_addr(&buf[SIADDR_IDX..GIADDR_IDX], &mut msg.siaddr);
        Message::buf_into_addr(&buf[GIADDR_IDX..CHADDR_IDX], &mut msg.giaddr);
        Message::buf_into_addr(&buf[CHADDR_IDX..CHADDR_IDX + 6], &mut msg.chaddr);
        msg.sname = Message::buf_to_msg_string(&buf[SNAME_IDX..FILE_IDX])?;
        msg.file = Message::buf_to_msg_string(&buf[FILE_IDX..OPTIONS_START_IDX])?;
        Message::buf_into_options(&buf[OPTIONS_START_IDX..], &mut msg.options);

        Some(msg)
    }

    fn buf_into_addr(buf: &[u8], addr: &mut [u8]) {
        addr.copy_from_slice(buf);
    }

    fn buf_to_msg_string(buf: &[u8]) -> Option<String> {
        use std::str;
        let maybe_string = str::from_utf8(buf);
        match maybe_string.ok() {
            Some(string) => Some(string.trim_right_matches('\x00').to_string()),
            None => None,
        }
    }

    fn buf_into_options(buf: &[u8], options: &mut Vec<ConfigOption>) {
        let mut opt_idx = 0;
        while opt_idx < buf.len() && buf[opt_idx] != END_OP {
            let opt_code = buf[opt_idx];
            let opt_len: usize = match opt_code {
                0 | END_OP => 1, // fixed length option
                _ => match buf.get(opt_idx + 1) {
                    // variable length option
                    Some(len) => (len + 2) as usize,
                    None => 1 as usize,
                },
            };
            let maybe_opt = ConfigOption::from_buffer(&buf[opt_idx..opt_idx + opt_len]);
            opt_idx += opt_len;
            let opt = match maybe_opt {
                Some(opt) => opt,
                None => continue,
            };
            options.push(opt);
        }
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

#[test]
fn test_message_from_buffer_returns_correct_message() {
    use std::string::ToString;

    let mut buf = Vec::new();
    buf.push(1u8);
    buf.push(1u8);
    buf.push(6u8);
    buf.push(0u8);
    buf.extend_from_slice(b"\x00\x00\x00\x2A");
    buf.extend_from_slice(b"\x04\x00");
    buf.extend_from_slice(b"\x00\x00");
    buf.extend_from_slice(b"\x00\x00\x00\x00");
    buf.extend_from_slice(b"\xC0\xA8\x01\x01");
    buf.extend_from_slice(b"\x00\x00\x00\x00");
    buf.extend_from_slice(b"\x00\x00\x00\x00");
    buf.extend_from_slice(b"\x00\x00\x00\x00\x00\x00");
    buf.extend_from_slice(b"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00");
    buf.extend_from_slice(b"relay.example.com");
    let mut old_len = buf.len();
    let mut unused_bytes = SNAME_LEN - b"relay.example.com".len();
    buf.resize(old_len + unused_bytes, 0u8);
    buf.extend_from_slice(b"boot.img");
    old_len = buf.len();
    unused_bytes = FILE_LEN - b"boot.img".len();
    buf.resize(old_len + unused_bytes, 0u8);
    buf.extend_from_slice(b"\x01\x04\xFF\xFF\xFF\x00");
    buf.extend_from_slice(b"\x00");
    buf.extend_from_slice(b"\x00");
    buf.extend_from_slice(b"\x0D\x02\xAA\xBB");
    buf.extend_from_slice(b"\xFF");

    let got = Message::from_buffer(&buf).unwrap();

    let opt_want1 = ConfigOption {
        code: 1,
        value: vec![255, 255, 255, 0],
    };
    let opt_want2 = ConfigOption {
        code: 0,
        value: vec![],
    };
    let opt_want3 = ConfigOption {
        code: 0,
        value: vec![],
    };
    let opt_want4 = ConfigOption {
        code: 13,
        value: vec![0xAA, 0xBB],
    };
    let want = Message {
        op: OpCode::BOOTREQUEST,
        xid: 42,
        secs: 1024,
        bdcast_flag: false,
        ciaddr: [0, 0, 0, 0],
        yiaddr: [192, 168, 1, 1],
        siaddr: [0, 0, 0, 0],
        giaddr: [0, 0, 0, 0],
        chaddr: [0, 0, 0, 0, 0, 0],
        sname: "relay.example.com".to_string(),
        file: "boot.img".to_string(),
        options: vec![opt_want1, opt_want2, opt_want3, opt_want4],
    };

    assert_eq!(got, want);
}

#[test]
fn test_message_from_too_short_buffer_returns_none() {
    let buf = vec![0u8, 0u8, 0u8];

    let got = Message::from_buffer(&buf);

    assert_eq!(got, None);
}

/// A DHCP protocol op-code as defined in RFC 2131.
///
/// `OpCode::BOOTREQUEST` should only appear in protocol messages from the
/// client, and conversely `OpCode::BOOTREPLY` should only appear in messages
/// from the server.
#[derive(Copy, Clone, Debug, PartialEq)]
pub enum OpCode {
    BOOTREQUEST = 1,
    BOOTREPLY = 2,
}

impl OpCode {
    /// Instantiates an `OpCode` from its `u8` raw value. Returns None for an
    /// invalid `OpCode` raw value.
    pub fn from(val: u8) -> Option<Self> {
        match val {
            1 => Some(OpCode::BOOTREQUEST),
            2 => Some(OpCode::BOOTREPLY),
            _ => None,
        }
    }
}

/// A vendor extension/configuration option for DHCP protocol messages.
///
/// `ConfigOption`s can be fixed or variable length per RFC 1533. When
/// `value` is left empty, the `ConfigOption` will be treated as a fixed
/// length field.
#[derive(Debug, PartialEq)]
pub struct ConfigOption {
    pub code: u8,
    pub value: Vec<u8>,
}

impl ConfigOption {
    fn from_buffer(buf: &[u8]) -> Option<ConfigOption> {
        if buf.len() <= 0 {
            return None;
        }
        let code = buf[0];
        if !ConfigOption::is_valid_code(code) {
            return None;
        }
        let len: usize = match buf.get(1) {
            Some(l) => *l as usize,
            None => 0,
        };
        let mut value = Vec::new();
        let mut i: usize = 2;
        while i < len + 2 {
            let v = match buf.get(i) {
                Some(val) => *val,
                None => return None,
            };
            value.push(v);
            i += 1;
        }
        if len != value.len() {
            return None;
        }
        let opt = ConfigOption { code, value };
        Some(opt)
    }

    fn serialize_to(&self, output: &mut Vec<u8>) {
        if !self.has_valid_code() {
            return;
        }
        output.push(self.code);
        let len = self.value.len() as u8;
        if len > 0 {
            output.push(len);
        }
        output.extend(&self.value);
    }

    fn has_valid_code(&self) -> bool {
        // code is between 0 and 61, inclusive, or is 255
        self.code <= 61 || self.code == 255
    }

    fn is_valid_code(code: u8) -> bool {
        // code is between 0 and 61, inclusive, or is 255
        code <= 61 || code == 255
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

#[test]
fn test_option_from_valid_buffer_has_correct_values() {
    let buf = vec![1, 4, 255, 255, 255, 0];
    let result = ConfigOption::from_buffer(&buf);
    match result {
        Some(opt) => {
            assert_eq!(opt.code, 1);
            assert_eq!(opt.value, vec![255, 255, 255, 0]);
        }
        None => assert!(false), // test failure
    }
}

#[test]
fn test_option_from_valid_buffer_with_fixed_length_has_correct_values() {
    let buf = vec![255];
    let result = ConfigOption::from_buffer(&buf);
    match result {
        Some(opt) => {
            assert_eq!(opt.code, 255);
            assert!(opt.value.is_empty());
        }
        None => assert!(false), // test failure
    }
}

#[test]
fn test_option_from_buffer_with_invalid_code_returns_none() {
    let buf = vec![72, 2, 1, 2];
    let result = ConfigOption::from_buffer(&buf);
    match result {
        Some(opt) => assert!(false), // test failure
        None => assert!(true),       // test success
    }
}

#[test]
fn test_option_from_buffer_with_invalid_length_returns_none() {
    let buf = vec![1, 6, 255, 255, 255, 0];
    let result = ConfigOption::from_buffer(&buf);
    match result {
        Some(opt) => assert!(false), // test failure
        None => assert!(true),       // test success
    }
}
