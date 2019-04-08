// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::protocol::request::InstallSource;
use std::fmt;
use std::time::SystemTime;

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

/// Options controlling a single update check
#[derive(Clone, Debug)]
pub struct CheckOptions {
    /// Was this check initiated by a person that's waiting for an answer?
    ///  This is used to ignore the background poll rate, and to be aggressive about
    ///  failing fast, so as not to hang on not receiving a response.
    pub source: InstallSource,
}

/// This describes the data around the scheduling of update checks
#[derive(Clone, Debug, PartialEq)]
pub struct UpdateCheckSchedule {
    /// When the last update check was attempted (start time of the check process).
    pub last_update_time: SystemTime,

    /// When the next periodic update window starts.
    pub next_update_window_start: SystemTime,

    /// When the update should happen (in the update window).
    pub next_update_time: SystemTime,
}

/// These hold the data maintained request-to-request so that the requirements for
/// backoffs, throttling, proxy use, etc. can all be properly maintained.  This is
/// NOT the state machine's internal state.
#[derive(Clone, Debug, Default)]
pub struct ProtocolState {
    /// If the server has dictated the next poll interval, this holds what that
    /// interval is.
    pub server_dictated_poll_interval: Option<std::time::Duration>,

    /// The number of consecutive failed update attempts.
    pub consecutive_failed_update_attempts: u32,

    /// The number of consecutive failed update checks.  Used to perform backoffs.
    pub consecutive_failed_update_checks: u32,

    /// The number of consecutive proxied requests.  Used to periodically not use
    /// proxies, in the case of an invalid proxy configuration.
    pub consecutive_proxied_requests: u32,
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
