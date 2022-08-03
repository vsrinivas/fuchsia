// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

/// This struct provides information about the "board" that a product is being
/// assembled to run on.
#[derive(Debug, Deserialize, Serialize, PartialEq)]
pub struct BoardInformation {
    /// The name of the board.
    pub name: String,

    /// The "features" that this board provides to the product.
    ///
    /// NOTE: This is a still-evolving, loosely-coupled, set of identifiers.
    /// It's an unstable interface between the boards and the platform.
    #[serde(default)]
    pub provided_features: Vec<String>,
}

pub trait BoardInformationExt {
    /// Returns whether or not this board provides the named feature.
    fn provides_feature(&self, name: impl AsRef<str>) -> bool;
}

impl BoardInformationExt for BoardInformation {
    /// Returns whether or not this board provides the named feature.
    fn provides_feature(&self, name: impl AsRef<str>) -> bool {
        // .contains(&str) doesn't work for Vec<String>, so it's neccessary
        // to use .iter().any(...) instead.
        let name = name.as_ref();
        self.provided_features.iter().any(|f| f == name)
    }
}

impl BoardInformationExt for Option<&BoardInformation> {
    fn provides_feature(&self, name: impl AsRef<str>) -> bool {
        match self {
            Some(board_info) => board_info.provides_feature(name),
            _ => false,
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_basic_board_deserialize() {
        let json = serde_json::json!({
            "name": "sample board",
        });

        let parsed: BoardInformation = serde_json::from_value(json).unwrap();
        let expected =
            BoardInformation { name: "sample board".to_owned(), provided_features: Vec::default() };

        assert_eq!(parsed, expected);
    }

    #[test]
    fn test_complete_board_deserialize() {
        let json = serde_json::json!({
            "name": "sample board",
            "provided_features": [
                "feature_a",
                "feature_b"
            ]
        });

        let parsed: BoardInformation = serde_json::from_value(json).unwrap();
        let expected = BoardInformation {
            name: "sample board".to_owned(),
            provided_features: vec!["feature_a".into(), "feature_b".into()],
        };

        assert_eq!(parsed, expected);
    }

    #[test]
    fn test_provides_feature() {
        let board_info = BoardInformation {
            name: "sample".to_owned(),
            provided_features: vec!["feature_a".into(), "feature_b".into()],
        };

        assert!(board_info.provides_feature("feature_a"));
        assert!(board_info.provides_feature("feature_b"));
        assert!(!board_info.provides_feature("feature_c"));
    }
}
