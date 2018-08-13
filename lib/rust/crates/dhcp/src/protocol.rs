// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(warnings)]

use std::net::Ipv4Addr;

pub const SERVER_PORT: u16 = 67;
pub const CLIENT_PORT: u16 = 68;

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

const ONE_BYTE_LEN: usize = 8;
const TWO_BYTE_LEN: usize = 16;
const THREE_BYTE_LEN: usize = 24;

/// A DHCP protocol message as defined in RFC 2131.
///
/// All fields in `Message` follow the naming conventions outlined in the RFC.
/// Note that `Message` does not expose `htype`, `hlen`, or `hops` fields, as
/// these fields are effectively constants.
#[derive(Clone, Debug, PartialEq)]
pub struct Message {
    pub op: OpCode,
    pub xid: u32,
    pub secs: u16,
    pub bdcast_flag: bool,
    /// `ciaddr` should be stored in Big-Endian order, e.g `[192, 168, 1, 1]`.
    pub ciaddr: Ipv4Addr,
    /// `yiaddr` should be stored in Big-Endian order, e.g `[192, 168, 1, 1]`.
    pub yiaddr: Ipv4Addr,
    /// `siaddr` should be stored in Big-Endian order, e.g `[192, 168, 1, 1]`.
    pub siaddr: Ipv4Addr,
    /// `giaddr` should be stored in Big-Endian order, e.g `[192, 168, 1, 1]`.
    pub giaddr: Ipv4Addr,
    /// `chaddr` should be stored in Big-Endian order,
    /// e.g `[0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF]`.
    pub chaddr: MacAddr,
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
            ciaddr: Ipv4Addr::new(0, 0, 0, 0),
            yiaddr: Ipv4Addr::new(0, 0, 0, 0),
            siaddr: Ipv4Addr::new(0, 0, 0, 0),
            giaddr: Ipv4Addr::new(0, 0, 0, 0),
            chaddr: [0; MAC_ADDR_LEN],
            sname: String::new(),
            file: String::new(),
            options: Vec::new(),
        };
        msg
    }

    /// Instantiates a new `Message` from a byte buffer conforming to the DHCP
    /// protocol as defined RFC 2131. Returns `None` if the buffer is malformed.
    /// Any malformed configuration options will be skipped over, leaving only
    /// well formed `ConfigOption`s in the final `Message`.
    pub fn from_buffer(buf: &[u8]) -> Option<Self> {
        use byteorder::{BigEndian, ByteOrder};

        if buf.len() < OPTIONS_START_IDX {
            return None;
        }

        let mut msg = Message::new();
        let op = buf.get(OP_IDX)?;
        msg.op = OpCode::from(*op)?;
        msg.xid = BigEndian::read_u32(&buf[XID_IDX..SECS_IDX]);
        msg.secs = BigEndian::read_u16(&buf[SECS_IDX..FLAGS_IDX]);
        msg.bdcast_flag = buf[FLAGS_IDX] > 0;
        // The conditional earlier in this function ensures that buf is long enough
        // for the following 4 calls to always succeed.
        msg.ciaddr = ip_addr_from_buf_at(buf, CIADDR_IDX).expect("out of range indexing on buf");
        msg.yiaddr = ip_addr_from_buf_at(buf, YIADDR_IDX).expect("out of range indexing on buf");
        msg.siaddr = ip_addr_from_buf_at(buf, SIADDR_IDX).expect("out of range indexing on buf");
        msg.giaddr = ip_addr_from_buf_at(buf, GIADDR_IDX).expect("out of range indexing on buf");
        copy_buf_into_mac_addr(&buf[CHADDR_IDX..CHADDR_IDX + 6], &mut msg.chaddr);
        msg.sname = buf_to_msg_string(&buf[SNAME_IDX..FILE_IDX])?;
        msg.file = buf_to_msg_string(&buf[FILE_IDX..OPTIONS_START_IDX])?;
        buf_into_options(&buf[OPTIONS_START_IDX..], &mut msg.options);

        Some(msg)
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
        buffer.extend_from_slice(&self.ciaddr.octets());
        buffer.extend_from_slice(&self.yiaddr.octets());
        buffer.extend_from_slice(&self.siaddr.octets());
        buffer.extend_from_slice(&self.giaddr.octets());
        buffer.extend_from_slice(&self.chaddr);
        buffer.extend_from_slice(&[0u8; UNUSED_CHADDR_BYTES]);
        trunc_string_to_n_and_push(&self.sname, SNAME_LEN, &mut buffer);
        trunc_string_to_n_and_push(&self.file, FILE_LEN, &mut buffer);
        buffer.extend_from_slice(&self.serialize_options());
        buffer
    }

    fn serialize_options(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        for option in &self.options {
            option.serialize_to(&mut bytes);
        }
        bytes.push(OptionCode::End as u8);
        bytes
    }

    /// Returns a reference to the `Message`'s `ConfigOption` with `code`, or `None`
    /// if `Message` does not have the specified `ConfigOption`.
    pub fn get_config_option(&self, code: OptionCode) -> Option<&ConfigOption> {
        // There should generally be few (~0 - 10) options attached to a message
        // so the linear search should not be unreasonably costly.
        for opt in &self.options {
            if opt.code == code {
                return Some(opt);
            }
        }
        None
    }

    /// Returns the value's DHCP `MessageType` or `None` if unassigned.
    pub fn get_dhcp_type(&self) -> Option<MessageType> {
        let dhcp_type_option = self.get_config_option(OptionCode::DhcpMessageType)?;
        let maybe_dhcp_type_value = dhcp_type_option.value.get(0)?;
        let dhcp_type = MessageType::from(*maybe_dhcp_type_value);
        if dhcp_type == MessageType::Unknown {
            return None;
        }
        Some(dhcp_type)
    }
}

/// A DHCP protocol op-code as defined in RFC 2131.
///
/// Note that this type corresponds to the first field of a DHCP message,
/// opcode, and is distinct from the OptionCode type. In this case, "Op"
/// is an abbreviation for Operator, not Option.
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

/// A 48-bit network hardware MAC address.
pub type MacAddr = [u8; MAC_ADDR_LEN];

/// A vendor extension/configuration option for DHCP protocol messages.
///
/// `ConfigOption`s can be fixed or variable length per RFC 1533. When
/// `value` is left empty, the `ConfigOption` will be treated as a fixed
/// length field.
#[derive(Clone, Debug, PartialEq)]
pub struct ConfigOption {
    pub code: OptionCode,
    pub value: Vec<u8>,
}

impl ConfigOption {
    fn from_buffer(buf: &[u8]) -> Option<ConfigOption> {
        let raw_code = buf.get(0)?;
        let code = OptionCode::option_code_from_u8(*raw_code)?;
        let len: usize = match buf.get(1) {
            Some(l) => *l as usize,
            None => {
                return Some(ConfigOption {
                    code: code,
                    value: vec![],
                })
            }
        };
        let mut value = Vec::with_capacity(len);
        let val_offset = 2;
        let actual_len = buf.len() - val_offset;
        if len > 0 && len == actual_len {
            value.extend_from_slice(&buf[val_offset..]);
        } else if len > 0 {
            return None;
        }
        let opt = ConfigOption { code, value };
        Some(opt)
    }

    fn serialize_to(&self, output: &mut Vec<u8>) {
        output.push(self.code as u8);
        let len = self.value.len() as u8;
        if len > 0 {
            output.push(len);
        }
        output.extend(&self.value);
    }
}

/// A DHCP option code.
///
/// This enum corresponds to the codes for DHCP options as defined in
/// RFC 1533. Note that not all options defined in the RFC are represented
/// here; options which are not in this type are not currently supported. Supported
/// options appear in this type in the order in which they are defined in the RFC.
#[derive(Copy, Clone, Debug, PartialEq)]
pub enum OptionCode {
    Pad = 0,
    End = 255,
    SubnetMask = 1,
    Router = 3,
    NameServer = 5,
    RequestedIpAddr = 50,
    IpAddrLeaseTime = 51,
    DhcpMessageType = 53,
    ServerId = 54,
    RenewalTime = 58,
    RebindingTime = 59,
}

impl OptionCode {
    /// Returns an OptionCode value when given a valid and supported code value, else None.
    pub fn option_code_from_u8(n: u8) -> Option<OptionCode> {
        match n {
            0 => Some(OptionCode::Pad),
            255 => Some(OptionCode::End),
            1 => Some(OptionCode::SubnetMask),
            3 => Some(OptionCode::Router),
            5 => Some(OptionCode::NameServer),
            50 => Some(OptionCode::RequestedIpAddr),
            51 => Some(OptionCode::IpAddrLeaseTime),
            53 => Some(OptionCode::DhcpMessageType),
            54 => Some(OptionCode::ServerId),
            58 => Some(OptionCode::RenewalTime),
            59 => Some(OptionCode::RebindingTime),
            _ => None,
        }
    }
}

/// A DHCP Message Type.
///
/// This enum corresponds to the DHCP Message Type option values
/// defined in section 9.4 of RFC 1533.
#[derive(Copy, Clone, Debug, PartialEq)]
pub enum MessageType {
    Unknown = 0, // Not specified by RFC; used for From implementation.
    DHCPDISCOVER = 1,
    DHCPOFFER = 2,
    DHCPREQUEST = 3,
    DHCPDECLINE = 4,
    DHCPACK = 5,
    DHCPNAK = 6,
    DHCPRELEASE = 7,
    DHCPINFORM = 8,
}

impl From<u8> for MessageType {
    /// Creates a `MessageType` value from a `u8`.
    ///
    /// The mapping of `u8` value to `MessageType` reflects the mapping defined in
    /// RFC 2132 with on exception: `u8` values outside of the RFC defined mappings
    /// will return a `MessageType::Unknown`. The client should treat this value
    /// as an error case.
    fn from(num: u8) -> Self {
        match num {
            1 => MessageType::DHCPDISCOVER,
            2 => MessageType::DHCPOFFER,
            3 => MessageType::DHCPREQUEST,
            4 => MessageType::DHCPDECLINE,
            5 => MessageType::DHCPACK,
            6 => MessageType::DHCPNAK,
            7 => MessageType::DHCPRELEASE,
            8 => MessageType::DHCPINFORM,
            _ => MessageType::Unknown,
        }
    }
}

// Returns an Ipv4Addr when given a byte buffer in network order whose len >= start + 4.
pub fn ip_addr_from_buf_at(buf: &[u8], start: usize) -> Option<Ipv4Addr> {
    if buf.len() < start + 4 {
        return None;
    }
    Some(Ipv4Addr::new(
        buf[start],
        buf[start + 1],
        buf[start + 2],
        buf[start + 3],
    ))
}

fn copy_buf_into_mac_addr(buf: &[u8], addr: &mut MacAddr) {
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
    while opt_idx < buf.len()
        && OptionCode::option_code_from_u8(buf[opt_idx]) != Some(OptionCode::End)
    {
        let opt_code = buf[opt_idx];
        let opt_len: usize = match OptionCode::option_code_from_u8(opt_code) {
            None => continue,                                   // invalid option
            Some(OptionCode::Pad) | Some(OptionCode::End) => 1, // fixed length option
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

fn trunc_string_to_n_and_push(s: &str, n: usize, buffer: &mut Vec<u8>) {
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

#[cfg(test)]
mod tests {

    use super::*;
    use std::net::Ipv4Addr;

    #[test]
    fn test_serialize_returns_correct_bytes() {
        let mut msg = Message::new();
        msg.xid = 42;
        msg.secs = 1024;
        msg.yiaddr = Ipv4Addr::new(192, 168, 1, 1);
        msg.sname = String::from("relay.example.com");
        msg.file = String::from("boot.img");
        msg.options.push(ConfigOption {
            code: OptionCode::SubnetMask,
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
        buf.extend_from_slice(b"\x36\x04\xAA\xBB\xCC\xDD");
        buf.extend_from_slice(b"\xFF");

        let got = Message::from_buffer(&buf).unwrap();

        let opt_want1 = ConfigOption {
            code: OptionCode::SubnetMask,
            value: vec![255, 255, 255, 0],
        };
        let opt_want2 = ConfigOption {
            code: OptionCode::Pad,
            value: vec![],
        };
        let opt_want3 = ConfigOption {
            code: OptionCode::Pad,
            value: vec![],
        };
        let opt_want4 = ConfigOption {
            code: OptionCode::ServerId,
            value: vec![0xAA, 0xBB, 0xCC, 0xDD],
        };
        let want = Message {
            op: OpCode::BOOTREQUEST,
            xid: 42,
            secs: 1024,
            bdcast_flag: false,
            ciaddr: Ipv4Addr::new(0, 0, 0, 0),
            yiaddr: Ipv4Addr::new(192, 168, 1, 1),
            siaddr: Ipv4Addr::new(0, 0, 0, 0),
            giaddr: Ipv4Addr::new(0, 0, 0, 0),
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

    #[test]
    fn test_serialize_with_valid_option_returns_correct_bytes() {
        let opt = ConfigOption {
            code: OptionCode::SubnetMask,
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
    fn test_serialize_with_fixed_len_option_returns_correct_bytes() {
        let opt = ConfigOption {
            code: OptionCode::End,
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
                assert_eq!(opt.code as u8, 1);
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
                assert_eq!(opt.code as u8, 255);
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

    #[test]
    fn test_get_dhcp_type_with_dhcp_type_option_returns_value() {
        let mut msg = Message::new();
        msg.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![MessageType::DHCPDISCOVER as u8],
        });

        let got = msg.get_dhcp_type().unwrap();

        let want = MessageType::DHCPDISCOVER;
        assert_eq!(got, want);
    }

    #[test]
    fn test_get_dhcp_type_without_dhcp_type_option_returns_none() {
        let mut msg = Message::new();
        msg.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![],
        });

        let got = msg.get_dhcp_type();

        assert!(got.is_none());
    }

    #[test]
    fn test_get_dhcp_type_with_invalid_dhcp_type_value_returns_none() {
        let mut msg = Message::new();
        msg.options.push(ConfigOption {
            code: OptionCode::DhcpMessageType,
            value: vec![224, 223, 222],
        });

        let got = msg.get_dhcp_type();

        assert!(got.is_none());
    }
}
