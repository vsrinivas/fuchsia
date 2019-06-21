// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde_derive::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Clone, Copy, PartialEq, Eq)]
#[cfg_attr(test, derive(Hash))]
pub enum AvailableTargets {
    FileTarget,
}

impl AvailableTargets {
    pub fn friendly_names() -> Vec<&'static str> {
        vec![
            "file", // for FileTarget,
        ]
    }

    pub fn friendly_name_to_value(
        name: &str,
    ) -> std::result::Result<AvailableTargets, &'static str> {
        match name {
            "file" => Ok(AvailableTargets::FileTarget),
            _ => Err("invalid target type"),
        }
    }

    pub fn value_to_friendly_name(value: AvailableTargets) -> &'static str {
        match value {
            AvailableTargets::FileTarget => "file",
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::target::AvailableTargets;
    use std::collections::HashSet;

    #[test]
    fn friendly_names_count() {
        // Reminds to add more test when number of target types change
        assert_eq!(AvailableTargets::friendly_names().len(), 1);
    }

    #[test]
    fn friendly_names_get_all() {
        // Reminds to add more test when number of target types change
        assert_eq!(AvailableTargets::friendly_names(), vec!["file"]);
    }

    #[test]
    fn all_names_have_unique_values() {
        let names = AvailableTargets::friendly_names();
        let mut values = HashSet::new();

        for name in names.iter() {
            let value = AvailableTargets::friendly_name_to_value(name).unwrap();
            assert_eq!(values.contains(&value), false);
            values.insert(value);
        }
    }

    #[test]
    fn friendly_names_to_value_valid_name() {
        assert_eq!(
            AvailableTargets::friendly_name_to_value("file").unwrap(),
            AvailableTargets::FileTarget
        );
    }

    #[test]
    fn friendly_names_to_value_invalid_name() {
        assert_eq!(AvailableTargets::friendly_name_to_value("hello").is_err(), true);
    }

    #[test]
    fn friendly_names_to_value_null_name() {
        assert_eq!(AvailableTargets::friendly_name_to_value("").is_err(), true);
    }

    #[test]
    fn value_to_friendly_name() {
        assert_eq!(AvailableTargets::value_to_friendly_name(AvailableTargets::FileTarget), "file");
    }

}
