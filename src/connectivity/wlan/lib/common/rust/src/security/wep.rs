// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! IEEE Std 802.11-2016 WEP descriptors and credentials.
//!
//! **WEP is insecure and support will be removed. This module is provided for legacy support only
//! and its use should be avoided.**
//!
//! WEP has no dedicated descriptor data. Only WEP-40 and WEP-104 are supported and key size is not
//! negotiated with remote stations.

// TODO(fxbug.dev/96000): Name items in this module in a way that makes it clear that they
//                        implement an insecure security protocol.

use fidl_fuchsia_wlan_common_security as fidl_security;
use hex;
use std::convert::{TryFrom, TryInto};
use thiserror::Error;

use crate::security::SecurityError;

pub const WEP40_KEY_BYTES: usize = 5;
pub const WEP104_KEY_BYTES: usize = 13;

#[derive(Clone, Copy, Debug, Error, Eq, PartialEq)]
#[non_exhaustive]
pub enum WepError {
    #[error("invalid WEP key size: {0} bytes")]
    Size(usize),
    #[error("invalid WEP key encoding")]
    Encoding,
}

/// WEP key.
///
/// IEEE Std 802.11-2016 describes two WEP key sizes: WEP-40 and WEP-104. `Key` provides variants
/// for these standard key sizes represented as unencoded bytes.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum WepKey {
    /// WEP-40 key. This is a partial key and is joined with an IV to form a 64-bit key.
    Wep40([u8; WEP40_KEY_BYTES]),
    /// WEP-104 key. This is a partial key and is joined with an IV to form a 128-bit key.
    Wep104([u8; WEP104_KEY_BYTES]),
}

impl WepKey {
    /// Parses a WEP key from a byte sequence.
    ///
    /// This function parses both unencoded keys and ASCII hexadecimal encoded keys. IEEE Std
    /// 802.11-2016 does not specify an encoding for non-hexadecimal keys, so the raw bytes are
    /// accepted as is, though these keys are typically ASCII or UTF-8 encoded text in practice.
    /// ASCII hexadecimal encoded keys are decoded into raw bytes.
    ///
    /// Note that `Key` does not provide a mechanism to restore the original byte sequence parsed
    /// by this function, so the exact encoding of ASCII hexadecimal encoded keys may be lost.
    ///
    /// # Errors
    ///
    /// Returns an error if the size or encoding of the byte sequence is incompatible.
    pub fn parse(bytes: impl AsRef<[u8]>) -> Result<Self, WepError> {
        const WEP40_HEX_ENCODING_BYTES: usize = WEP40_KEY_BYTES * 2;
        const WEP104_HEX_ENCODING_BYTES: usize = WEP104_KEY_BYTES * 2;

        let bytes = bytes.as_ref();
        match bytes.len() {
            WEP40_HEX_ENCODING_BYTES | WEP104_HEX_ENCODING_BYTES => {
                let bytes = hex::decode(bytes).map_err(|_| WepError::Encoding)?;
                Ok(match bytes.len() {
                    WEP40_KEY_BYTES => WepKey::Wep40(bytes.try_into().unwrap()),
                    WEP104_KEY_BYTES => WepKey::Wep104(bytes.try_into().unwrap()),
                    _ => unreachable!(),
                })
            }
            _ => WepKey::try_from_literal_bytes(bytes),
        }
    }

    /// Converts unencoded bytes into a WEP key.
    ///
    /// This conversion is not a parse and does **not** accept ASCII hexadecimal encoded keys; the
    /// bytes are interpreted literally and copied as is. Use `Key::parse` for hexadecimal keys.
    pub(crate) fn try_from_literal_bytes(bytes: impl AsRef<[u8]>) -> Result<Self, WepError> {
        let bytes = bytes.as_ref();
        let n = bytes.len();
        match n {
            WEP40_KEY_BYTES => Ok(WepKey::Wep40(bytes.try_into().unwrap())),
            WEP104_KEY_BYTES => Ok(WepKey::Wep104(bytes.try_into().unwrap())),
            _ => Err(WepError::Size(n)),
        }
    }
}

impl AsRef<[u8]> for WepKey {
    fn as_ref(&self) -> &[u8] {
        match self {
            WepKey::Wep40(ref bytes) => bytes,
            WepKey::Wep104(ref bytes) => bytes,
        }
    }
}

impl From<[u8; WEP40_KEY_BYTES]> for WepKey {
    fn from(bytes: [u8; WEP40_KEY_BYTES]) -> Self {
        WepKey::Wep40(bytes)
    }
}

impl From<[u8; WEP104_KEY_BYTES]> for WepKey {
    fn from(bytes: [u8; WEP104_KEY_BYTES]) -> Self {
        WepKey::Wep104(bytes)
    }
}

impl From<WepKey> for Vec<u8> {
    fn from(key: WepKey) -> Self {
        match key {
            WepKey::Wep40(bytes) => bytes.into(),
            WepKey::Wep104(bytes) => bytes.into(),
        }
    }
}

/// WEP authenticator.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WepAuthenticator {
    /// WEP key used to authenticate.
    pub key: WepKey,
}

impl From<WepAuthenticator> for fidl_security::Protocol {
    fn from(_: WepAuthenticator) -> Self {
        fidl_security::Protocol::Wep
    }
}

impl From<WepAuthenticator> for fidl_security::WepCredentials {
    fn from(authenticator: WepAuthenticator) -> Self {
        let key = authenticator.key.into();
        fidl_security::WepCredentials { key }
    }
}

impl TryFrom<fidl_security::WepCredentials> for WepAuthenticator {
    type Error = SecurityError;

    fn try_from(credentials: fidl_security::WepCredentials) -> Result<Self, Self::Error> {
        let key = WepKey::try_from_literal_bytes(credentials.key)?;
        Ok(WepAuthenticator { key })
    }
}

#[cfg(test)]
mod tests {
    use test_case::test_case;

    use crate::security::wep::{WepError, WepKey, WEP104_KEY_BYTES};

    #[test_case([0xFF; 5] => Ok(WepKey::Wep40([0xFF; 5])))]
    #[test_case("wep40" => Ok(WepKey::Wep40([b'w', b'e', b'p', b'4', b'0'])))]
    #[test_case("abcdef0000" => Ok(WepKey::Wep40([0xAB, 0xCD, 0xEF, 0, 0])))]
    #[test_case("FFFFFF0000" => Ok(WepKey::Wep40([0xFF, 0xFF, 0xFF, 0, 0])))]
    #[test_case("aaaAAA0000" => Ok(WepKey::Wep40([0xAA, 0xAA, 0xAA, 0, 0])))]
    #[test_case("authenticates" => Ok(WepKey::Wep104([
        b'a', b'u', b't', b'h', b'e', b'n', b't', b'i', b'c', b'a', b't', b'e', b's',
    ])))]
    #[test_case("ffffffffffffffffffffffffff" => Ok(WepKey::Wep104([0xFF; WEP104_KEY_BYTES])))]
    #[test_case("abcdef" => Err(WepError::Size(6)))]
    #[test_case("abcdefZZZZ" => Err(WepError::Encoding))]
    fn parse_wep_key(bytes: impl AsRef<[u8]>) -> Result<WepKey, WepError> {
        WepKey::parse(bytes)
    }
}
