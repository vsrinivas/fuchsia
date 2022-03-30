// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::cup_ecdsa::PublicKeys;
use crate::protocol::request::OS;
use version::Version;

/// This is the name and version of the updater binary that is built using this crate.
///
/// This is how the updater identifies itself with the Omaha service.
///
#[derive(Clone, Debug)]
pub struct Updater {
    /// The string identifying the updater itself.  (e.g. 'Omaha', 'Fuchsia/Rust')
    pub name: String,

    /// The version of the updater itself.  (e.g '0.0.1.0')
    pub version: Version,
}

/// This struct wraps up the configuration data that an updater binary needs to supply.
///
#[derive(Clone, Debug)]
pub struct Config {
    pub updater: Updater,

    pub os: OS,

    /// This is the address of the Omaha service that should be used.
    pub service_url: String,

    /// These are the public keys to use when communicating with the Omaha server.
    pub omaha_public_keys: Option<PublicKeys>,
}

#[cfg(test)]
pub mod test_support {

    use super::*;
    use crate::cup_ecdsa::{PublicKeyAndId, PublicKeys};
    use p256::ecdsa::{SigningKey, VerifyingKey};
    use signature::rand_core::OsRng;
    use std::convert::TryInto;

    /// Handy generator for an updater configuration.  Used to reduce test boilerplate.
    pub fn config_generator() -> Config {
        let signing_key = SigningKey::random(&mut OsRng);
        let omaha_public_keys = PublicKeys {
            latest: PublicKeyAndId {
                id: 42.try_into().unwrap(),
                key: VerifyingKey::from(&signing_key),
            },
            historical: vec![],
        };

        Config {
            updater: Updater { name: "updater".to_string(), version: Version::from([1, 2, 3, 4]) },
            os: OS {
                platform: "platform".to_string(),
                version: "0.1.2.3".to_string(),
                service_pack: "sp".to_string(),
                arch: "test_arch".to_string(),
            },
            service_url: "http://example.com/".to_string(),
            omaha_public_keys: Some(omaha_public_keys),
        }
    }
}
