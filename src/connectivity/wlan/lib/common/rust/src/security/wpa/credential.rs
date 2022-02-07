// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use hex;
use std::convert::{TryFrom, TryInto};
use std::fmt::Debug;
use std::str;
use thiserror::Error;

pub const PSK_SIZE_BYTES: usize = 32;
pub const PASSPHRASE_MIN_SIZE_BYTES: usize = 8;
pub const PASSPHRASE_MAX_SIZE_BYTES: usize = 63;

#[derive(Clone, Copy, Debug, Error, Eq, PartialEq)]
#[non_exhaustive]
pub enum PskError {
    #[error("invalid PSK size: {0} bytes")]
    Size(usize),
    #[error("invalid PSK encoding")]
    Encoding,
}

#[derive(Clone, Copy, Debug, Error, Eq, PartialEq)]
#[non_exhaustive]
pub enum PassphraseError {
    #[error("invalid WPA passphrase size: {0} bytes")]
    Size(usize),
    #[error("invalid WPA passphrase encoding")]
    Encoding,
}

/// WPA pre-shared key (PSK).
#[derive(Clone, Debug, Eq, PartialEq)]
#[repr(transparent)]
pub struct Psk(pub [u8; PSK_SIZE_BYTES]);

impl Psk {
    /// Parses a PSK from a byte sequence.
    ///
    /// This function parses both unencoded and ASCII hexadecimal encoded PSKs.
    ///
    /// Note that `Psk` does not provide a mechanism to restore the original byte sequence parsed
    /// by this function, so the exact encoding of ASCII hexadecimal encoded PSKs may be lost.
    ///
    /// # Errors
    ///
    /// Returns an error if the size or encoding of the byte sequence is incompatible.
    pub fn parse(bytes: impl AsRef<[u8]>) -> Result<Self, PskError> {
        let bytes = bytes.as_ref();
        if bytes.len() == PSK_SIZE_BYTES * 2 {
            let bytes = hex::decode(bytes).map_err(|_| PskError::Encoding)?;
            Ok(Psk(bytes.try_into().unwrap()))
        } else {
            Psk::try_from(bytes)
        }
    }
}

impl AsRef<[u8]> for Psk {
    fn as_ref(&self) -> &[u8] {
        &self.0
    }
}

impl From<[u8; PSK_SIZE_BYTES]> for Psk {
    fn from(bytes: [u8; 32]) -> Self {
        Psk(bytes)
    }
}

impl From<Psk> for [u8; PSK_SIZE_BYTES] {
    fn from(psk: Psk) -> Self {
        psk.0
    }
}

impl From<Psk> for Vec<u8> {
    fn from(psk: Psk) -> Self {
        psk.0.into()
    }
}

/// Converts unencoded bytes into a PSK.
///
/// This conversion is not a parse and does **not** accept ASCII hexadecimal encoded PSKs; the
/// bytes are copied as is. Use `Psk::parse` for hexadecimal keys.
impl<'a> TryFrom<&'a [u8]> for Psk {
    type Error = PskError;

    fn try_from(bytes: &'a [u8]) -> Result<Self, PskError> {
        let n = bytes.len();
        let psk = Psk(bytes.try_into().map_err(|_| PskError::Size(n))?);
        Ok(psk)
    }
}

/// WPA passphrase.
///
/// Passphrases are UTF-8 encoded and the underlying representation is `String`.
#[derive(Clone, Debug, Eq, PartialEq)]
#[repr(transparent)]
pub struct Passphrase {
    text: String,
}

impl Passphrase {
    /// Consumes the `Passphrase` and performs a fallible write.
    ///
    /// The function `f` is used to mutate the `String` representation of the passphrase.
    ///
    /// # Errors
    ///
    /// Returns an error if the mutated `String` is not a valid WPA passphrase. Namely, the
    /// `String` must consist of between `PASSPHRASE_MIN_SIZE_BYTES` and
    /// `PASSPHRASE_MAX_SIZE_BYTES` bytes (**not** characters or graphemes).
    ///
    /// Note that if an error is returned, then the `Passphrase` is consumed. Use `clone` to
    /// recover the original `Passphrase`.
    pub fn try_write_with<F>(mut self, mut f: F) -> Result<Self, PassphraseError>
    where
        F: FnMut(&mut String),
    {
        f(&mut self.text);
        Passphrase::check(&self.text)?;
        Ok(self)
    }

    fn check(text: &str) -> Result<(), PassphraseError> {
        let n = text.as_bytes().len();
        if n < PASSPHRASE_MIN_SIZE_BYTES || n > PASSPHRASE_MAX_SIZE_BYTES {
            return Err(PassphraseError::Size(n));
        }
        Ok(())
    }
}

impl AsRef<[u8]> for Passphrase {
    fn as_ref(&self) -> &[u8] {
        &self.text.as_bytes()
    }
}

impl AsRef<str> for Passphrase {
    fn as_ref(&self) -> &str {
        &self.text
    }
}

impl From<Passphrase> for Vec<u8> {
    fn from(passphrase: Passphrase) -> Self {
        passphrase.text.into_bytes()
    }
}

impl From<Passphrase> for String {
    fn from(passphrase: Passphrase) -> Self {
        passphrase.text
    }
}

impl<'a> TryFrom<&'a [u8]> for Passphrase {
    type Error = PassphraseError;

    fn try_from(bytes: &'a [u8]) -> Result<Self, PassphraseError> {
        let text = str::from_utf8(bytes).map_err(|_| PassphraseError::Encoding)?;
        Passphrase::check(text.as_ref())?;
        Ok(Passphrase { text: text.to_owned() })
    }
}

impl<'a> TryFrom<&'a str> for Passphrase {
    type Error = PassphraseError;

    fn try_from(text: &'a str) -> Result<Self, PassphraseError> {
        Passphrase::check(text)?;
        Ok(Passphrase { text: text.to_owned() })
    }
}

impl TryFrom<String> for Passphrase {
    type Error = PassphraseError;

    fn try_from(text: String) -> Result<Self, PassphraseError> {
        Passphrase::check(text.as_ref())?;
        Ok(Passphrase { text })
    }
}

impl TryFrom<Vec<u8>> for Passphrase {
    type Error = PassphraseError;

    fn try_from(bytes: Vec<u8>) -> Result<Self, PassphraseError> {
        let bytes: &[u8] = bytes.as_ref();
        Passphrase::try_from(bytes)
    }
}

#[cfg(test)]
mod tests {
    use std::convert::TryFrom;

    use crate::security::wpa::credential::{
        Passphrase, PassphraseError, Psk, PskError, PSK_SIZE_BYTES,
    };

    #[test]
    fn convert_passphrase_bad_encoding() {
        assert!(matches!(
            Passphrase::try_from([0xFFu8, 0xFF, 0xFF, 0xFF, 0xFF].as_ref()),
            Err(PassphraseError::Encoding)
        ));
    }

    #[test]
    fn passphrase_bad_size() {
        assert!(matches!(Passphrase::try_from("tiny"), Err(PassphraseError::Size(4))));
        assert!(matches!(
            Passphrase::try_from(
                "huuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuge"
            ),
            Err(PassphraseError::Size(65))
        ));

        let passphrase = Passphrase::try_from("itsasecret").unwrap();
        assert!(matches!(
            passphrase.try_write_with(|text| {
                *text = "tiny".to_string();
            }),
            Err(PassphraseError::Size(4))
        ));
    }

    #[test]
    fn parse_psk() {
        // Parse binary PSK.
        assert_eq!(
            Psk::parse("therearethirtytwobytesineverypsk").unwrap(),
            Psk(*b"therearethirtytwobytesineverypsk")
        );
        // Parse hexadecimal ASCII encoded PSK.
        assert_eq!(
            Psk::parse("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF").unwrap(),
            Psk::from([0xFF; PSK_SIZE_BYTES])
        );
    }

    #[test]
    fn parse_psk_bad_size() {
        assert!(matches!(Psk::parse(b"lolwut"), Err(PskError::Size(6))));
    }

    #[test]
    fn parse_psk_bad_encoding() {
        assert!(matches!(
            Psk::parse("ZZFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"),
            Err(PskError::Encoding)
        ));
    }
}
