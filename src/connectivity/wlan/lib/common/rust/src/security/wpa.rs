// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! IEEE Std 802.11-2016 WPA descriptors and credentials.

use fidl_fuchsia_wlan_common_security as fidl_security;
use hex;
use std::convert::{TryFrom, TryInto};
use std::str;
use thiserror::Error;

pub const PSK_SIZE_BYTES: usize = 32;
pub const PASSPHRASE_MIN_SIZE_BYTES: usize = 8;
pub const PASSPHRASE_MAX_SIZE_BYTES: usize = 63;

pub const WPA1: WpaDescriptor = WpaDescriptor::Wpa1 { authentication: () };
pub const WPA2_PERSONAL_TKIP: WpaDescriptor = WpaDescriptor::Wpa2 {
    encryption: Some(Wpa2Encryption::TKIP),
    authentication: Authentication::Personal(()),
};
pub const WPA2_PERSONAL_CCMP: WpaDescriptor = WpaDescriptor::Wpa2 {
    encryption: Some(Wpa2Encryption::CCMP),
    authentication: Authentication::Personal(()),
};
pub const WPA3_PERSONAL_CCMP: WpaDescriptor = WpaDescriptor::Wpa3 {
    encryption: Some(Wpa3Encryption::CCMP),
    authentication: Authentication::Personal(()),
};
pub const WPA3_PERSONAL_GCMP: WpaDescriptor = WpaDescriptor::Wpa3 {
    encryption: Some(Wpa3Encryption::GCMP),
    authentication: Authentication::Personal(()),
};

#[derive(Clone, Copy, Debug, Error)]
#[non_exhaustive]
pub enum PskError {
    #[error("invalid PSK size: {0} bytes")]
    Size(usize),
    #[error("invalid PSK encoding")]
    Encoding,
}

#[derive(Clone, Copy, Debug, Error)]
#[non_exhaustive]
pub enum PassphraseError {
    #[error("invalid WPA passphrase size: {0} bytes")]
    Size(usize),
    #[error("invalid WPA passphrase encoding")]
    Encoding,
}

#[derive(Clone, Copy, Debug, Error)]
#[non_exhaustive]
pub enum WpaError {
    #[error(transparent)]
    Psk(PskError),
    #[error(transparent)]
    Passphrase(PassphraseError),
}

impl From<PskError> for WpaError {
    fn from(error: PskError) -> Self {
        WpaError::Psk(error)
    }
}

impl From<PassphraseError> for WpaError {
    fn from(error: PassphraseError) -> Self {
        WpaError::Passphrase(error)
    }
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

/// WPA authentication suite.
///
/// WPA authentication is divided into two broad suites: WPA Personal and WPA Enterprise. The
/// credentials and mechanisms for each suite differ and both WPA descriptors and authenticators
/// discriminate on this basis.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Authentication<P = (), E = ()> {
    Personal(P),
    Enterprise(E),
}

pub type AuthenticationDescriptor = Authentication<(), ()>;
pub type Credentials = Authentication<PersonalCredentials, EnterpriseCredentials>;

impl<P, E> Authentication<P, E> {
    /// Converts an `Authentication` into a descriptor with no payload (the unit type `()`). Any
    /// payload (i.e., credentials) are dropped.
    pub fn into_descriptor(self) -> Authentication<(), ()> {
        match self {
            Authentication::Personal(_) => Authentication::Personal(()),
            Authentication::Enterprise(_) => Authentication::Enterprise(()),
        }
    }

    pub fn into_personal(self) -> Option<P> {
        if let Authentication::Personal(personal) = self {
            Some(personal)
        } else {
            None
        }
    }

    pub fn into_enterprise(self) -> Option<E> {
        if let Authentication::Enterprise(enterprise) = self {
            Some(enterprise)
        } else {
            None
        }
    }

    pub fn is_personal(&self) -> bool {
        matches!(self, Authentication::Personal(_))
    }

    pub fn is_enterprise(&self) -> bool {
        matches!(self, Authentication::Enterprise(_))
    }

    pub fn as_ref(&self) -> Authentication<&P, &E> {
        match self {
            Authentication::Personal(ref personal) => Authentication::Personal(personal),
            Authentication::Enterprise(ref enterprise) => Authentication::Enterprise(enterprise),
        }
    }
}

impl Default for Authentication<(), ()> {
    fn default() -> Self {
        Authentication::Personal(())
    }
}

impl From<Credentials> for fidl_security::WpaCredentials {
    fn from(credentials: Credentials) -> Self {
        match credentials {
            Credentials::Personal(personal) => personal.into(),
            // TODO(seanolson): Implement conversions.
            Credentials::Enterprise(_) => panic!("WPA Enterprise is unsupported"),
        }
    }
}

impl From<EnterpriseCredentials> for Credentials {
    fn from(enterprise: EnterpriseCredentials) -> Self {
        Credentials::Enterprise(enterprise)
    }
}

impl From<PersonalCredentials> for Credentials {
    fn from(personal: PersonalCredentials) -> Self {
        Credentials::Personal(personal)
    }
}

impl TryFrom<fidl_security::WpaCredentials> for Credentials {
    type Error = WpaError;

    fn try_from(credentials: fidl_security::WpaCredentials) -> Result<Self, Self::Error> {
        match credentials {
            fidl_security::WpaCredentials::Psk(psk) => {
                Ok(Credentials::Personal(Psk::from(psk).into()))
            }
            fidl_security::WpaCredentials::Passphrase(passphrase) => {
                let passphrase = Passphrase::try_from(passphrase)?;
                Ok(Credentials::Personal(passphrase.into()))
            }
            _ => panic!("unknown FIDL credentials variant"),
        }
    }
}

/// WPA Personal credentials.
///
/// Enumerates credentials data used to authenticate with the WPA Personal suite.
///
/// See [`Authentication`] and the [`Credentials`] type definition.
///
/// [`Authentication`]: crate::security::wpa::Authentication
/// [`Credentials`]: crate::security::wpa::Credentials
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PersonalCredentials {
    /// Pre-shared key (PSK).
    Psk(Psk),
    /// Passphrase.
    Passphrase(Passphrase),
}

impl AsRef<[u8]> for PersonalCredentials {
    fn as_ref(&self) -> &[u8] {
        match self {
            PersonalCredentials::Psk(ref psk) => psk.as_ref(),
            PersonalCredentials::Passphrase(ref passphrase) => passphrase.as_ref(),
        }
    }
}

impl From<Passphrase> for PersonalCredentials {
    fn from(passphrase: Passphrase) -> PersonalCredentials {
        PersonalCredentials::Passphrase(passphrase)
    }
}

impl From<PersonalCredentials> for fidl_security::WpaCredentials {
    fn from(credentials: PersonalCredentials) -> Self {
        match credentials {
            PersonalCredentials::Psk(psk) => fidl_security::WpaCredentials::Psk(psk.into()),
            PersonalCredentials::Passphrase(passphrase) => {
                fidl_security::WpaCredentials::Passphrase(passphrase.into())
            }
        }
    }
}

impl From<Psk> for PersonalCredentials {
    fn from(psk: Psk) -> PersonalCredentials {
        PersonalCredentials::Psk(psk)
    }
}

impl TryFrom<fidl_security::WpaCredentials> for PersonalCredentials {
    type Error = WpaError;

    fn try_from(credentials: fidl_security::WpaCredentials) -> Result<Self, Self::Error> {
        match credentials {
            fidl_security::WpaCredentials::Psk(psk) => Ok(PersonalCredentials::Psk(Psk::from(psk))),
            fidl_security::WpaCredentials::Passphrase(passphrase) => {
                let passphrase = Passphrase::try_from(passphrase)?;
                Ok(PersonalCredentials::Passphrase(passphrase))
            }
            _ => panic!("unknown FIDL credentials variant"),
        }
    }
}

// TODO(seanolson): Add variants to `EnterpriseCredentials` as needed and implement conversions.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum EnterpriseCredentials {}

/// General WPA encryption algorithm.
///
/// Names an encryption algorithm used across the family of WPA versions. Some versions of WPA may
/// not support some of these algorithms.
///
/// Note that no types in this crate or module implement these algorithms in any way; this type is
/// strictly nominal.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum Encryption {
    TKIP = 0,
    CCMP = 1,
    GCMP = 2,
}

impl From<Wpa2Encryption> for Encryption {
    fn from(encryption: Wpa2Encryption) -> Self {
        match encryption {
            Wpa2Encryption::TKIP => Encryption::TKIP,
            Wpa2Encryption::CCMP => Encryption::CCMP,
        }
    }
}

impl From<Wpa3Encryption> for Encryption {
    fn from(encryption: Wpa3Encryption) -> Self {
        match encryption {
            Wpa3Encryption::CCMP => Encryption::CCMP,
            Wpa3Encryption::GCMP => Encryption::GCMP,
        }
    }
}

/// WPA2 encryption algorithm.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum Wpa2Encryption {
    TKIP = 0,
    CCMP = 1,
}

impl TryFrom<Encryption> for Wpa2Encryption {
    type Error = ();

    fn try_from(encryption: Encryption) -> Result<Self, Self::Error> {
        match encryption {
            Encryption::TKIP => Ok(Wpa2Encryption::TKIP),
            Encryption::CCMP => Ok(Wpa2Encryption::CCMP),
            _ => Err(()),
        }
    }
}

/// WPA3 encryption algorithm.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum Wpa3Encryption {
    CCMP = 1,
    GCMP = 2,
}

impl TryFrom<Encryption> for Wpa3Encryption {
    type Error = ();

    fn try_from(encryption: Encryption) -> Result<Self, Self::Error> {
        match encryption {
            Encryption::CCMP => Ok(Wpa3Encryption::CCMP),
            Encryption::GCMP => Ok(Wpa3Encryption::GCMP),
            _ => Err(()),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Wpa<P = (), E = ()> {
    Wpa1 { authentication: P },
    Wpa2 { encryption: Option<Wpa2Encryption>, authentication: Authentication<P, E> },
    Wpa3 { encryption: Option<Wpa3Encryption>, authentication: Authentication<P, E> },
}

impl<P, E> Wpa<P, E> {
    /// Converts a `Wpa` into a descriptor with no payload (the unit type `()`). Any payload (i.e.,
    /// credentials) are dropped.
    pub fn into_descriptor(self) -> Wpa<(), ()> {
        match self {
            Wpa::Wpa1 { .. } => Wpa::Wpa1 { authentication: () },
            Wpa::Wpa2 { encryption, authentication } => {
                Wpa::Wpa2 { encryption, authentication: authentication.into_descriptor() }
            }
            Wpa::Wpa3 { encryption, authentication } => {
                Wpa::Wpa3 { encryption, authentication: authentication.into_descriptor() }
            }
        }
    }

    /// Gets the configured encryption algorithm.
    ///
    /// This function coalesces the encryption algorithms supported by various versions of WPA and
    /// returns the generalized `Encryption` type.
    ///
    /// Note that WPA1 is not configurable in this way and always uses TKIP (and so this function
    /// will always return `Some(Encryption::TKIP)` for WPA1. For other versions of WPA, the
    /// configured enryption algorithm may not be known and this function returns `None` in that
    /// case. Importantly, this does **not** mean that no encryption used, but rather that the
    /// algorithm is unspecified or unknown.
    pub fn encryption(&self) -> Option<Encryption> {
        match self {
            Wpa::Wpa1 { .. } => Some(Encryption::TKIP),
            Wpa::Wpa2 { encryption, .. } => encryption.map(Into::into),
            Wpa::Wpa3 { encryption, .. } => encryption.map(Into::into),
        }
    }
}

impl<P, E> From<Wpa<P, E>> for fidl_security::Protocol {
    fn from(wpa: Wpa<P, E>) -> Self {
        match wpa {
            Wpa::Wpa1 { .. } => fidl_security::Protocol::Wpa1,
            Wpa::Wpa2 { authentication, .. } => match authentication {
                Authentication::Personal(_) => fidl_security::Protocol::Wpa2Personal,
                Authentication::Enterprise(_) => fidl_security::Protocol::Wpa2Enterprise,
            },
            Wpa::Wpa3 { authentication, .. } => match authentication {
                Authentication::Personal(_) => fidl_security::Protocol::Wpa3Personal,
                Authentication::Enterprise(_) => fidl_security::Protocol::Wpa3Enterprise,
            },
        }
    }
}

/// WPA descriptor.
///
/// Describes the configuration of the WPA security protocol.
pub type WpaDescriptor = Wpa<(), ()>;
/// WPA authenticator.
///
/// Provides credentials for authenticating against the described configuration of the WPA security
/// protocol.
pub type WpaAuthenticator = Wpa<PersonalCredentials, EnterpriseCredentials>;

impl WpaAuthenticator {
    /// Converts a WPA authenticator into its credentials (`Authentication`).
    pub fn into_credentials(self) -> Credentials {
        match self {
            Wpa::Wpa1 { authentication: personal } => Authentication::Personal(personal),
            Wpa::Wpa2 { authentication, .. } | Wpa::Wpa3 { authentication, .. } => authentication,
        }
    }

    /// Gets a reference to the credentials (`Authentication`) of a WPA authenticator.
    pub fn as_credentials(&self) -> Authentication<&PersonalCredentials, &EnterpriseCredentials> {
        match self {
            Wpa::Wpa1 { authentication: ref personal } => Authentication::Personal(personal),
            Wpa::Wpa2 { ref authentication, .. } | Wpa::Wpa3 { ref authentication, .. } => {
                authentication.as_ref()
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use std::convert::TryFrom;

    use crate::security::wpa::{Passphrase, PassphraseError, Psk, PskError, PSK_SIZE_BYTES};

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
