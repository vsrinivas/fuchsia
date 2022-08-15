// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! IEEE Std 802.11-2016 WPA descriptors and credentials.
//!
//! This module describes WPA security protocols. Its primary API is composed of the
//! [`WpaDescriptor`] and [`WpaAuthenticator`] types.
//!
//! WPA is more complex than WEP and several versions and authentication suites are provided. Types
//! in this module fall into two broad categories: _specific_ and _general_. The general types
//! provide representations of WPA primitives regardless of version and suite, and provide more
//! ergonomic APIs. An example of a general type is [`Cipher`]. General types are exposed by
//! specific types, which diverge across versions and suites and have no invalid representations.
//! An example of a specific type is [`Wpa3Cipher`].
//!
//! Note that [`SecurityDescriptor`] and [`SecurityAuthenticator`] are always valid specifications
//! of security protocols and authentication. When interacting with WPA, it is typically enough to
//! use functions like [`WpaAuthenticator::to_credentials`] and [`WpaDescriptor::cipher`]. While
//! the types returned from these functions are general, they are derived from a valid security
//! protocol specification.
//!
//! [`Cipher`]: crate::security::wpa::Cipher
//! [`SecurityAuthenticator`]: crate::security::SecurityAuthenticator
//! [`SecurityDescriptor`]: crate::security::SecurityDescriptor
//! [`Wpa3Cipher`]: crate::security::wpa::Wpa3Cipher
//! [`WpaAuthenticator`]: crate::security::wpa::WpaAuthenticator
//! [`WpaAuthenticator::to_credentials`]: crate::security::wpa::Wpa::to_credentials
//! [`WpaDescriptor`]: crate::security::wpa::WpaDescriptor
//! [`WpaDescriptor::cipher`]: crate::security::wpa::Wpa::cipher

pub mod credential;
mod data;

use derivative::Derivative;
use fidl_fuchsia_wlan_common_security as fidl_security;
use std::convert::TryFrom;
use std::fmt::Debug;
use thiserror::Error;

use crate::security::{
    wpa::{
        credential::{Passphrase, PassphraseError, Psk, PskError},
        data::{CredentialData, EnterpriseData, PersonalData},
    },
    BareCredentials, SecurityError,
};

pub use crate::security::wpa::data::AuthenticatorData;

#[derive(Clone, Copy, Debug, Error, Eq, PartialEq)]
#[non_exhaustive]
pub enum WpaError {
    #[error(transparent)]
    Psk(#[from] PskError),
    #[error(transparent)]
    Passphrase(#[from] PassphraseError),
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

/// General WPA credentials.
///
/// Provides credentials keyed by WPA Personal and WPA Enterprise suite. This type is general and
/// can represent credentials used across versions of WPA.
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

    /// Converts an `Authentication` describing a particular WPA version into a general type that
    /// describes credentials used across versions of WPA.
    pub fn into_credentials(self) -> Credentials
    where
        PersonalCredentials: From<P>,
        EnterpriseCredentials: From<E>,
    {
        match self {
            Authentication::Personal(personal) => Authentication::Personal(personal.into()),
            Authentication::Enterprise(enterprise) => Authentication::Enterprise(enterprise.into()),
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

impl From<Wpa1Credentials> for Authentication<Wpa1Credentials, ()> {
    fn from(credentials: Wpa1Credentials) -> Self {
        Authentication::Personal(credentials)
    }
}

// TODO(fxbug.dev/92693): Specify the WPA2 Enterprise type.
impl From<Wpa2PersonalCredentials> for Authentication<Wpa2PersonalCredentials, ()> {
    fn from(credentials: Wpa2PersonalCredentials) -> Self {
        Authentication::Personal(credentials)
    }
}

// TODO(fxbug.dev/92693): Specify the WPA3 Enterprise type.
impl From<Wpa3PersonalCredentials> for Authentication<Wpa3PersonalCredentials, ()> {
    fn from(credentials: Wpa3PersonalCredentials) -> Self {
        Authentication::Personal(credentials)
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

impl<P, E> From<Authentication<P, E>> for fidl_security::WpaCredentials
where
    P: Into<fidl_security::WpaCredentials>,
    E: Into<fidl_security::WpaCredentials>,
{
    fn from(authentication: Authentication<P, E>) -> Self {
        match authentication {
            Authentication::Personal(personal) => personal.into(),
            // TODO(fxbug.dev/92693): Implement conversions for WPA Enterprise.
            Authentication::Enterprise(_) => panic!("WPA Enterprise is unsupported"),
        }
    }
}

/// Conversion of general WPA credentials into bare credentials.
impl From<Credentials> for BareCredentials {
    fn from(credentials: Credentials) -> Self {
        match credentials {
            Credentials::Personal(personal) => match personal {
                PersonalCredentials::Passphrase(passphrase) => {
                    BareCredentials::WpaPassphrase(passphrase)
                }
                PersonalCredentials::Psk(psk) => BareCredentials::WpaPsk(psk),
            },
            // TODO(fxbug.dev/92693): Implement conversions for WPA Enterprise.
            Credentials::Enterprise(_) => panic!("WPA Enterprise is unsupported"),
        }
    }
}

/// General WPA Personal credentials.
///
/// Enumerates credential data used across the family of WPA versions to authenticate with the WPA
/// Personal suite. This is a superset of WPA Personal credentials for each version of WPA.
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

impl From<Wpa1Credentials> for PersonalCredentials {
    fn from(credentials: Wpa1Credentials) -> Self {
        match credentials {
            Wpa1Credentials::Psk(psk) => PersonalCredentials::Psk(psk),
            Wpa1Credentials::Passphrase(passphrase) => PersonalCredentials::Passphrase(passphrase),
        }
    }
}

impl From<Wpa2PersonalCredentials> for PersonalCredentials {
    fn from(credentials: Wpa2PersonalCredentials) -> Self {
        match credentials {
            Wpa2PersonalCredentials::Psk(psk) => PersonalCredentials::Psk(psk),
            Wpa2PersonalCredentials::Passphrase(passphrase) => {
                PersonalCredentials::Passphrase(passphrase)
            }
        }
    }
}

impl From<Wpa3PersonalCredentials> for PersonalCredentials {
    fn from(credentials: Wpa3PersonalCredentials) -> Self {
        match credentials {
            Wpa3PersonalCredentials::Passphrase(passphrase) => {
                PersonalCredentials::Passphrase(passphrase)
            }
        }
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

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Wpa1Credentials {
    Psk(Psk),
    Passphrase(Passphrase),
}

impl AsRef<[u8]> for Wpa1Credentials {
    fn as_ref(&self) -> &[u8] {
        match self {
            Wpa1Credentials::Psk(ref psk) => psk.as_ref(),
            Wpa1Credentials::Passphrase(ref passphrase) => passphrase.as_ref(),
        }
    }
}

impl From<Passphrase> for Wpa1Credentials {
    fn from(passphrase: Passphrase) -> Self {
        Wpa1Credentials::Passphrase(passphrase)
    }
}

impl From<Psk> for Wpa1Credentials {
    fn from(psk: Psk) -> Self {
        Wpa1Credentials::Psk(psk)
    }
}

impl From<Wpa1Credentials> for fidl_security::WpaCredentials {
    fn from(credentials: Wpa1Credentials) -> Self {
        PersonalCredentials::from(credentials).into()
    }
}

// This is implemented (infallibly) via `TryFrom`, because the set of accepted WPA credentials may
// expand with revisions to IEEE Std 802.11 and conversions from the general enumeration to the
// specific enumeration should be handled in a fallible and defensive way by client code.
impl TryFrom<PersonalCredentials> for Wpa1Credentials {
    type Error = SecurityError;

    fn try_from(credentials: PersonalCredentials) -> Result<Self, Self::Error> {
        match credentials {
            PersonalCredentials::Psk(psk) => Ok(Wpa1Credentials::Psk(psk)),
            PersonalCredentials::Passphrase(passphrase) => {
                Ok(Wpa1Credentials::Passphrase(passphrase))
            }
        }
    }
}

impl TryFrom<fidl_security::WpaCredentials> for Wpa1Credentials {
    type Error = SecurityError;

    fn try_from(credentials: fidl_security::WpaCredentials) -> Result<Self, Self::Error> {
        match credentials {
            fidl_security::WpaCredentials::Psk(psk) => Ok(Wpa1Credentials::Psk(Psk::from(psk))),
            fidl_security::WpaCredentials::Passphrase(passphrase) => {
                let passphrase = Passphrase::try_from(passphrase)?;
                Ok(Wpa1Credentials::Passphrase(passphrase))
            }
            _ => panic!("unknown FIDL credentials variant"),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Wpa2PersonalCredentials {
    Psk(Psk),
    Passphrase(Passphrase),
}

impl AsRef<[u8]> for Wpa2PersonalCredentials {
    fn as_ref(&self) -> &[u8] {
        match self {
            Wpa2PersonalCredentials::Psk(ref psk) => psk.as_ref(),
            Wpa2PersonalCredentials::Passphrase(ref passphrase) => passphrase.as_ref(),
        }
    }
}

impl From<Passphrase> for Wpa2PersonalCredentials {
    fn from(passphrase: Passphrase) -> Self {
        Wpa2PersonalCredentials::Passphrase(passphrase)
    }
}

impl From<Psk> for Wpa2PersonalCredentials {
    fn from(psk: Psk) -> Self {
        Wpa2PersonalCredentials::Psk(psk)
    }
}

impl From<Wpa2PersonalCredentials> for fidl_security::WpaCredentials {
    fn from(credentials: Wpa2PersonalCredentials) -> Self {
        PersonalCredentials::from(credentials).into()
    }
}

// This is implemented (infallibly) via `TryFrom`, because the set of accepted WPA credentials may
// expand with revisions to IEEE Std 802.11 and conversions from the general enumeration to the
// specific enumeration should be handled in a fallible and defensive way by client code.
impl TryFrom<PersonalCredentials> for Wpa2PersonalCredentials {
    type Error = SecurityError;

    fn try_from(credentials: PersonalCredentials) -> Result<Self, Self::Error> {
        match credentials {
            PersonalCredentials::Psk(psk) => Ok(Wpa2PersonalCredentials::Psk(psk)),
            PersonalCredentials::Passphrase(passphrase) => {
                Ok(Wpa2PersonalCredentials::Passphrase(passphrase))
            }
        }
    }
}

impl TryFrom<fidl_security::WpaCredentials> for Wpa2PersonalCredentials {
    type Error = SecurityError;

    fn try_from(credentials: fidl_security::WpaCredentials) -> Result<Self, Self::Error> {
        match credentials {
            fidl_security::WpaCredentials::Psk(psk) => {
                Ok(Wpa2PersonalCredentials::Psk(Psk::from(psk)))
            }
            fidl_security::WpaCredentials::Passphrase(passphrase) => {
                let passphrase = Passphrase::try_from(passphrase)?;
                Ok(Wpa2PersonalCredentials::Passphrase(passphrase))
            }
            _ => panic!("unknown FIDL credentials variant"),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Wpa3PersonalCredentials {
    Passphrase(Passphrase),
}

impl AsRef<[u8]> for Wpa3PersonalCredentials {
    fn as_ref(&self) -> &[u8] {
        match self {
            Wpa3PersonalCredentials::Passphrase(ref passphrase) => passphrase.as_ref(),
        }
    }
}

impl From<Passphrase> for Wpa3PersonalCredentials {
    fn from(passphrase: Passphrase) -> Self {
        Wpa3PersonalCredentials::Passphrase(passphrase)
    }
}

impl From<Wpa3PersonalCredentials> for fidl_security::WpaCredentials {
    fn from(credentials: Wpa3PersonalCredentials) -> Self {
        PersonalCredentials::from(credentials).into()
    }
}

impl TryFrom<PersonalCredentials> for Wpa3PersonalCredentials {
    type Error = SecurityError;

    fn try_from(credentials: PersonalCredentials) -> Result<Self, Self::Error> {
        match credentials {
            PersonalCredentials::Passphrase(passphrase) => {
                Ok(Wpa3PersonalCredentials::Passphrase(passphrase))
            }
            _ => Err(SecurityError::Incompatible),
        }
    }
}

impl TryFrom<fidl_security::WpaCredentials> for Wpa3PersonalCredentials {
    type Error = SecurityError;

    fn try_from(credentials: fidl_security::WpaCredentials) -> Result<Self, Self::Error> {
        match credentials {
            fidl_security::WpaCredentials::Psk(_) => Err(SecurityError::Incompatible),
            fidl_security::WpaCredentials::Passphrase(passphrase) => {
                let passphrase = Passphrase::try_from(passphrase)?;
                Ok(Wpa3PersonalCredentials::Passphrase(passphrase))
            }
            _ => panic!("unknown FIDL credentials variant"),
        }
    }
}

// TODO(fxbug.dev/92693): Add variants to `EnterpriseCredentials` as needed and implement
//                        conversions.
/// General WPA Enterprise credentials.
///
/// Enumerates credential data used across the family of WPA versions to authenticate with the WPA
/// Enterprise suite. This is a superset of WPA Enterprise credentials for each version of WPA.
///
/// See [`Authentication`] and the [`Credentials`] type definition.
///
/// [`Authentication`]: crate::security::wpa::Authentication
/// [`Credentials`]: crate::security::wpa::Credentials
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum EnterpriseCredentials {}

impl From<()> for EnterpriseCredentials {
    fn from(_: ()) -> Self {
        // TODO(fxbug.dev/92693): Implement conversions for WPA Enterprise.
        panic!("WPA Enterprise is unsupported")
    }
}

impl From<EnterpriseCredentials> for fidl_security::WpaCredentials {
    fn from(_: EnterpriseCredentials) -> Self {
        // TODO(fxbug.dev/92693): Implement conversions for WPA Enterprise.
        panic!("WPA Enterprise is unsupported")
    }
}

/// General WPA cipher.
///
/// Names a cipher used across the family of WPA versions. Some versions of WPA may not support
/// some of these algorithms.
///
/// Note that no types in this crate or module implement these ciphers nor encryption algorithms in
/// any way; this type is strictly nominal.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum Cipher {
    TKIP = 0,
    CCMP = 1,
    GCMP = 2,
}

impl From<Wpa2Cipher> for Cipher {
    fn from(cipher: Wpa2Cipher) -> Self {
        match cipher {
            Wpa2Cipher::TKIP => Cipher::TKIP,
            Wpa2Cipher::CCMP => Cipher::CCMP,
        }
    }
}

impl From<Wpa3Cipher> for Cipher {
    fn from(cipher: Wpa3Cipher) -> Self {
        match cipher {
            Wpa3Cipher::CCMP => Cipher::CCMP,
            Wpa3Cipher::GCMP => Cipher::GCMP,
        }
    }
}

/// WPA2 cipher.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum Wpa2Cipher {
    TKIP = 0,
    CCMP = 1,
}

impl TryFrom<Cipher> for Wpa2Cipher {
    type Error = SecurityError;

    fn try_from(cipher: Cipher) -> Result<Self, Self::Error> {
        match cipher {
            Cipher::TKIP => Ok(Wpa2Cipher::TKIP),
            Cipher::CCMP => Ok(Wpa2Cipher::CCMP),
            _ => Err(SecurityError::Incompatible),
        }
    }
}

/// WPA3 cipher.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum Wpa3Cipher {
    CCMP = 1,
    GCMP = 2,
}

impl TryFrom<Cipher> for Wpa3Cipher {
    type Error = SecurityError;

    fn try_from(cipher: Cipher) -> Result<Self, Self::Error> {
        match cipher {
            Cipher::CCMP => Ok(Wpa3Cipher::CCMP),
            Cipher::GCMP => Ok(Wpa3Cipher::GCMP),
            _ => Err(SecurityError::Incompatible),
        }
    }
}

/// General description of WPA and its authentication.
///
/// This type is typically used via the [`WpaAuthenticator`] and [`WpaDescriptor`] type
/// definitions, which represent WPA authenticators and descriptors, respectively. It is not
/// generally necessary to interact with this type directly.
///
/// This type constructor describes the configuration of a WPA security protocol and additionally
/// contains authentication (credential) data defined by its type parameter. For authenticators,
/// this data represents credentials, such as a PSK or passphrase. For descriptors, this data is
/// the unit type `()`, and so no authentication data is available. See the [`data`] module and
/// [`CredentialData`] trait for more.
///
/// [`CredentialData`]: crate::security::wpa::data::CredentialData
/// [`data`]: crate::security::wpa::data
/// [`WpaAuthenticator`]: crate::security::wpa::WpaAuthenticator
/// [`WpaDescriptor`]: crate::security::wpa::WpaDescriptor
#[derive(Derivative)]
#[derivative(
    Clone(bound = ""),
    Copy(bound = "
        <C::Personal as PersonalData>::Wpa1: Copy,
        <C::Personal as PersonalData>::Wpa2: Copy,
        <C::Personal as PersonalData>::Wpa3: Copy,
        <C::Enterprise as EnterpriseData>::Wpa2: Copy,
        <C::Enterprise as EnterpriseData>::Wpa3: Copy,
    "),
    Debug(bound = ""),
    Eq(bound = ""),
    PartialEq(bound = "")
)]
pub enum Wpa<C = ()>
where
    C: CredentialData,
{
    Wpa1 {
        credentials: <C::Personal as PersonalData>::Wpa1,
    },
    Wpa2 {
        cipher: Option<Wpa2Cipher>,
        authentication: Authentication<
            <C::Personal as PersonalData>::Wpa2,
            <C::Enterprise as EnterpriseData>::Wpa2,
        >,
    },
    Wpa3 {
        cipher: Option<Wpa3Cipher>,
        authentication: Authentication<
            <C::Personal as PersonalData>::Wpa3,
            <C::Enterprise as EnterpriseData>::Wpa3,
        >,
    },
}

impl<C> Wpa<C>
where
    C: CredentialData,
{
    /// Converts a `Wpa` into a descriptor with no payload (the unit type `()`). Any payload (i.e.,
    /// credentials) are dropped.
    pub fn into_descriptor(self) -> Wpa<()> {
        match self {
            Wpa::Wpa1 { .. } => Wpa::Wpa1 { credentials: () },
            Wpa::Wpa2 { cipher, authentication } => {
                Wpa::Wpa2 { cipher, authentication: authentication.into_descriptor() }
            }
            Wpa::Wpa3 { cipher, authentication } => {
                Wpa::Wpa3 { cipher, authentication: authentication.into_descriptor() }
            }
        }
    }

    /// Gets the configured cipher.
    ///
    /// This function coalesces the ciphers supported by various versions of WPA and returns the
    /// generalized `Cipher` type.
    ///
    /// Note that WPA1 is not configurable in this way and always uses TKIP (and so this function
    /// will always return `Some(Cipher::TKIP)` for WPA1). For other versions of WPA, the
    /// configured cipher may not be known and this function returns `None` in that case.
    /// Importantly, this does **not** mean that no cipher is used, but rather that the specific
    /// cipher is unspecified or unknown.
    pub fn cipher(&self) -> Option<Cipher> {
        match self {
            Wpa::Wpa1 { .. } => Some(Cipher::TKIP),
            Wpa::Wpa2 { cipher, .. } => cipher.map(Into::into),
            Wpa::Wpa3 { cipher, .. } => cipher.map(Into::into),
        }
    }
}

impl<C> From<Wpa<C>> for fidl_security::Protocol
where
    C: CredentialData,
{
    fn from(wpa: Wpa<C>) -> Self {
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
/// Describes the configuration of the WPA security protocol. WPA descriptors optionally specify a
/// pairwise cipher. Descriptors that lack this information are simply less specific than
/// descriptors that include it.
pub type WpaDescriptor = Wpa<()>;

impl WpaDescriptor {
    pub fn bind(self, credentials: BareCredentials) -> Result<WpaAuthenticator, SecurityError> {
        match credentials {
            BareCredentials::WpaPassphrase(passphrase) => match self {
                WpaDescriptor::Wpa1 { .. } => {
                    Ok(WpaAuthenticator::Wpa1 { credentials: passphrase.into() })
                }
                WpaDescriptor::Wpa2 { cipher, authentication } => Ok(WpaAuthenticator::Wpa2 {
                    cipher,
                    authentication: match authentication {
                        Authentication::Personal(_) => {
                            Ok(Authentication::Personal(passphrase.into()))
                        }
                        Authentication::Enterprise(_) => Err(SecurityError::Unsupported),
                    }?,
                }),
                WpaDescriptor::Wpa3 { cipher, authentication } => Ok(WpaAuthenticator::Wpa3 {
                    cipher,
                    authentication: match authentication {
                        Authentication::Personal(_) => {
                            Ok(Authentication::Personal(passphrase.into()))
                        }
                        Authentication::Enterprise(_) => Err(SecurityError::Unsupported),
                    }?,
                }),
            },
            BareCredentials::WpaPsk(psk) => match self {
                WpaDescriptor::Wpa1 { .. } => {
                    Ok(WpaAuthenticator::Wpa1 { credentials: psk.into() })
                }
                WpaDescriptor::Wpa2 { cipher, authentication } => Ok(WpaAuthenticator::Wpa2 {
                    cipher,
                    authentication: match authentication {
                        Authentication::Personal(_) => Ok(Authentication::Personal(psk.into())),
                        Authentication::Enterprise(_) => Err(SecurityError::Unsupported),
                    }?,
                }),
                WpaDescriptor::Wpa3 { .. } => Err(SecurityError::Incompatible),
            },
            _ => Err(SecurityError::Incompatible),
        }
    }
}

/// WPA authenticator.
///
/// Provides credentials for authenticating against the described configuration of the WPA security
/// protocol.
pub type WpaAuthenticator = Wpa<AuthenticatorData>;

impl WpaAuthenticator {
    /// Converts a WPA authenticator into its credentials.
    ///
    /// The output of this function is general and describes all versions of WPA. This means it
    /// cannot be used to determine which version of WPA has been specified. Note that WPA1
    /// specifies its credentials as WPA1 Personal via [`Authentication::Personal`] even though
    /// WPA1 has no Enterprise suite.
    ///
    /// [`Authentication::Personal`]: crate::security::wpa::Authentication::Personal
    pub fn into_credentials(self) -> Credentials {
        match self {
            Wpa::Wpa1 { credentials } => Authentication::Personal(credentials.into()),
            Wpa::Wpa2 { authentication, .. } => authentication.into_credentials(),
            Wpa::Wpa3 { authentication, .. } => authentication.into_credentials(),
        }
    }

    pub fn to_credentials(&self) -> Credentials {
        match self {
            Wpa::Wpa1 { ref credentials } => Authentication::Personal(credentials.clone().into()),
            Wpa::Wpa2 { ref authentication, .. } => authentication.clone().into_credentials(),
            Wpa::Wpa3 { ref authentication, .. } => authentication.clone().into_credentials(),
        }
    }
}

#[cfg(test)]
mod tests {
    use fidl_fuchsia_wlan_common_security as fidl_security;
    use std::convert::{TryFrom, TryInto};
    use test_case::test_case;

    use crate::security::{
        wep::{WepKey, WEP40_KEY_BYTES},
        wpa::{
            self,
            credential::{Passphrase, Psk, PSK_SIZE_BYTES},
        },
        BareCredentials, SecurityError,
    };

    fn wep_key() -> WepKey {
        [170u8; WEP40_KEY_BYTES].into()
    }

    fn wpa_psk() -> Psk {
        [170u8; PSK_SIZE_BYTES].into()
    }

    fn wpa_passphrase() -> Passphrase {
        Passphrase::try_from("password").unwrap()
    }

    trait PersonalCredentialsTestCase: Sized {
        fn psk() -> Self;
        fn passphrase() -> Self;
    }

    impl PersonalCredentialsTestCase for wpa::PersonalCredentials {
        fn psk() -> Self {
            wpa::PersonalCredentials::Psk(wpa_psk())
        }

        fn passphrase() -> Self {
            wpa::PersonalCredentials::Passphrase(wpa_passphrase())
        }
    }

    trait WpaCredentialsTestCase: Sized {
        fn psk() -> Self;
        fn passphrase() -> Self;
    }

    impl WpaCredentialsTestCase for fidl_security::WpaCredentials {
        fn psk() -> Self {
            fidl_security::WpaCredentials::Psk(wpa_psk().0)
        }

        fn passphrase() -> Self {
            fidl_security::WpaCredentials::Passphrase(wpa_passphrase().into())
        }
    }

    trait BareCredentialsTestCase: Sized {
        fn wep_key() -> Self;
        fn psk() -> Self;
        fn passphrase() -> Self;
    }

    impl BareCredentialsTestCase for BareCredentials {
        fn wep_key() -> Self {
            BareCredentials::WepKey(wep_key())
        }

        fn psk() -> Self {
            BareCredentials::WpaPsk(wpa_psk())
        }

        fn passphrase() -> Self {
            BareCredentials::WpaPassphrase(wpa_passphrase())
        }
    }

    trait WpaDescriptorTestCase: Sized {
        const WPA1: Self;
        const WPA2_PERSONAL: Self;
        const WPA3_PERSONAL: Self;
    }

    impl WpaDescriptorTestCase for wpa::WpaDescriptor {
        const WPA1: Self = wpa::WpaDescriptor::Wpa1 { credentials: () };
        const WPA2_PERSONAL: Self = wpa::WpaDescriptor::Wpa2 {
            cipher: None,
            authentication: wpa::Authentication::Personal(()),
        };
        const WPA3_PERSONAL: Self = wpa::WpaDescriptor::Wpa3 {
            cipher: None,
            authentication: wpa::Authentication::Personal(()),
        };
    }

    #[test_case(WpaCredentialsTestCase::psk() => matches Ok(wpa::Wpa1Credentials::Psk(_)))]
    #[test_case(WpaCredentialsTestCase::passphrase() => matches
        Ok(wpa::Wpa1Credentials::Passphrase(_))
    )]
    fn wpa1_credentials_from_credentials_fidl(
        credentials: fidl_security::WpaCredentials,
    ) -> Result<wpa::Wpa1Credentials, SecurityError> {
        credentials.try_into()
    }

    #[test_case(WpaCredentialsTestCase::psk() => matches Ok(wpa::Wpa2PersonalCredentials::Psk(_)))]
    #[test_case(WpaCredentialsTestCase::passphrase() => matches
        Ok(wpa::Wpa2PersonalCredentials::Passphrase(_))
    )]
    fn wpa2_personal_credentials_from_credentials_fidl(
        credentials: fidl_security::WpaCredentials,
    ) -> Result<wpa::Wpa2PersonalCredentials, SecurityError> {
        credentials.try_into()
    }

    #[test_case(WpaCredentialsTestCase::psk() => Err(SecurityError::Incompatible))]
    #[test_case(WpaCredentialsTestCase::passphrase() => matches
        Ok(wpa::Wpa3PersonalCredentials::Passphrase(_))
    )]
    fn wpa3_personal_credentials_from_credentials_fidl(
        credentials: fidl_security::WpaCredentials,
    ) -> Result<wpa::Wpa3PersonalCredentials, SecurityError> {
        credentials.try_into()
    }

    #[test_case(PersonalCredentialsTestCase::psk() => matches Ok(wpa::Wpa1Credentials::Psk(_)))]
    #[test_case(PersonalCredentialsTestCase::passphrase() => matches
        Ok(wpa::Wpa1Credentials::Passphrase(_))
    )]
    fn wpa1_personal_credentials_from_personal_credentials(
        credentials: wpa::PersonalCredentials,
    ) -> Result<wpa::Wpa1Credentials, SecurityError> {
        credentials.try_into()
    }

    #[test_case(PersonalCredentialsTestCase::psk() => Err(SecurityError::Incompatible))]
    #[test_case(PersonalCredentialsTestCase::passphrase() => matches
        Ok(wpa::Wpa3PersonalCredentials::Passphrase(_))
    )]
    fn wpa3_personal_credentials_from_personal_credentials(
        credentials: wpa::PersonalCredentials,
    ) -> Result<wpa::Wpa3PersonalCredentials, SecurityError> {
        credentials.try_into()
    }

    #[test_case(wpa::Cipher::TKIP => Ok(wpa::Wpa2Cipher::TKIP))]
    #[test_case(wpa::Cipher::CCMP => Ok(wpa::Wpa2Cipher::CCMP))]
    #[test_case(wpa::Cipher::GCMP => Err(SecurityError::Incompatible))]
    fn wpa2_cipher_from_cipher(cipher: wpa::Cipher) -> Result<wpa::Wpa2Cipher, SecurityError> {
        cipher.try_into()
    }

    #[test_case(wpa::Cipher::TKIP => Err(SecurityError::Incompatible))]
    #[test_case(wpa::Cipher::CCMP => Ok(wpa::Wpa3Cipher::CCMP))]
    #[test_case(wpa::Cipher::GCMP => Ok(wpa::Wpa3Cipher::GCMP))]
    fn wpa3_cipher_from_cipher(cipher: wpa::Cipher) -> Result<wpa::Wpa3Cipher, SecurityError> {
        cipher.try_into()
    }

    #[test_case(WpaDescriptorTestCase::WPA1, BareCredentialsTestCase::psk() =>
        Ok(wpa::WpaAuthenticator::Wpa1 {
            credentials: wpa::Wpa1Credentials::Psk(wpa_psk()),
        })
    )]
    #[test_case(WpaDescriptorTestCase::WPA2_PERSONAL, BareCredentialsTestCase::psk() =>
        Ok(wpa::WpaAuthenticator::Wpa2 {
            cipher: None,
            authentication: wpa::Authentication::Personal(
                wpa::Wpa2PersonalCredentials::Psk(wpa_psk())
            ),
        })
    )]
    #[test_case(WpaDescriptorTestCase::WPA3_PERSONAL, BareCredentialsTestCase::passphrase() =>
        Ok(wpa::WpaAuthenticator::Wpa3 {
            cipher: None,
            authentication: wpa::Authentication::Personal(
                wpa::Wpa3PersonalCredentials::Passphrase(wpa_passphrase())
            ),
        })
    )]
    #[test_case(WpaDescriptorTestCase::WPA2_PERSONAL, BareCredentialsTestCase::wep_key() =>
        Err(SecurityError::Incompatible)
    )]
    #[test_case(WpaDescriptorTestCase::WPA3_PERSONAL, BareCredentialsTestCase::psk() =>
        Err(SecurityError::Incompatible)
    )]
    fn wpa_bind_descriptor(
        descriptor: wpa::WpaDescriptor,
        credentials: BareCredentials,
    ) -> Result<wpa::WpaAuthenticator, SecurityError> {
        descriptor.bind(credentials)
    }
}
