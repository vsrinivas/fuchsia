// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crypto::hmac::Hmac;
use crypto::pbkdf2;
use crypto::sha1::Sha1;
use {Error, Result};

/// Keys derived from a passphrase provide comparably low levels of security.
/// Passphrases should have a minimum length of 20 characters since shorter passphrases
/// are unlikely to prevent attacks.
#[derive(Debug, PartialEq)]
pub struct Psk {
    config: Config,
}

#[derive(Debug, PartialEq)]
pub struct Config {
    ssid: Vec<u8>,
    passphrase: Vec<u8>,
}

impl Config {
    pub fn new(passphrase: &[u8], ssid: &[u8]) -> Result<Config> {
        // IEEE Std 802.11-2016, 9.4.2.2
        if ssid.len() > 32 {
            return Err(Error::InvalidSsidLen(ssid.len()));
        }

        // IEEE Std 802.11-2016, J.4.1
        if passphrase.len() < 8 || passphrase.len() > 63 {
            Err(Error::InvalidPassphraseLen(passphrase.len()))
        } else {
            for c in passphrase {
                if *c < 32 || *c > 126 {
                    return Err(Error::InvalidPassphraseChar(*c));
                }
            }
            Ok(Config {
                ssid: ssid.to_vec(),
                passphrase: passphrase.to_vec(),
            })
        }
    }
}

impl Psk {
    pub(crate) fn new(config: Config) -> Result<Psk> {
        Ok(Psk { config })
    }

    pub fn compute(&self) -> Vec<u8> {
        // IEEE Std 802.11-2016, J.4.1
        let size: usize = 256 / 8;
        let mut out_key: Vec<u8> = vec![0; size];
        let mut hmac = Hmac::new(Sha1::new(), &self.config.passphrase[..]);
        pbkdf2::pbkdf2(&mut hmac, &self.config.ssid[..], 4096, &mut out_key[..]);
        out_key
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use hex::FromHex;

    fn assert_psk(password: &str, ssid: &str, expected: &str) {
        let cfg_result = Config::new(password.as_bytes(), ssid.as_bytes());
        assert_eq!(cfg_result.is_ok(), true);

        let psk_result = Psk::new(cfg_result.unwrap());
        assert_eq!(psk_result.is_ok(), true);
        let psk = psk_result.unwrap();
        let actual = psk.compute();
        let expected = Vec::from_hex(expected).unwrap();
        assert_eq!(actual, expected);
    }

    // IEEE Std 802.11-2016, J.4.2, Test case 1
    #[test]
    fn test_psk_test_case_1() {
        assert_psk(
            "password",
            "IEEE",
            "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e",
        );
    }

    // IEEE Std 802.11-2016, J.4.2, Test case 2
    #[test]
    fn test_psk_test_case_2() {
        assert_psk(
            "ThisIsAPassword",
            "ThisIsASSID",
            "0dc0d6eb90555ed6419756b9a15ec3e3209b63df707dd508d14581f8982721af",
        );
    }

    // IEEE Std 802.11-2016, J.4.2, Test case 3
    #[test]
    fn test_psk_test_case_3() {
        assert_psk(
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            "ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ",
            "becb93866bb8c3832cb777c2f559807c8c59afcb6eae734885001300a981cc62",
        );
    }

    #[test]
    fn test_psk_too_short_password() {
        let result = Config::new("short".as_bytes(), "Some SSID".as_bytes());
        assert_eq!(result.is_err(), true);
    }

    #[test]
    fn test_psk_too_long_password() {
        let result = Config::new(
            "1234567890123456789012345678901234567890123456789012345678901234".as_bytes(),
            "Some SSID".as_bytes(),
        );
        assert_eq!(result.is_err(), true);
    }

    #[test]
    fn test_psk_invalid_char_password() {
        let result = Config::new("Invalid Char \x1F".as_bytes(), "Some SSID".as_bytes());
        assert_eq!(result.is_err(), true);
    }

    #[test]
    fn test_psk_ascii_bounds_password() {
        let result = Config::new(
            "\x20ASCII Bound Test \x7E".as_bytes(),
            "Some SSID".as_bytes(),
        );
        assert_eq!(result.is_ok(), true);
    }

    #[test]
    fn test_psk_invalid_unicode_char_password() {
        let result = Config::new(
            "refuse unicode \u{00DF} chars".as_bytes(),
            "Some SSID".as_bytes(),
        );
        assert_eq!(result.is_err(), true);
    }

    #[test]
    fn test_psk_unicode_valid_length_password() {
        // Five characters but 10 bytes.
        let result = Config::new(
            "\u{00DF}\u{00DF}\u{00DF}\u{00DF}\u{00DF}".as_bytes(),
            "Some SSID".as_bytes(),
        );
        assert_eq!(result.is_err(), true);
    }

    #[test]
    fn test_psk_too_long_ssid() {
        let result = Config::new(
            "ThisIsAPassword".as_bytes(),
            "123456789012345678901234567890123".as_bytes(),
        );
        assert_eq!(result.is_err(), true);
    }
}
