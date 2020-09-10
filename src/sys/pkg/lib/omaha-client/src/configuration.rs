// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
}

#[cfg(test)]
pub mod test_support {

    use super::*;

    /// Handy generator for an updater configuration.  Used to reduce test boilerplate.
    pub fn config_generator() -> Config {
        Config {
            updater: Updater { name: "updater".to_string(), version: Version::from([1, 2, 3, 4]) },
            os: OS {
                platform: "platform".to_string(),
                version: "0.1.2.3".to_string(),
                service_pack: "sp".to_string(),
                arch: "test_arch".to_string(),
            },
            service_url: "http://example.com/".to_string(),
        }
    }
}
