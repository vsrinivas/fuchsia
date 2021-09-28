// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Wireless network security descriptors and authenticators.
//!
//! This module describes wireless network security protocols as well as credentials used to
//! authenticate using those protocols. Types in this module provide two important features:
//! conversions from and into FIDL types used within the WLAN stack and consistency. Here,
//! consistency means that types have no invalid values; given an instance of a type, it is always
//! valid. For example, a `SecurityAuthenticator` always represents a valid protocol-credential
//! pair.
//!
//! The `SecurityDescriptor` and `SecurityAuthenticator` types form the primary API of this module.
//! A _descriptor_ merely describes (names) a security protocol such as WPA2 Personal using TKIP.
//! An _authenticator_ describes a security protocol and additionally contains credentials used to
//! authenticate using that protocol such as WPA Personal using TKIP with a PSK credential.
//! Authenticators can be converted into descriptors (which drops any associated credentials).
//!
//! # FIDL
//!
//! Types in this module support conversion into and from datagrams in the
//! `fuchsia.wlan.common.security` package. When interacting with FIDL, most code should prefer
//! conversions between FIDL types and this module's `SecurityDescriptor` and
//! `SecurityAuthenticator` types, though conversions for intermediate types are also provided.
//!
//! The models used by this module and the FIDL package differ, so types do not always have a
//! direct analog, but the table below describes the most straightforward and important mappings.
//!
//! | Rust Type               | FIDL Type        | Rust to FIDL | FIDL to Rust |
//! |-------------------------|------------------|--------------|--------------|
//! | `SecurityDescriptor`    | `Protocol`       | `From`       | `From`       |
//! | `SecurityAuthenticator` | `Authentication` | `From`       | `TryFrom`    |
//!
//! # Cryptography and RSN
//!
//! This module does **not** implement any security protocols nor cryptography and only describes
//! them with limited detail as needed at API boundaries and in code that merely interacts with
//! these protocols. See the `rsn` crate for implementations of the IEEE Std 802.11-2016 Robust
//! Security Network (RSN).

// NOTE: At the time of this writing, the WLAN stack does not support WPA Enterprise. One
//       consequence of this is that conversions between Rust and FIDL types may return errors or
//       panic when they involve WPA Enterprise representations. See TODOs in this module.

pub mod wep;
pub mod wpa;

use fidl_fuchsia_wlan_common_security as fidl_security;
use std::convert::TryFrom;
use thiserror::Error;

/// Extension methods for the `Credentials` FIDL datagram.
pub trait CredentialsExt {
    fn into_wep(self) -> Option<fidl_security::WepCredentials>;
    fn into_wpa(self) -> Option<fidl_security::WpaCredentials>;
}

impl CredentialsExt for fidl_security::Credentials {
    fn into_wep(self) -> Option<fidl_security::WepCredentials> {
        if let fidl_security::Credentials::Wep(credentials) = self {
            Some(credentials)
        } else {
            None
        }
    }

    fn into_wpa(self) -> Option<fidl_security::WpaCredentials> {
        if let fidl_security::Credentials::Wpa(credentials) = self {
            Some(credentials)
        } else {
            None
        }
    }
}

#[derive(Clone, Copy, Debug, Error)]
#[non_exhaustive]
pub enum SecurityError {
    #[error(transparent)]
    Wep(wep::WepError),
    #[error(transparent)]
    Wpa(wpa::WpaError),
    #[error("incompatible protocol or features")]
    Incompatible,
}

impl From<wep::WepError> for SecurityError {
    fn from(error: wep::WepError) -> Self {
        SecurityError::Wep(error)
    }
}

impl From<wpa::WpaError> for SecurityError {
    fn from(error: wpa::WpaError) -> Self {
        SecurityError::Wpa(error)
    }
}

/// Description of a wireless network security protocol.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SecurityDescriptor {
    Open,
    Wep,
    Wpa(wpa::WpaDescriptor),
}

impl SecurityDescriptor {
    pub fn is_open(&self) -> bool {
        matches!(self, SecurityDescriptor::Open)
    }
}

impl From<fidl_security::Protocol> for SecurityDescriptor {
    fn from(protocol: fidl_security::Protocol) -> Self {
        match protocol {
            fidl_security::Protocol::Open => SecurityDescriptor::Open,
            fidl_security::Protocol::Wep => SecurityDescriptor::Wep,
            fidl_security::Protocol::Wpa1 => {
                SecurityDescriptor::Wpa(wpa::WpaDescriptor::Wpa1 { authentication: () })
            }
            fidl_security::Protocol::Wpa2Personal => {
                SecurityDescriptor::Wpa(wpa::WpaDescriptor::Wpa2 {
                    authentication: wpa::Authentication::Personal(()),
                    encryption: None,
                })
            }
            fidl_security::Protocol::Wpa2Enterprise => {
                SecurityDescriptor::Wpa(wpa::WpaDescriptor::Wpa2 {
                    authentication: wpa::Authentication::Enterprise(()),
                    encryption: None,
                })
            }
            fidl_security::Protocol::Wpa3Personal => {
                SecurityDescriptor::Wpa(wpa::WpaDescriptor::Wpa3 {
                    authentication: wpa::Authentication::Personal(()),
                    encryption: None,
                })
            }
            fidl_security::Protocol::Wpa3Enterprise => {
                SecurityDescriptor::Wpa(wpa::WpaDescriptor::Wpa3 {
                    authentication: wpa::Authentication::Enterprise(()),
                    encryption: None,
                })
            }
            _ => panic!("unknown FIDL security protocol variant"),
        }
    }
}

impl From<SecurityDescriptor> for fidl_security::Protocol {
    fn from(descriptor: SecurityDescriptor) -> Self {
        match descriptor {
            SecurityDescriptor::Open => fidl_security::Protocol::Open,
            SecurityDescriptor::Wep => fidl_security::Protocol::Wep,
            SecurityDescriptor::Wpa(wpa) => match wpa {
                wpa::WpaDescriptor::Wpa1 { .. } => fidl_security::Protocol::Wpa1,
                wpa::WpaDescriptor::Wpa2 { authentication, .. } => match authentication {
                    wpa::Authentication::Personal(_) => fidl_security::Protocol::Wpa2Personal,
                    wpa::Authentication::Enterprise(_) => fidl_security::Protocol::Wpa2Enterprise,
                },
                wpa::WpaDescriptor::Wpa3 { authentication, .. } => match authentication {
                    wpa::Authentication::Personal(_) => fidl_security::Protocol::Wpa3Personal,
                    wpa::Authentication::Enterprise(_) => fidl_security::Protocol::Wpa3Enterprise,
                },
            },
        }
    }
}

impl From<wpa::WpaDescriptor> for SecurityDescriptor {
    fn from(descriptor: wpa::WpaDescriptor) -> Self {
        SecurityDescriptor::Wpa(descriptor)
    }
}

/// Credentials and configuration for authenticating using a particular wireless network security
/// protocol.
#[derive(Clone, Debug)]
pub enum SecurityAuthenticator {
    Open,
    Wep(wep::WepAuthenticator),
    Wpa(wpa::WpaAuthenticator),
}

impl SecurityAuthenticator {
    /// Converts an authenticator into a descriptor with no payload (the unit type `()`). Any
    /// payload (i.e., credentials) are dropped.
    pub fn into_descriptor(self) -> SecurityDescriptor {
        match self {
            SecurityAuthenticator::Open => SecurityDescriptor::Open,
            SecurityAuthenticator::Wep(_) => SecurityDescriptor::Wep,
            SecurityAuthenticator::Wpa(authenticator) => {
                SecurityDescriptor::Wpa(authenticator.into_descriptor())
            }
        }
    }

    pub fn into_wep(self) -> Option<wep::WepAuthenticator> {
        match self {
            SecurityAuthenticator::Wep(authenticator) => Some(authenticator),
            _ => None,
        }
    }

    pub fn into_wpa(self) -> Option<wpa::WpaAuthenticator> {
        match self {
            SecurityAuthenticator::Wpa(authenticator) => Some(authenticator),
            _ => None,
        }
    }

    pub fn as_wep(&self) -> Option<&wep::WepAuthenticator> {
        match self {
            SecurityAuthenticator::Wep(ref authenticator) => Some(authenticator),
            _ => None,
        }
    }

    pub fn as_wpa(&self) -> Option<&wpa::WpaAuthenticator> {
        match self {
            SecurityAuthenticator::Wpa(ref authenticator) => Some(authenticator),
            _ => None,
        }
    }

    pub fn is_open(&self) -> bool {
        matches!(self, SecurityAuthenticator::Open)
    }
}

impl From<wep::WepAuthenticator> for SecurityAuthenticator {
    fn from(authenticator: wep::WepAuthenticator) -> Self {
        SecurityAuthenticator::Wep(authenticator)
    }
}

impl From<wpa::WpaAuthenticator> for SecurityAuthenticator {
    fn from(authenticator: wpa::WpaAuthenticator) -> Self {
        SecurityAuthenticator::Wpa(authenticator)
    }
}

impl From<SecurityAuthenticator> for fidl_security::Authentication {
    fn from(authenticator: SecurityAuthenticator) -> Self {
        match authenticator {
            SecurityAuthenticator::Open => fidl_security::Authentication {
                protocol: fidl_security::Protocol::Open,
                credentials: None,
            },
            SecurityAuthenticator::Wep(wep) => fidl_security::Authentication {
                protocol: fidl_security::Protocol::Wep,
                credentials: Some(Box::new(fidl_security::Credentials::Wep(wep.into()))),
            },
            SecurityAuthenticator::Wpa(wpa) => {
                use wpa::Authentication::{Enterprise, Personal};
                use wpa::Wpa::{Wpa1, Wpa2, Wpa3};

                let protocol = match (&wpa, wpa.as_credentials()) {
                    (Wpa1 { .. }, _) => fidl_security::Protocol::Wpa1,
                    (Wpa2 { .. }, Personal(_)) => fidl_security::Protocol::Wpa2Personal,
                    (Wpa2 { .. }, Enterprise(_)) => fidl_security::Protocol::Wpa2Enterprise,
                    (Wpa3 { .. }, Personal(_)) => fidl_security::Protocol::Wpa3Personal,
                    (Wpa3 { .. }, Enterprise(_)) => fidl_security::Protocol::Wpa3Enterprise,
                };
                fidl_security::Authentication {
                    protocol,
                    // TODO(seanolson): This panics when encountering WPA Enterprise.
                    credentials: Some(Box::new(fidl_security::Credentials::Wpa(
                        wpa.into_credentials().into(),
                    ))),
                }
            }
        }
    }
}

/// Converts an `Authentication` FIDL datagram into a `SecurityAuthenticator`.
///
/// This conversion should be preferred where possible.
///
/// # Errors
///
/// Returns an error if the `Authentication` datagram is invalid, such as specifying contradictory
/// protocols or encoding incompatible or invalid credentials.
impl TryFrom<fidl_security::Authentication> for SecurityAuthenticator {
    type Error = SecurityError;

    fn try_from(authentication: fidl_security::Authentication) -> Result<Self, Self::Error> {
        let fidl_security::Authentication { protocol, credentials } = authentication;
        match protocol {
            fidl_security::Protocol::Open => match credentials {
                None => Ok(SecurityAuthenticator::Open),
                _ => Err(SecurityError::Incompatible),
            },
            fidl_security::Protocol::Wep => credentials
                .ok_or_else(|| SecurityError::Incompatible)? // No credentials.
                .into_wep()
                .map(wep::WepAuthenticator::try_from)
                .transpose()? // Conversion failure.
                .map(From::from)
                .ok_or_else(|| SecurityError::Incompatible), // Non-WEP credentials.
            fidl_security::Protocol::Wpa1 => credentials
                .ok_or_else(|| SecurityError::Incompatible)? // No credentials.
                .into_wpa()
                .map(wpa::PersonalCredentials::try_from)
                .transpose()? // Conversion failure.
                .map(|authentication| wpa::WpaAuthenticator::Wpa1 { authentication })
                .map(From::from)
                .ok_or_else(|| SecurityError::Incompatible), // Non-WPA credentials.
            fidl_security::Protocol::Wpa2Personal => credentials
                .ok_or_else(|| SecurityError::Incompatible)? // No credentials.
                .into_wpa()
                .map(wpa::PersonalCredentials::try_from)
                .transpose()? // Conversion failure.
                .map(From::from)
                .map(|authentication| wpa::WpaAuthenticator::Wpa2 {
                    encryption: None,
                    authentication,
                })
                .map(From::from)
                .ok_or_else(|| SecurityError::Incompatible), // Non-WPA credentials.
            fidl_security::Protocol::Wpa3Personal => credentials
                .ok_or_else(|| SecurityError::Incompatible)? // No credentials.
                .into_wpa()
                .map(wpa::PersonalCredentials::try_from)
                .transpose()? // Conversion failure.
                .map(From::from)
                .map(|authentication| wpa::WpaAuthenticator::Wpa3 {
                    encryption: None,
                    authentication,
                })
                .map(From::from)
                .ok_or_else(|| SecurityError::Incompatible), // Non-WPA credentials.
            // TODO(seanolson): This returns an error when encountering WPA Enterprise protocols.
            _ => Err(SecurityError::Incompatible),
        }
    }
}

#[cfg(test)]
mod tests {
    use fidl_fuchsia_wlan_common_security as fidl_security;
    use std::convert::TryFrom;

    use crate::security::{wpa, SecurityAuthenticator, SecurityError};

    #[test]
    fn security_authenticator_from_authentication_fidl() {
        // Well formed WPA3 authentication.
        let authentication = fidl_security::Authentication {
            protocol: fidl_security::Protocol::Wpa3Personal,
            credentials: Some(Box::new(fidl_security::Credentials::Wpa(
                fidl_security::WpaCredentials::Psk([0u8; 32]),
            ))),
        };
        let authenticator = SecurityAuthenticator::try_from(authentication).unwrap();
        assert!(matches!(authenticator.as_wpa(), Some(wpa::WpaAuthenticator::Wpa3 { .. })));
        assert_eq!(
            authenticator.into_wpa().unwrap().into_credentials().into_personal().unwrap(),
            wpa::PersonalCredentials::Psk(wpa::Psk::from([0u8; 32]))
        );

        // Ill-formed WPA3 authentication (incompatible credentials).
        let authentication = fidl_security::Authentication {
            protocol: fidl_security::Protocol::Wpa3Personal,
            credentials: Some(Box::new(fidl_security::Credentials::Wep(
                fidl_security::WepCredentials { key: vec![0u8; 13] },
            ))),
        };
        assert!(matches!(
            SecurityAuthenticator::try_from(authentication),
            Err(SecurityError::Incompatible)
        ));

        // Ill-formed WPA3 authentication (no credentials).
        let authentication = fidl_security::Authentication {
            protocol: fidl_security::Protocol::Wpa3Personal,
            credentials: None,
        };
        assert!(matches!(
            SecurityAuthenticator::try_from(authentication),
            Err(SecurityError::Incompatible)
        ));
    }

    #[test]
    fn authentication_fidl_from_security_authenticator() {
        let authenticator = SecurityAuthenticator::Wpa(wpa::WpaAuthenticator::Wpa3 {
            authentication: wpa::Credentials::Personal(wpa::PersonalCredentials::Psk(
                wpa::Psk::from([0u8; 32]),
            )),
            encryption: None,
        });
        let authentication = fidl_security::Authentication::from(authenticator);
        assert_eq!(
            authentication,
            fidl_security::Authentication {
                protocol: fidl_security::Protocol::Wpa3Personal,
                credentials: Some(Box::new(fidl_security::Credentials::Wpa(
                    fidl_security::WpaCredentials::Psk([0u8; 32]),
                ))),
            }
        );
    }
}
