// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;

/// The omaha_client::common module contains those types that are common to many parts of the
/// library.  Many of these don't belong to a specific sub-module.

/// Omaha has historically supported multiple methods of counting devices.  Currently, the
/// only recommended method is the Client Regulated - Date method.
///
/// See https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#client-regulated-counting-date-based
#[derive(Clone, Debug)]
pub enum UserCounting {
    ClientRegulatedByDate(
        /// Date (sent by the server) of the last contact with Omaha.
        Option<i32>,
    ),
}

/// Omaha only supports versions in the form of A.B.C.D.  This is a utility wrapper around that form
/// of version.
#[derive(Clone)]
pub struct Version(pub [u32; 4]);

impl fmt::Display for Version {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}.{}.{}.{}", self.0[0], self.0[1], self.0[2], self.0[3])
    }
}

impl fmt::Debug for Version {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        // The Debug trait just forwards to the Display trait implementation for this type
        fmt::Display::fmt(self, f)
    }
}

/// The App struct holds information about an application to perform an update check for.
#[derive(Clone, Debug)]
pub struct App {
    /// This is the app_id that Omaha uses to identify a given application.
    pub id: String,

    /// This is the current version of the application.
    pub version: Version,

    /// This is the fingerprint for the application package.
    ///
    /// See https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#packages--fingerprints
    pub fingerprint: Option<String>,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    pub fn test_version_display() {
        let version = Version([1, 2, 3, 4]);
        assert_eq!("1.2.3.4", version.to_string());

        let version = Version([0, 6, 4, 7]);
        assert_eq!("0.6.4.7", version.to_string());
    }

    #[test]
    pub fn test_version_debug() {
        let version = Version([1, 2, 3, 4]);
        assert_eq!("1.2.3.4", format!("{:?}", version));

        let version = Version([0, 6, 4, 7]);
        assert_eq!("0.6.4.7", format!("{:?}", version));
    }
}
