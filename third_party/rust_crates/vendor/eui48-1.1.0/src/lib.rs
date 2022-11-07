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

extern crate regex;
#[cfg(feature = "rustc-serialize")]
extern crate rustc_serialize;
#[cfg(feature = "serde")]
extern crate serde;
#[cfg(feature = "serde_json")]
extern crate serde_json;

use std::default::Default;
use std::error::Error;
use std::fmt;
use std::str::FromStr;

use regex::Regex;
#[cfg(feature = "rustc-serialize")]
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
    /// Length is incorrect (should be 11 to 17)
    InvalidLength(usize),
    /// The input string is invalid, usize bytes were found, and we put up to 6 bytes into Eui48
    InvalidByteCount(usize, Eui48),
}

impl MacAddress {
    /// Create a new MacAddress from `[u8; 6]`.
    pub const fn new(eui: Eui48) -> MacAddress {
        MacAddress { eui }
    }

    /// Create a new MacAddress from a byte slice.
    ///
    /// Returns an error (without any description) if the slice doesn't have the proper length.
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, ParseError> {
        if bytes.len() != EUI48LEN {
            return Err(ParseError::InvalidLength(bytes.len()));
        }
        let mut input: [u8; EUI48LEN] = Default::default();
        input[..EUI48LEN].clone_from_slice(&bytes[..EUI48LEN]);
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

    /// Returns a String representation in the IPv6 link local format 'fe80::0000:00ff:fe00:0000'
    pub fn to_link_local(&self) -> String {
        format!(
            "fe80::{:02x}{:02x}:{:02x}ff:fe{:02x}:{:02x}{:02x}",
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
        let re = Regex::new("(0x)?([0-9a-fA-F]{1,2})[:.-]?").unwrap();
        let mut eui: Eui48 = [0; EUI48LEN];

        match s.len() {
            11..=17 => {}
            _ => {
                return Err(ParseError::InvalidLength(s.len()));
            }
        }

        let mut i = 0;
        for caps in re.captures_iter(s) {
            // Fill the array and keep counting for InvalidByteCount
            if i < EUI48LEN {
                let matched_byte = caps.get(2).unwrap().as_str();
                eui[i] = u8::from_str_radix(matched_byte, 16).unwrap();
            }
            i += 1;
        }

        if i != EUI48LEN {
            return Err(ParseError::InvalidByteCount(i, eui));
        }

        Ok(MacAddress::new(eui))
    }

    /// Return the internal structure as a slice of bytes
    pub fn as_bytes(&self) -> &[u8] {
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
    /// Display format is canonical format (00-00-00-00-00-00) by default
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
                "Invalid length; expecting 11 to 17 chars, found {}",
                found
            ),
            ParseError::InvalidByteCount(found, eui) => write!(
                f,
                "Invalid byte count; Matched `{}` bytes ({:?})",
                found,
                &eui[..found]
            ),
        }
    }
}

impl Error for ParseError {
    /// Human readable description for ParseError enum
    fn description(&self) -> &str {
        "MacAddress parse error"
    }
}

#[cfg(feature = "rustc-serialize")]
impl Encodable for MacAddress {
    /// Encode a MacAddress using the default format
    fn encode<E: Encoder>(&self, e: &mut E) -> Result<(), E::Error> {
        let disp_fmt = MacAddress::get_display_format();
        e.emit_str(&self.to_string(disp_fmt))
    }
}

#[cfg(feature = "rustc-serialize")]
impl Decodable for MacAddress {
    /// Decode a MacAddress from a string in canonical form
    fn decode<D: Decoder>(d: &mut D) -> Result<MacAddress, D::Error> {
        let string = d.read_str()?;
        string.parse().map_err(|err| d.error(&format!("{}", err)))
    }
}

#[cfg(all(feature = "serde", not(feature = "serde_bytes")))]
impl Serialize for MacAddress {
    /// Serialize a MacAddress in the default format using the serde crate
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        let disp_fmt = MacAddress::get_display_format();
        serializer.serialize_str(&self.to_string(disp_fmt))
    }
}

#[cfg(feature = "serde_bytes")]
impl Serialize for MacAddress {
    /// Serialize a MacAddress as raw bytes using the serde crate
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        serializer.serialize_bytes(self.as_bytes())
    }
}

#[cfg(all(feature = "serde", not(feature = "serde_bytes")))]
impl<'de> Deserialize<'de> for MacAddress {
    /// Deserialize a MacAddress from canonical form using the serde crate
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        struct MacAddressVisitor;
        impl<'de> de::Visitor<'de> for MacAddressVisitor {
            type Value = MacAddress;

            fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
                write!(formatter, "a string representation of a MAC address")
            }

            fn visit_str<E: de::Error>(self, value: &str) -> Result<Self::Value, E> {
                value.parse().map_err(|err| E::custom(&format!("{}", err)))
            }
        }
        deserializer.deserialize_str(MacAddressVisitor)
    }
}

#[cfg(feature = "serde_bytes")]
impl<'de> Deserialize<'de> for MacAddress {
    /// Deserialize a MacAddress from raw bytes using the serde crate
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        struct MacAddressVisitor;
        impl<'de> de::Visitor<'de> for MacAddressVisitor {
            type Value = MacAddress;

            fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
                write!(formatter, "6-element byte array")
            }

            fn visit_bytes<E: de::Error>(self, value: &[u8]) -> Result<Self::Value, E> {
                MacAddress::from_bytes(value).map_err(|_| E::invalid_length(value.len(), &self))
            }
        }
        deserializer.deserialize_bytes(MacAddressVisitor)
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
        assert_eq!("fe80::1034:56ff:feab:cdef", mac.to_link_local());
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
        assert_eq!(
            "12-34-56-78-90-0a",
            MacAddress::parse_str("0x1234567890A")
                .unwrap()
                .to_canonical()
        );
        assert_eq!(
            "12-34-56-ab-cd-ef",
            MacAddress::parse_str("123456ABCDEF")
                .unwrap()
                .to_canonical()
        );
        assert_eq!(
            "00-00-00-00-00-00",
            MacAddress::parse_str("!0x00000000000")
                .unwrap()
                .to_canonical()
        );
        assert_eq!(
            "00-00-00-00-00-00",
            MacAddress::parse_str("0x00000000000!")
                .unwrap()
                .to_canonical()
        );
        // Test error parsing
        assert_eq!(MacAddress::parse_str(""), Err(InvalidLength(0)));
        assert_eq!(MacAddress::parse_str("0"), Err(InvalidLength(1)));
        assert_eq!(
            MacAddress::parse_str("1234567890ABCD"),
            Err(InvalidByteCount(7, [0x12, 0x34, 0x56, 0x78, 0x90, 0xAB]))
        );
        assert_eq!(
            MacAddress::parse_str("1234567890ABCDEF"),
            Err(InvalidByteCount(8, [0x12, 0x34, 0x56, 0x78, 0x90, 0xAB]))
        );
        assert_eq!(
            MacAddress::parse_str("01234567890ABCDEF"),
            Err(InvalidByteCount(9, [0x01, 0x23, 0x45, 0x67, 0x89, 0x0A]))
        );
        assert_eq!(
            MacAddress::parse_str("0x1234567890ABCDE"),
            Err(InvalidByteCount(8, [0x12, 0x34, 0x56, 0x78, 0x90, 0xAB]))
        );
        assert_eq!(
            MacAddress::parse_str("0x00:01:02:03:"),
            Err(InvalidByteCount(4, [0, 1, 2, 3, 0, 0]))
        );
        assert_eq!(
            MacAddress::parse_str("0x00:01:02:03:04:"),
            Err(InvalidByteCount(5, [0, 1, 2, 3, 4, 0]))
        );
        assert_eq!(
            MacAddress::parse_str("::::::::::::::"),
            Err(InvalidByteCount(0, [0, 0, 0, 0, 0, 0]))
        );
        assert_eq!(
            MacAddress::parse_str(":::::::::::::::::"),
            Err(InvalidByteCount(0, [0, 0, 0, 0, 0, 0]))
        );
        assert_eq!(
            MacAddress::parse_str("0x0x0x0x0x0x0x"),
            Err(InvalidByteCount(4, [0, 0, 0, 0, 0, 0]))
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
        assert_eq!(m1, m1);
        assert_eq!(m2, m2);
        assert_ne!(m1, m2);
        assert_ne!(m2, m1);
    }

    #[test]
    fn test_clone() {
        let m1 = MacAddress::parse_str("12:34:56:AB:CD:EF").unwrap();
        let m2 = m1;
        assert_eq!(m1, m1);
        assert_eq!(m2, m2);
        assert_eq!(m1, m2);
        assert_eq!(m2, m1);
    }

    #[test]
    fn test_serialize() {
        use rustc_serialize::json;

        let mac = MacAddress::parse_str("12:34:56:AB:CD:EF").unwrap();
        // Format returned is base on compile time of feature(disp_hexstring)
        if cfg!(feature = "disp_hexstring") {
            assert_eq!("\"12:34:56:ab:cd:ef\"", json::encode(&mac).unwrap());
        } else {
            assert_eq!("\"12-34-56-ab-cd-ef\"", json::encode(&mac).unwrap());
        }
    }

    #[test]
    fn test_deserialize() {
        use rustc_serialize::json;

        let mac = MacAddress::parse_str("12:34:56:AB:CD:EF").unwrap();

        if cfg!(feature = "disp_hexstring") {
            let d = "\"12:34:56:AB:CD:EF\"";
            assert_eq!(mac, json::decode(&d).unwrap());
        } else {
            let d = "\"12-34-56-AB-CD-EF\"";
            assert_eq!(mac, json::decode(&d).unwrap());
        }
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
        let mac = MacAddress::parse_str("0x123456ABCDEF").unwrap();
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
            "Err(InvalidByteCount(7, [18, 52, 86, 171, 205, 239]))".to_owned(),
            format!("{:?}", MacAddress::parse_str("123456ABCDEF1"))
        );
        assert_eq!(
            "Err(InvalidLength(19))",
            format!("{:?}", MacAddress::parse_str("12##45#67#89#AB#C#D"))
        );
    }

    #[test]
    #[cfg(feature = "serde_json")]
    fn test_serde_json_serialize() {
        use serde_json;
        let serialized =
            serde_json::to_string(&MacAddress::parse_str("12:34:56:AB:CD:EF").unwrap()).unwrap();
        if cfg!(feature = "disp_hexstring") {
            assert_eq!("\"12:34:56:ab:cd:ef\"", serialized);
        } else {
            assert_eq!("\"12-34-56-ab-cd-ef\"", serialized);
        }
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
    #[should_panic(expected = "Invalid length; expecting 11 to 17 chars, found 2")]
    #[cfg(feature = "serde_json")]
    fn test_serde_json_deserialize_panic() {
        let _should_panic: MacAddress = serde_json::from_str("\"12\"").unwrap();
    }

    #[test]
    #[cfg(feature = "serde_bytes")]
    fn test_serde_bytes_serialization_roundtrip() {
        use bincode;
        let mac = MacAddress::parse_str("12:34:56:AB:CD:EF").unwrap();
        let mut buffer = Vec::new();
        bincode::serialize_into(&mut buffer, &mac).unwrap();
        let deserialized: MacAddress = bincode::deserialize_from(&*buffer).unwrap();
        assert_eq!(deserialized, mac);
    }

    #[test]
    fn test_macaddressformat_derive() {
        assert_eq!(MacAddressFormat::HexString, MacAddressFormat::HexString);
        assert_ne!(MacAddressFormat::HexString, MacAddressFormat::Canonical);
    }

    #[test]
    fn test_parseerror_fmt() {
        assert_eq!(
            "Invalid length; expecting 11 to 17 chars, found 2".to_owned(),
            format!("{}", ParseError::InvalidLength(2))
        );
        assert_eq!(
            "Invalid length; expecting 11 to 17 chars, found 2".to_owned(),
            ParseError::InvalidLength(2).to_string()
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
