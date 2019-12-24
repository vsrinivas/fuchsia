// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::Error, anyhow::ensure, std::num::NonZeroU32};

// PBKDF2-HMAC-SHA1 is considered insecure but required for PSK computation.
#[allow(deprecated)]
use mundane::insecure::insecure_pbkdf2_hmac_sha1;

/// Keys derived from a passphrase provide comparably low levels of security.
/// Passphrases should have a minimum length of 20 characters since shorter passphrases
/// are unlikely to prevent attacks.
pub type Psk = Box<[u8]>;

pub fn compute(passphrase: &[u8], ssid: &[u8]) -> Result<Psk, anyhow::Error> {
    // IEEE Std 802.11-2016, 9.4.2.2
    ensure!(ssid.len() <= 32, Error::InvalidSsidLen(ssid.len()));

    // IEEE Std 802.11-2016, J.4.1
    ensure!(
        passphrase.len() >= 8 && passphrase.len() <= 63,
        Error::InvalidPassphraseLen(passphrase.len())
    );

    for c in passphrase {
        ensure!(*c >= 32 && *c <= 126, Error::InvalidPassphraseChar(*c));
    }

    // Compute PSK: IEEE Std 802.11-2016, J.4.1
    let size = 256 / 8;
    let mut psk = vec![0_u8; size];
    // Safe: Using constant non-zero value.
    let iters = unsafe { NonZeroU32::new_unchecked(4096) };
    insecure_pbkdf2_hmac_sha1(&passphrase[..], &ssid[..], iters, &mut psk[..]);
    Ok(psk.into_boxed_slice())
}

#[cfg(test)]
mod tests {
    use super::*;
    use hex::FromHex;

    fn assert_psk(password: &str, ssid: &str, expected: &str) {
        let psk = compute(password.as_bytes(), ssid.as_bytes()).expect("computing PSK failed");
        let expected = Vec::from_hex(expected).unwrap();
        assert_eq!(&psk[..], &expected[..]);
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
        let result = compute("short".as_bytes(), "Some SSID".as_bytes());
        assert!(result.is_err());
    }

    #[test]
    fn test_psk_too_long_password() {
        let result = compute(
            "1234567890123456789012345678901234567890123456789012345678901234".as_bytes(),
            "Some SSID".as_bytes(),
        );
        assert!(result.is_err());
    }

    #[test]
    fn test_psk_invalid_char_password() {
        let result = compute("Invalid Char \x1F".as_bytes(), "Some SSID".as_bytes());
        assert!(result.is_err());
    }

    #[test]
    fn test_psk_ascii_bounds_password() {
        let result = compute("\x20ASCII Bound Test \x7E".as_bytes(), "Some SSID".as_bytes());
        assert!(result.is_ok());
    }

    #[test]
    fn test_psk_invalid_unicode_char_password() {
        let result = compute("refuse unicode \u{00DF} chars".as_bytes(), "Some SSID".as_bytes());
        assert!(result.is_err());
    }

    #[test]
    fn test_psk_unicode_valid_length_password() {
        // Five characters but 10 bytes.
        let result =
            compute("\u{00DF}\u{00DF}\u{00DF}\u{00DF}\u{00DF}".as_bytes(), "Some SSID".as_bytes());
        assert!(result.is_err());
    }

    #[test]
    fn test_psk_too_long_ssid() {
        let result =
            compute("ThisIsAPassword".as_bytes(), "123456789012345678901234567890123".as_bytes());
        assert!(result.is_err());
    }
}
