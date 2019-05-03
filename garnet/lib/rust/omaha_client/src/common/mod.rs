// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The omaha_client::common module contains those types that are common to many parts of the
//! library.  Many of these don't belong to a specific sub-module.

use crate::protocol::request::InstallSource;
use itertools::Itertools;
use std::fmt;
use std::str::FromStr;
use std::time::SystemTime;

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

/// Omaha only supports versions in the form of A.B.C.D, A.B.C, A.B or A.  This is a utility
/// wrapper around that form of version.
#[derive(Clone, Eq, Ord, PartialEq, PartialOrd)]
pub struct Version(pub Vec<u32>);

impl fmt::Display for Version {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}", self.0.iter().format("."))
    }
}

impl fmt::Debug for Version {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        // The Debug trait just forwards to the Display trait implementation for this type
        fmt::Display::fmt(self, f)
    }
}

#[derive(Debug, failure::Fail)]
struct TooManyNumbersError;

impl fmt::Display for TooManyNumbersError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str("Too many numbers in version, the maximum is 4.")
    }
}

impl FromStr for Version {
    type Err = failure::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let nums = s.split('.').map(|s| s.parse::<u32>()).collect::<Result<Vec<u32>, _>>()?;
        if nums.len() > 4 {
            return Err(TooManyNumbersError.into());
        }
        Ok(Version(nums))
    }
}

impl From<Vec<u32>> for Version {
    fn from(v: Vec<u32>) -> Self {
        Version(v)
    }
}

macro_rules! impl_from {
    ($($t:ty),+) => {
        $(
            impl From<$t> for Version {
                fn from(v: $t) -> Self {
                    Version(v.to_vec())
                }
            }
        )+
    }
}
impl_from!(&[u32], [u32; 1], [u32; 2], [u32; 3], [u32; 4]);

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

impl App {
    /// Construct an App from an ID and version, from anything that can be converted into a String
    /// and a Version.
    pub fn new<I: Into<String>, V: Into<Version>>(id: I, version: V) -> Self {
        App { id: id.into(), version: version.into(), fingerprint: None }
    }

    /// Construct an App from an ID, version, and fingerprint.  From anything that can be converted
    /// into a String and a Version.
    pub fn with_fingerprint<I: Into<String>, V: Into<Version>, F: Into<String>>(
        id: I,
        version: V,
        fingerprint: F,
    ) -> Self {
        App { id: id.into(), version: version.into(), fingerprint: Some(fingerprint.into()) }
    }
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
        let version = Version::from([1, 2, 3, 4]);
        assert_eq!("1.2.3.4", version.to_string());

        let version = Version::from([0, 6, 4, 7]);
        assert_eq!("0.6.4.7", version.to_string());
    }

    #[test]
    pub fn test_version_debug() {
        let version = Version::from([1, 2, 3, 4]);
        assert_eq!("1.2.3.4", format!("{:?}", version));

        let version = Version::from([0, 6, 4, 7]);
        assert_eq!("0.6.4.7", format!("{:?}", version));
    }

    #[test]
    pub fn test_version_parse() {
        let version = Version::from([1, 2, 3, 4]);
        assert_eq!("1.2.3.4".parse::<Version>().unwrap(), version);

        let version = Version::from([6, 4, 7]);
        assert_eq!("6.4.7".parse::<Version>().unwrap(), version);

        let version = Version::from([999]);
        assert_eq!("999".parse::<Version>().unwrap(), version);
    }

    #[test]
    pub fn test_version_parse_error() {
        assert!("1.2.3.4.5".parse::<Version>().is_err());
        assert!("1.2.".parse::<Version>().is_err());
        assert!(".1.2".parse::<Version>().is_err());
        assert!("-1".parse::<Version>().is_err());
        assert!("abc".parse::<Version>().is_err());
        assert!(".".parse::<Version>().is_err());
        assert!("".parse::<Version>().is_err());
        assert!("999999999999999999999999".parse::<Version>().is_err());
    }

    #[test]
    pub fn test_version_compare() {
        assert!(Version::from([1, 2, 3, 4]) < Version::from([2, 0, 3]));
        assert!(Version::from([1, 2, 3]) < Version::from([1, 2, 3, 4]));
    }

    #[test]
    pub fn test_app_new_version() {
        let app = App::new("some_id", [1, 2]);
        assert_eq!(app.id, "some_id");
        assert_eq!(app.version, [1, 2].into());
        assert_eq!(app.fingerprint, None);
    }

    #[test]
    pub fn test_app_with_fingerprint() {
        let app = App::with_fingerprint("some_id_2", [4, 6], "some_fp");
        assert_eq!(app.id, "some_id_2");
        assert_eq!(app.version, [4, 6].into());
        assert_eq!(app.fingerprint, Some("some_fp".to_string()));
    }
}
