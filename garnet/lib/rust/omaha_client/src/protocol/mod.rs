// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde_derive::{Deserialize, Serialize};

pub mod request;
pub mod response;

pub const PROTOCOL_V3: &str = "3.0";

/// The cohort identifies the update 'track' or 'channel', and is used to implement the tracking of
/// membership in a fractional roll-out.  This is per-application data.
///
/// This is sent to Omaha to identify the cohort that the application is in.  This is returned (with
/// possibly new values) by Omaha to indicate that the application is now in a different cohort.  On
/// the next update check for that application, the updater needs to use this newly returned cohort
/// as the one that it sends to Omaha with that application.
///
/// For more information about cohorts, see the 'cohort', 'cohorthint', and 'cohortname' attributes
/// of the Request.App object at:
///
/// https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#app-request
#[derive(Clone, Debug, Default, Deserialize, Eq, PartialEq, Serialize)]
pub struct Cohort {
    /// This is the cohort id itself.
    #[serde(rename = "cohort")]
    #[serde(skip_serializing_if = "Option::is_none")]
    pub id: Option<String>,

    #[serde(rename = "cohorthint")]
    #[serde(skip_serializing_if = "Option::is_none")]
    pub hint: Option<String>,

    #[serde(rename = "cohortname")]
    #[serde(skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
}

impl Cohort {
    /// Create a new Cohort instance from just a cohort id (channel name).
    pub fn new(id: &str) -> Cohort {
        Cohort { id: Some(id.to_string()), hint: None, name: None }
    }

    pub fn from_hint(hint: &str) -> Cohort {
        Cohort { id: None, hint: Some(hint.to_string()), name: None }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    pub fn test_cohort_new() {
        let cohort = Cohort::new("my_cohort");
        assert_eq!(Some("my_cohort".to_string()), cohort.id);
        assert_eq!(None, cohort.hint);
        assert_eq!(None, cohort.name);
    }

}
