// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use cml::error::Error;
use std::{fmt, str::FromStr};

/// Represents the set of features a CML file is compiled with. This struct can be
/// used to check whether a feature used in CML is enabled.
#[derive(Debug)]
pub struct FeatureSet(Vec<Feature>);

impl FeatureSet {
    /// Create an empty FeatureSet.
    pub fn empty() -> FeatureSet {
        FeatureSet(Vec::new())
    }

    /// Tests whether `feature` is enabled.
    pub fn has(&self, feature: &Feature) -> bool {
        self.0.iter().find(|f| *f == feature).is_some()
    }

    /// Returns an `Err` if `feature` is not enabled.
    pub fn check(&self, feature: Feature) -> Result<(), Error> {
        if self.has(&feature) {
            Ok(())
        } else {
            Err(Error::RestrictedFeature(feature.to_string()))
        }
    }
}

impl From<Vec<Feature>> for FeatureSet {
    fn from(features: Vec<Feature>) -> FeatureSet {
        FeatureSet(features)
    }
}

/// A feature that can be enabled/opt-into.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Feature {
    /// Allows `hub` framework capability to be used.
    Hub,

    // Allows dynamic child name lengths to exceed the default limit.
    AllowLongNames,
}

impl FromStr for Feature {
    type Err = String;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "hub" => Ok(Feature::Hub),
            "allow_long_names" => Ok(Feature::AllowLongNames),
            _ => Err(format!("unrecognized feature \"{}\"", s)),
        }
    }
}

impl fmt::Display for Feature {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            Feature::Hub => "hub",
            Feature::AllowLongNames => "allow_long_names",
        })
    }
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches};

    #[test]
    fn feature_is_parsed() {
        assert_eq!(Feature::Hub, "hub".parse::<Feature>().unwrap());
        assert_eq!(Feature::AllowLongNames, "allow_long_names".parse::<Feature>().unwrap());
    }

    #[test]
    fn feature_is_printed() {
        assert_eq!("hub", Feature::Hub.to_string());
        assert_eq!("allow_long_names", Feature::AllowLongNames.to_string());
    }

    #[test]
    fn feature_set_has() {
        let set = FeatureSet::empty();
        assert!(!set.has(&Feature::Hub));

        let set = FeatureSet::from(vec![Feature::Hub]);
        assert!(set.has(&Feature::Hub));
    }

    #[test]
    fn feature_set_check() {
        let set = FeatureSet::empty();
        assert_matches!(set.check(Feature::Hub), Err(Error::RestrictedFeature(f)) if f == "hub");

        let set = FeatureSet::from(vec![Feature::Hub]);
        assert_matches!(set.check(Feature::Hub), Ok(()));
    }
}
