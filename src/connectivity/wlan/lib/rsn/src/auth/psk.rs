// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::Error, anyhow::ensure, fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211, ieee80211::Ssid,
    std::str,
};

// PBKDF2-HMAC-SHA1 is considered insecure but required for PSK computation.
#[allow(deprecated)]
use mundane::insecure::insecure_pbkdf2_hmac_sha1;
use nonzero_ext::nonzero;

/// Keys derived from a passphrase provide comparably low levels of security.
/// Passphrases should have a minimum length of 20 characters since shorter passphrases
/// are unlikely to prevent attacks.
pub type Psk = Box<[u8]>;

pub fn compute(passphrase: &[u8], ssid: &Ssid) -> Result<Psk, anyhow::Error> {
    // IEEE Std 802.11-2016, 9.4.2.2
    // TODO(fxbug.dev/81772): Remove this check when Ssid is guaranteed to never
    // exceed 32 bytes.
    ensure!(
        ssid.len() <= (fidl_ieee80211::MAX_SSID_BYTE_LEN as usize),
        Error::InvalidSsidLen(ssid.len())
    );

    // IEEE Std 802.11-2016, J.4.1 provides a reference implementation that describes the
    // passphrase as:
    //
    //   ... sequence of between 8 and 63 ASCII-encoded characters ... Each character in the
    //   pass-phrase has an encoding in the range 32 to 126 (decimal).
    //
    // However, the standard does not seem to specify this encoding or otherwise state that it is a
    // requirement. In practice, devices accept UTF-8 encoded passphrases, which is far less
    // restrictive than a subset of ASCII. This code attempts to parse the passphrase as UTF-8 and
    // emits an error if this is not possible. Note that this also accepts the ASCII encoding
    // suggested by J.4.1.
    let _utf8 = str::from_utf8(passphrase)
        .map_err(|error| Error::InvalidPassphraseEncoding(error.valid_up_to()))?;

    // IEEE Std 802.11-2016, J.4.1 suggests a passphrase length of [8, 64). However, J.4.1 also
    // suggests ASCII encoding and this code expects UTF-8 encoded passphrases. This implicitly
    // supports the ASCII encodings described in J.4.1. However, the length of the byte sequence no
    // longer represents the number of encoded characters, so non-ASCII passphrases may appear to
    // have arbitrary character limits. Note that the character count can be obtained via
    // `_utf8.chars().count()`.
    ensure!(
        passphrase.len() >= 8 && passphrase.len() <= 63,
        Error::InvalidPassphraseLen(passphrase.len())
    );

    // Compute PSK: IEEE Std 802.11-2016, J.4.1
    let size = 256 / 8;
    let mut psk = vec![0_u8; size];
    let iters = nonzero!(4096u32);
    insecure_pbkdf2_hmac_sha1(&passphrase[..], &ssid[..], iters, &mut psk[..]);
    Ok(psk.into_boxed_slice())
}

#[cfg(test)]
mod tests {
    use super::*;
    use hex::FromHex;

    fn assert_psk(password: &str, ssid: &str, expected: &str) {
        let psk = compute(password.as_bytes(), &Ssid::from(ssid)).expect("computing PSK failed");
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
        let result = compute("short".as_bytes(), &Ssid::from("Some SSID"));
        assert!(result.is_err());
    }

    #[test]
    fn test_psk_too_long_password() {
        let result = compute(
            "1234567890123456789012345678901234567890123456789012345678901234".as_bytes(),
            &Ssid::from("Some SSID"),
        );
        assert!(result.is_err());
    }

    #[test]
    fn test_psk_ascii_bounds_password() {
        let result = compute("\x20ASCII Bound Test \x7E".as_bytes(), &Ssid::from("Some SSID"));
        assert!(result.is_ok());
    }

    #[test]
    fn test_psk_non_ascii_password() {
        assert!(compute("パスワード".as_bytes(), &Ssid::from("Some SSID")).is_ok());
    }

    #[test]
    fn test_psk_invalid_encoding_password() {
        assert!(compute(&[0xFFu8; 32], &Ssid::from("Some SSID")).is_err());
    }

    #[test]
    fn test_psk_too_long_ssid() {
        let result =
            compute("ThisIsAPassword".as_bytes(), &Ssid::from("123456789012345678901234567890123"));
        assert!(result.is_err());
    }
}
