// Copyright 2016 Andrew Baumhauer <andy@baumhauer.us>
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

//! Represent and parse IEEE EUI-48 Media Access Control addresses
//! The IEEE claims trademarks on the names EUI-48 and EUI-64, in which EUI is an
//! abbreviation for Extended Unique Identifier.

#![doc(
    html_logo_url = "https://www.rust-lang.org/logos/rust-logo-128x128-blk-v2.png",
    html_favicon_url = "https://www.rust-lang.org/favicon.ico",
    html_root_url = "https://doc.rust-lang.org/eui48/"
)]
#![cfg_attr(test, deny(warnings))]

extern crate rustc_serialize;
#[cfg(feature = "serde")]
extern crate serde;
#[cfg(feature = "serde_json")]
extern crate serde_json;

use std::default::Default;
use std::error::Error;
use std::fmt;
use std::str::FromStr;

use rustc_serialize::{Decodable, Decoder, Encodable, Encoder};
#[cfg(feature = "serde")]
use serde::{de, Deserialize, Deserializer, Serialize, Serializer};

/// A 48-bit (6 byte) buffer containing the EUI address
pub const EUI48LEN: usize = 6;
pub type Eui48 = [u8; EUI48LEN];

/// A 64-bit (8 byte) buffer containing the EUI address
pub const EUI64LEN: usize = 8;
pub type Eui64 = [u8; EUI64LEN];

/// A MAC address (EUI-48)
#[repr(C)]
#[derive(Copy, Clone, Hash, Eq, PartialEq, Ord, PartialOrd)]
pub struct MacAddress {
    /// The 48-bit number stored in 6 bytes
    eui: Eui48,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
/// Format to display MacAddress
pub enum MacAddressFormat {
    /// Use - notaion
    Canonical,
    /// Use : notation
    HexString,
    /// Use . notation
    DotNotation,
    /// Use 0x notation
    Hexadecimal,
}

#[derive(PartialEq, Eq, Copy, Clone, Debug, Ord, PartialOrd, Hash)]
/// Parsing errors
pub enum ParseError {
    /// Length is incorrect (should be 14 or 17)
    InvalidLength(usize),
    /// Character not [0-9a-fA-F]|'x'|'-'|':'|'.'
    InvalidCharacter(char, usize),
}

impl MacAddress {
    /// Create a new MacAddress from `[u8; 6]`.
    pub fn new(eui: Eui48) -> MacAddress {
        MacAddress { eui: eui }
    }

    /// Create a new MacAddress from a byte slice.
    ///
    /// Returns an error (without any description) if the slice doesn't have the proper length.
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, ()> {
        if bytes.len() != EUI48LEN {
            return Err(());
        }
        let mut input: [u8; EUI48LEN] = Default::default();
        for i in 0..EUI48LEN {
            input[i] = bytes[i];
        }
        Ok(Self::new(input))
    }

    /// Returns empty EUI-48 address
    pub fn nil() -> MacAddress {
        MacAddress { eui: [0; EUI48LEN] }
    }

    /// Returns 'ff:ff:ff:ff:ff:ff', a MAC broadcast address
    pub fn broadcast() -> MacAddress {
        MacAddress {
            eui: [0xFF; EUI48LEN],
        }
    }

    /// Returns true if the address is '00:00:00:00:00:00'
    pub fn is_nil(&self) -> bool {
        self.eui.iter().all(|&b| b == 0)
    }

    /// Returns true if the address is 'ff:ff:ff:ff:ff:ff'
    pub fn is_broadcast(&self) -> bool {
        self.eui.iter().all(|&b| b == 0xFF)
    }

    /// Returns true if bit 1 of Y is 0 in address 'xY:xx:xx:xx:xx:xx'
    pub fn is_unicast(&self) -> bool {
        self.eui[0] & 1 == 0
    }

    /// Returns true if bit 1 of Y is 1 in address 'xY:xx:xx:xx:xx:xx'
    pub fn is_multicast(&self) -> bool {
        self.eui[0] & 1 == 1
    }

    /// Returns true if bit 2 of Y is 0 in address 'xY:xx:xx:xx:xx:xx'
    pub fn is_universal(&self) -> bool {
        self.eui[0] & 1 << 1 == 0
    }

    /// Returns true if bit 2 of Y is 1 in address 'xY:xx:xx:xx:xx:xx'
    pub fn is_local(&self) -> bool {
        self.eui[0] & 1 << 1 == 2
    }

    /// Returns a String representation in the format '00-00-00-00-00-00'
    pub fn to_canonical(&self) -> String {
        format!(
            "{:02x}-{:02x}-{:02x}-{:02x}-{:02x}-{:02x}",
            self.eui[0], self.eui[1], self.eui[2], self.eui[3], self.eui[4], self.eui[5]
        )
    }

    /// Returns a String representation in the format '00:00:00:00:00:00'
    pub fn to_hex_string(&self) -> String {
        format!(
            "{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
            self.eui[0], self.eui[1], self.eui[2], self.eui[3], self.eui[4], self.eui[5]
        )
    }

    /// Returns a String representation in the format '0000.0000.0000'
    pub fn to_dot_string(&self) -> String {
        format!(
            "{:02x}{:02x}.{:02x}{:02x}.{:02x}{:02x}",
            self.eui[0], self.eui[1], self.eui[2], self.eui[3], self.eui[4], self.eui[5]
        )
    }

    /// Returns a String representation in the format '0x000000000000'
    pub fn to_hexadecimal(&self) -> String {
        format!(
            "0x{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
            self.eui[0], self.eui[1], self.eui[2], self.eui[3], self.eui[4], self.eui[5]
        )
    }

    /// Returns a String representation in the EUI-64 interface ID format '0000:00ff:fe00:0000'
    pub fn to_interfaceid(&self) -> String {
        format!(
            "{:02x}{:02x}:{:02x}ff:fe{:02x}:{:02x}{:02x}",
            (self.eui[0] ^ 0x02),
            self.eui[1],
            self.eui[2],
            self.eui[3],
            self.eui[4],
            self.eui[5]
        )
    }

    /// Returns a String representation in the IPv6 link local format 'ff80::0000:00ff:fe00:0000'
    pub fn to_link_local(&self) -> String {
        format!(
            "ff80::{:02x}{:02x}:{:02x}ff:fe{:02x}:{:02x}{:02x}",
            (self.eui[0] ^ 0x02),
            self.eui[1],
            self.eui[2],
            self.eui[3],
            self.eui[4],
            self.eui[5]
        )
    }

    /// Returns a String in the format selected by fmt
    pub fn to_string(&self, fmt: MacAddressFormat) -> String {
        match fmt {
            MacAddressFormat::Canonical => self.to_canonical(),
            MacAddressFormat::HexString => self.to_hex_string(),
            MacAddressFormat::DotNotation => self.to_dot_string(),
            MacAddressFormat::Hexadecimal => self.to_hexadecimal(),
        }
    }

    /// Parses a String representation from any format supported
    pub fn parse_str(s: &str) -> Result<MacAddress, ParseError> {
        let mut offset = 0; // Offset into the u8 Eui48 vector
        let mut hn: bool = false; // Have we seen the high nibble yet?
        let mut eui: Eui48 = [0; EUI48LEN];

        match s.len() {
            14 | 17 => {} // The formats are all 12 characters with 2 or 5 delims
            _ => return Err(ParseError::InvalidLength(s.len())),
        }

        for (idx, c) in s.chars().enumerate() {
            if offset >= EUI48LEN {
                // We shouln't still be parsing
                return Err(ParseError::InvalidLength(s.len()));
            }

            match c {
                '0'...'9' | 'a'...'f' | 'A'...'F' => {
                    match hn {
                        false => {
                            // We will match '0' and run this even if the format is 0x
                            hn = true; // Parsed the high nibble
                            eui[offset] = (c.to_digit(16).unwrap() as u8) << 4;
                        }
                        true => {
                            hn = false; // Parsed the low nibble
                            eui[offset] += c.to_digit(16).unwrap() as u8;
                            offset += 1;
                        }
                    }
                }
                '-' | ':' | '.' => {}
                'x' | 'X' => {
                    match idx {
                        1 => {
                            // If idx = 1, we are possibly parsing 0x1234567890ab format
                            // Reset the offset to zero to ignore the first two characters
                            offset = 0;
                            hn = false;
                        }
                        _ => return Err(ParseError::InvalidCharacter(c, idx)),
                    }
                }
                _ => return Err(ParseError::InvalidCharacter(c, idx)),
            }
        }

        if offset == EUI48LEN {
            // A correctly parsed value is exactly 6 u8s
            Ok(MacAddress::new(eui))
        } else {
            Err(ParseError::InvalidLength(s.len())) // Something slipped through
        }
    }

    /// Return the internal structure as a slice of bytes
    pub fn as_bytes<'a>(&'a self) -> &'a [u8] {
        &self.eui
    }

    /// Returns an array in Eui48. Works as an inverse function of new()
    pub fn to_array(&self) -> Eui48 {
        self.eui
    }

    /// Returns Display MacAddressFormat, determined at compile time.
    pub fn get_display_format() -> MacAddressFormat {
        if cfg!(feature = "disp_hexstring") {
            MacAddressFormat::HexString
        } else {
            MacAddressFormat::Canonical
        }
    }
}

impl FromStr for MacAddress {
    type Err = ParseError;
    /// Create a MacAddress from String
    fn from_str(us: &str) -> Result<MacAddress, ParseError> {
        MacAddress::parse_str(us)
    }
}

impl Default for MacAddress {
    /// Create a Default MacAddress (00-00-00-00-00-00)
    fn default() -> MacAddress {
        MacAddress::nil()
    }
}

impl fmt::Debug for MacAddress {
    /// Debug format for MacAddress is HexString notation
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "MacAddress(\"{}\")",
            self.to_string(MacAddressFormat::HexString)
        )
    }
}

impl fmt::Display for MacAddress {
    /// Display format is canonical format (00-00-00-00-00-00)
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let disp_fmt = MacAddress::get_display_format();
        write!(f, "{}", self.to_string(disp_fmt))
    }
}

impl fmt::Display for ParseError {
    /// Human readable error strings for ParseError enum
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            ParseError::InvalidLength(found) => write!(
                f,
                "Invalid length; expecting 14 or 17 chars, found {}",
                found
            ),
            ParseError::InvalidCharacter(found, pos) => {
                write!(f, "Invalid character; found `{}` at offset {}", found, pos)
            }
        }
    }
}

impl Error for ParseError {
    /// Human readable description for ParseError enum
    fn description(&self) -> &str {
        "MacAddress parse error"
    }
}

impl Encodable for MacAddress {
    /// Encode a MacAddress as canonical form
    fn encode<E: Encoder>(&self, e: &mut E) -> Result<(), E::Error> {
        e.emit_str(&self.to_canonical())
    }
}

impl Decodable for MacAddress {
    /// Decode a MacAddress from a string in canonical form
    fn decode<D: Decoder>(d: &mut D) -> Result<MacAddress, D::Error> {
        let string = d.read_str()?;
        string.parse().map_err(|err| d.error(&format!("{}", err)))
    }
}

#[cfg(feature = "serde")]
impl Serialize for MacAddress {
    /// Serialize a MacAddress as canonical form using the serde crate
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        serializer.serialize_str(&self.to_canonical())
    }
}

#[cfg(feature = "serde")]
impl<'de> Deserialize<'de> for MacAddress {
    /// Deserialize a MacAddress from canonical form using the serde crate
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        struct MacAddressVisitor;
        impl<'de> de::Visitor<'de> for MacAddressVisitor {
            type Value = MacAddress;

            fn visit_str<E: de::Error>(self, value: &str) -> Result<MacAddress, E> {
                value.parse().map_err(|err| E::custom(&format!("{}", err)))
            }

            fn visit_bytes<E: de::Error>(self, value: &[u8]) -> Result<MacAddress, E> {
                MacAddress::from_bytes(value).map_err(|_| E::invalid_length(value.len(), &self))
            }

            fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
                write!(
                    formatter,
                    "either a string representation of a MAC address or 6-element byte array"
                )
            }
        }
        deserializer.deserialize_str(MacAddressVisitor)
    }
}

// ************** TESTS BEGIN HERE ***************
#[cfg(test)]
mod tests {
    use super::{Eui48, MacAddress, MacAddressFormat, ParseError};

    #[test]
    fn test_new() {
        let eui: Eui48 = [0x12, 0x34, 0x56, 0xAB, 0xCD, 0xEF];
        let mac = MacAddress::new(eui);

        assert!(mac.eui[0..5] == eui[0..5]);
    }

    #[test]
    fn test_from_bytes() {
        assert_eq!(
            "12:34:56:ab:cd:ef",
            MacAddress::from_bytes(&[0x12, 0x34, 0x56, 0xAB, 0xCD, 0xEF])
                .unwrap()
                .to_hex_string()
        );
        assert!(MacAddress::from_bytes(&[0x12, 0x34, 0x56, 0xAB, 0xCD]).is_err());
    }

    #[test]
    fn test_nil() {
        let nil = MacAddress::nil();
        let not_nil = MacAddress::broadcast();
        assert_eq!("00:00:00:00:00:00", nil.to_hex_string());
        assert!(nil.is_nil());
        assert!(!not_nil.is_nil());
    }

    #[test]
    fn test_default() {
        let default = MacAddress::default();
        assert!(default.is_nil());
    }

    #[test]
    fn test_broadcast() {
        let broadcast = MacAddress::broadcast();
        let not_broadcast = MacAddress::nil();
        assert_eq!("ff:ff:ff:ff:ff:ff", broadcast.to_hex_string());
        assert!(broadcast.is_broadcast());
        assert!(!not_broadcast.is_broadcast());
    }

    #[test]
    fn test_is_nil() {
        let nil = MacAddress::nil();
        let not_nil = MacAddress::parse_str("01:00:5E:AB:CD:EF").unwrap();
        assert!(nil.is_nil());
        assert!(!not_nil.is_nil());
    }

    #[test]
    fn test_is_broadcast() {
        let broadcast = MacAddress::broadcast();
        let not_broadcast = MacAddress::parse_str("01:00:5E:AB:CD:EF").unwrap();
        assert!(broadcast.is_broadcast());
        assert!(!not_broadcast.is_broadcast());
    }

    #[test]
    fn test_is_unicast() {
        let mac_u = MacAddress::parse_str("FE:00:5E:AB:CD:EF").unwrap();
        let mac_m = MacAddress::parse_str("01:00:5E:AB:CD:EF").unwrap();
        assert!(mac_u.is_unicast());
        assert!(!mac_m.is_unicast());
        assert_eq!("fe:00:5e:ab:cd:ef", mac_u.to_hex_string()); // Catch modifying first octet
        let mac = MacAddress::parse_str("FF:00:5E:AB:CD:EF").unwrap();
        assert!(!mac.is_unicast());
        assert_eq!("ff:00:5e:ab:cd:ef", mac.to_hex_string()); // Catch modifying first octet
        assert!(MacAddress::nil().is_unicast());
        assert!(!MacAddress::broadcast().is_unicast());
    }

    #[test]
    fn test_is_multicast() {
        let mac_u = MacAddress::parse_str("FE:00:5E:AB:CD:EF").unwrap();
        let mac_m = MacAddress::parse_str("01:00:5E:AB:CD:EF").unwrap();
        assert!(!mac_u.is_multicast());
        assert!(mac_m.is_multicast());
        assert!(!MacAddress::nil().is_multicast());
        assert_eq!("01:00:5e:ab:cd:ef", mac_m.to_hex_string()); // Catch modifying first octet
        let mac = MacAddress::parse_str("F0:00:5E:AB:CD:EF").unwrap();
        assert!(!mac.is_multicast());
        assert_eq!("f0:00:5e:ab:cd:ef", mac.to_hex_string()); // Catch modifying first octet
        assert!(MacAddress::broadcast().is_multicast());
    }

    #[test]
    fn test_is_universal() {
        let universal = MacAddress::parse_str("11:24:56:AB:CD:EF").unwrap();
        let not_universal = MacAddress::parse_str("12:24:56:AB:CD:EF").unwrap();
        assert!(universal.is_universal());
        assert!(!not_universal.is_universal());
        assert_eq!("11:24:56:ab:cd:ef", universal.to_hex_string()); // Catch modifying first octet
    }

    #[test]
    fn test_is_local() {
        let local = MacAddress::parse_str("06:34:56:AB:CD:EF").unwrap();
        let not_local = MacAddress::parse_str("00:34:56:AB:CD:EF").unwrap();
        assert!(local.is_local());
        assert!(!not_local.is_local());
        assert_eq!("06:34:56:ab:cd:ef", local.to_hex_string()); // Catch modifying first octet
    }

    #[test]
    fn test_to_canonical() {
        let eui: Eui48 = [0x12, 0x34, 0x56, 0xAB, 0xCD, 0xEF];
        let mac = MacAddress::new(eui);
        assert_eq!("12-34-56-ab-cd-ef", mac.to_canonical());
    }

    #[test]
    fn test_to_hex_string() {
        let eui: Eui48 = [0x12, 0x34, 0x56, 0xAB, 0xCD, 0xEF];
        let mac = MacAddress::new(eui);
        assert_eq!("12:34:56:ab:cd:ef", mac.to_hex_string());
    }

    #[test]
    fn test_to_dot_string() {
        let eui: Eui48 = [0x12, 0x34, 0x56, 0xAB, 0xCD, 0xEF];
        let mac = MacAddress::new(eui);
        assert_eq!("1234.56ab.cdef", mac.to_dot_string());
    }

    #[test]
    fn test_to_hexadecimal() {
        let eui: Eui48 = [0x12, 0x34, 0x56, 0xAB, 0xCD, 0xEF];
        let mac = MacAddress::new(eui);
        assert_eq!("0x123456abcdef", mac.to_hexadecimal());
    }

    #[test]
    fn test_to_interfaceid() {
        let eui: Eui48 = [0x12, 0x34, 0x56, 0xAB, 0xCD, 0xEF];
        let mac = MacAddress::new(eui);
        assert_eq!("1034:56ff:feab:cdef", mac.to_interfaceid());
    }

    #[test]
    fn test_to_link_local() {
        let eui: Eui48 = [0x12, 0x34, 0x56, 0xAB, 0xCD, 0xEF];
        let mac = MacAddress::new(eui);
        assert_eq!("ff80::1034:56ff:feab:cdef", mac.to_link_local());
    }

    #[test]
    fn test_to_string() {
        let eui: Eui48 = [0x12, 0x34, 0x56, 0xAB, 0xCD, 0xEF];
        let mac = MacAddress::new(eui);
        assert_eq!(
            "0x123456abcdef",
            mac.to_string(MacAddressFormat::Hexadecimal)
        );
        assert_eq!(
            "1234.56ab.cdef",
            mac.to_string(MacAddressFormat::DotNotation)
        );
        assert_eq!(
            "12:34:56:ab:cd:ef",
            mac.to_string(MacAddressFormat::HexString)
        );
        assert_eq!(
            "12-34-56-ab-cd-ef",
            mac.to_string(MacAddressFormat::Canonical)
        );
    }

    #[test]
    fn test_parse_str() {
        use super::ParseError::*;

        assert_eq!(
            "0x123456abcdef",
            MacAddress::parse_str("0x123456ABCDEF")
                .unwrap()
                .to_hexadecimal()
        );
        assert_eq!(
            "1234.56ab.cdef",
            MacAddress::parse_str("1234.56AB.CDEF")
                .unwrap()
                .to_dot_string()
        );
        assert_eq!(
            "12:34:56:ab:cd:ef",
            MacAddress::parse_str("12:34:56:AB:CD:EF")
                .unwrap()
                .to_hex_string()
        );
        assert_eq!(
            "12-34-56-ab-cd-ef",
            MacAddress::parse_str("12-34-56-AB-CD-EF")
                .unwrap()
                .to_canonical()
        );
        // Test error parsing
        assert_eq!(MacAddress::parse_str(""), Err(InvalidLength(0)));
        assert_eq!(MacAddress::parse_str("0"), Err(InvalidLength(1)));
        assert_eq!(
            MacAddress::parse_str("123456ABCDEF"),
            Err(InvalidLength(12))
        );
        assert_eq!(
            MacAddress::parse_str("1234567890ABCD"),
            Err(InvalidLength(14))
        );
        assert_eq!(
            MacAddress::parse_str("1234567890ABCDEF"),
            Err(InvalidLength(16))
        );
        assert_eq!(
            MacAddress::parse_str("01234567890ABCDEF"),
            Err(InvalidLength(17))
        );
        assert_eq!(
            MacAddress::parse_str("0x1234567890A"),
            Err(InvalidLength(13))
        );
        assert_eq!(
            MacAddress::parse_str("0x1234567890ABCDE"),
            Err(InvalidLength(17))
        );
        assert_eq!(
            MacAddress::parse_str("0x00:00:00:00:"),
            Err(InvalidLength(14))
        );
        assert_eq!(
            MacAddress::parse_str("0x00:00:00:00:00:"),
            Err(InvalidLength(17))
        );
        assert_eq!(
            MacAddress::parse_str("::::::::::::::"),
            Err(InvalidLength(14))
        );
        assert_eq!(
            MacAddress::parse_str(":::::::::::::::::"),
            Err(InvalidLength(17))
        );
        assert_eq!(
            MacAddress::parse_str("0x0x0x0x0x0x0x"),
            Err(InvalidCharacter('x', 3))
        );
        assert_eq!(
            MacAddress::parse_str("!0x00000000000"),
            Err(InvalidCharacter('!', 0))
        );
        assert_eq!(
            MacAddress::parse_str("0x00000000000!"),
            Err(InvalidCharacter('!', 13))
        );
    }

    #[test]
    fn test_as_bytes() {
        let mac = MacAddress::broadcast();
        let bytes = mac.as_bytes();

        assert!(bytes.len() == 6);
        assert!(bytes.iter().all(|&b| b == 0xFF));
    }

    #[test]
    fn test_compare() {
        let m1 = MacAddress::nil();
        let m2 = MacAddress::broadcast();
        assert!(m1 == m1);
        assert!(m2 == m2);
        assert!(m1 != m2);
        assert!(m2 != m1);
    }

    #[test]
    fn test_clone() {
        let m1 = MacAddress::parse_str("12:34:56:AB:CD:EF").unwrap();
        let m2 = m1.clone();
        assert!(m1 == m1);
        assert!(m2 == m2);
        assert!(m1 == m2);
        assert!(m2 == m1);
    }

    #[test]
    fn test_serialize() {
        use rustc_serialize::json;

        let mac = MacAddress::parse_str("12:34:56:AB:CD:EF").unwrap();
        assert_eq!("\"12-34-56-ab-cd-ef\"", json::encode(&mac).unwrap());
    }

    #[test]
    fn test_deserialize() {
        use rustc_serialize::json;

        let d = "\"12-34-56-AB-CD-EF\"";
        let mac = MacAddress::parse_str("12:34:56:AB:CD:EF").unwrap();
        assert_eq!(mac, json::decode(&d).unwrap());
    }

    #[test]
    fn test_serialize_roundtrip() {
        use rustc_serialize::json;

        let m1 = MacAddress::parse_str("12:34:56:AB:CD:EF").unwrap();
        let s = json::encode(&m1).unwrap();
        let m2 = json::decode(&s).unwrap();
        assert_eq!(m1, m2);
    }

    #[test]
    fn test_fmt_debug() {
        let mac = MacAddress::parse_str("12:34:56:AB:CD:EF").unwrap();
        assert_eq!(
            "MacAddress(\"12:34:56:ab:cd:ef\")".to_owned(),
            format!("{:?}", mac)
        );
    }

    #[test]
    fn test_fmt() {
        let mac = MacAddress::parse_str("12:34:56:AB:CD:EF").unwrap();
        match MacAddress::get_display_format() {
            MacAddressFormat::HexString => {
                assert_eq!("12:34:56:ab:cd:ef".to_owned(), format!("{}", mac))
            }
            _ => assert_eq!("12-34-56-ab-cd-ef".to_owned(), format!("{}", mac)),
        };
    }

    #[test]
    fn test_fmt_parse_errors() {
        assert_eq!(
            "Err(InvalidLength(12))".to_owned(),
            format!("{:?}", MacAddress::parse_str("123456ABCDEF"))
        );
        assert_eq!(
            "Err(InvalidCharacter(\'#\', 2))".to_owned(),
            format!("{:?}", MacAddress::parse_str("12#34#56#AB#CD#EF"))
        );
    }

    #[test]
    #[cfg(feature = "serde_json")]
    fn test_serde_json_serialize() {
        use serde_json;
        let serialized =
            serde_json::to_string(&MacAddress::parse_str("12:34:56:AB:CD:EF").unwrap()).unwrap();
        assert_eq!("\"12-34-56-ab-cd-ef\"", serialized);
    }

    #[test]
    #[cfg(feature = "serde_json")]
    fn test_serde_json_deserialize() {
        use serde_json;
        let mac = MacAddress::parse_str("12:34:56:AB:CD:EF").unwrap();
        let deserialized: MacAddress = serde_json::from_str("\"12-34-56-AB-CD-EF\"").unwrap();
        assert_eq!(deserialized, mac);
    }

    #[test]
    fn test_macaddressformat_derive() {
        assert_eq!(MacAddressFormat::HexString, MacAddressFormat::HexString);
        assert_ne!(MacAddressFormat::HexString, MacAddressFormat::Canonical);
    }

    #[test]
    fn test_parseerror_fmt() {
        use std::error::Error;
        assert_eq!(
            "Invalid length; expecting 14 or 17 chars, found 2".to_owned(),
            format!("{}", ParseError::InvalidLength(2))
        );
        assert_eq!(
            "Invalid character; found `@` at offset 2".to_owned(),
            format!("{}", ParseError::InvalidCharacter('@', 2))
        );
        assert_eq!(
            "MacAddress parse error".to_owned(),
            format!("{}", ParseError::InvalidLength(2).description())
        );
    }

    #[test]
    fn test_to_array() {
        let eui: Eui48 = [0x12, 0x34, 0x56, 0xAB, 0xCD, 0xEF];
        let mac = MacAddress::new(eui);
        assert_eq!(eui, MacAddress::new(eui).to_array());
        assert_eq!(mac, MacAddress::new(mac.to_array()));
    }
}
