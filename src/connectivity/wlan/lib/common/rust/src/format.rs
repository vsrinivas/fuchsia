// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use hex;

pub trait MacFmt {
    fn to_mac_str(&self) -> String;
    fn to_oui_uppercase(&self, delim: &str) -> String;
}

impl MacFmt for [u8; 6] {
    fn to_mac_str(&self) -> String {
        format!(
            "{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
            self[0], self[1], self[2], self[3], self[4], self[5]
        )
    }

    fn to_oui_uppercase(&self, sep: &str) -> String {
        format!("{:02X}{}{:02X}{}{:02X}", self[0], sep, self[1], sep, self[2])
    }
}

pub trait SsidFmt {
    /// Return an SSID formatted as <ssid-BYTES> where BYTES are the bytes of the
    /// SSID encoded as uppercase hexadecimal characters.
    fn to_ssid_str(&self) -> String;

    /// Return an SSID formatted as a UTF-8 string, or <ssid-BYTES> if a UTF-8 error
    /// is encountered.
    fn to_ssid_str_not_redactable(&self) -> String;
}

impl SsidFmt for Vec<u8> {
    fn to_ssid_str(&self) -> String {
        format!("<ssid-{}>", hex::encode(self))
    }

    fn to_ssid_str_not_redactable(&self) -> String {
        String::from_utf8(self.to_vec()).unwrap_or_else(|_| self.to_ssid_str())
    }
}

impl SsidFmt for &[u8] {
    fn to_ssid_str(&self) -> String {
        format!("<ssid-{}>", hex::encode(self))
    }

    fn to_ssid_str_not_redactable(&self) -> String {
        std::str::from_utf8(self).map(|s| s.to_string()).unwrap_or_else(|_| self.to_ssid_str())
    }
}

impl<T> SsidFmt for &T
where
    T: SsidFmt,
{
    fn to_ssid_str(&self) -> String {
        (*self).to_ssid_str()
    }

    fn to_ssid_str_not_redactable(&self) -> String {
        (*self).to_ssid_str_not_redactable()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn format_mac_str() {
        let mac: [u8; 6] = [0x00, 0x12, 0x48, 0x9a, 0xbc, 0xdf];
        assert_eq!(mac.to_mac_str(), "00:12:48:9a:bc:df");
    }

    #[test]
    fn format_ssid_str() {
        let empty_ssid: Vec<u8> = vec![];
        assert_eq!(empty_ssid.to_ssid_str(), "<ssid->");

        let other_ssid: Vec<u8> = vec![0x01, 0x02, 0x03, 0x04, 0x05];
        assert_eq!(other_ssid.to_ssid_str(), "<ssid-0102030405>");
    }

    #[test]
    fn format_oui_uppercase() {
        let mac: [u8; 6] = [0x0a, 0xb1, 0xcd, 0x9a, 0xbc, 0xdf];
        assert_eq!(mac.to_oui_uppercase(""), "0AB1CD");
        assert_eq!(mac.to_oui_uppercase(":"), "0A:B1:CD");
        assert_eq!(mac.to_oui_uppercase("-"), "0A-B1-CD");
    }
}
