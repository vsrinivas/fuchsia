// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Extracts the fields of a GN label.  GN labels are meant to be opaque in
//! output, and GN itself provides helper functions that does the same tasks
//! that are here.

use std::{
    convert::{TryFrom, TryInto},
    fmt::Display,
    str::FromStr,
};
use thiserror::Error;

// The TargetLabel is capable of representing a complete "canonical" GN label.
//
// Use its methods to access it's various representations.
//
#[derive(Debug, Clone, PartialEq)]
pub struct TargetLabel {
    label: Label,
    toolchain: Label,
}

impl TargetLabel {
    /// Equivalent to GN's `get_label_info(label, "name")`
    pub fn name(&self) -> &str {
        &self.label.name
    }

    /// Equivalent to GN's `get_label_info(label, "dir")`
    pub fn dir(&self) -> &str {
        &self.label.path
    }

    pub fn label_no_toolchain(&self) -> String {
        self.label.to_string()
    }

    pub fn label_with_toolchain(&self) -> String {
        // defer to Display impl
        self.to_string()
    }
}

impl Display for TargetLabel {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_fmt(format_args!("{}({})", self.label, self.toolchain))
    }
}

#[derive(Error, Debug)]
pub enum LabelParseErrors {
    #[error("A label without toolchain cannot have a toolchain: {0}")]
    LabelHasToolchainWhenItShouldNot(String),

    #[error("Label is missing a path: {0}")]
    PathMissing(String),

    #[error("Label is missing a name separator: {0}")]
    NameSeparatorMissing(String),

    #[error("Label is missing a name: {0}")]
    NameMissing(String),

    #[error("No closing parens on toolchain: {0}")]
    InvalidToolchain(String),

    #[error("Label doesn't have a toolchain: {0}")]
    TargetLabelMissingToolchain(String),
}

impl FromStr for TargetLabel {
    type Err = LabelParseErrors;

    /// Parses the string as a canonical, fully-qualified, label, returning an
    /// error, otherwise.
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        if let Some(string) = s.strip_suffix(")") {
            if let Some((label, toolchain)) = string.split_once("(") {
                let label = label.try_into()?;
                let toolchain = toolchain.try_into()?;
                Ok(Self { label, toolchain })
            } else {
                Err(LabelParseErrors::InvalidToolchain(s.to_owned()))
            }
        } else {
            Err(LabelParseErrors::TargetLabelMissingToolchain(s.to_owned()))
        }
    }
}

impl TryFrom<&str> for TargetLabel {
    type Error = LabelParseErrors;

    fn try_from(s: &str) -> Result<Self, Self::Error> {
        s.parse()
    }
}

#[derive(Debug, Clone, PartialEq)]
struct Label {
    path: String,
    name: String,
}

impl TryFrom<&str> for Label {
    type Error = LabelParseErrors;

    fn try_from(string: &str) -> Result<Self, Self::Error> {
        if string.contains("(") {
            return Err(LabelParseErrors::LabelHasToolchainWhenItShouldNot(string.to_owned()));
        }
        if let Some((path, name)) = string.split_once(":") {
            if path.len() == 0 {
                Err(LabelParseErrors::PathMissing(string.to_owned()))
            } else if name.len() == 0 {
                Err(LabelParseErrors::NameMissing(string.to_owned()))
            } else {
                Ok(Self { path: path.to_string(), name: name.to_string() })
            }
        } else {
            Err(LabelParseErrors::NameSeparatorMissing(string.to_owned()))
        }
    }
}

impl std::fmt::Display for Label {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}:{}", self.path, self.name)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_label_display() {
        let label = Label { path: "//some/path".into(), name: "name".into() };
        assert_eq!(format!("{}", label), "//some/path:name");
    }

    #[test]
    fn test_targetlabel_display() {
        let target_label = TargetLabel {
            label: Label { path: "//some/other/path".into(), name: "name".into() },
            toolchain: Label { path: "//toolchain/path".into(), name: "toolchain_name".into() },
        };
        assert_eq!(
            format!("{}", target_label),
            "//some/other/path:name(//toolchain/path:toolchain_name)"
        );
        assert_eq!(
            target_label.label_with_toolchain(),
            "//some/other/path:name(//toolchain/path:toolchain_name)"
        );
        assert_eq!(target_label.label_no_toolchain(), "//some/other/path:name")
    }

    #[test]
    fn test_valid_canonical_label() {
        let string = "//path/to/file:name";
        let label: Label = string.try_into().unwrap();
        assert_eq!(label.path, "//path/to/file");
        assert_eq!(label.name, "name");
    }

    #[test]
    fn test_valid_canonical_target_label() {
        let string = "//path/to/file:name(//diff/path:toolchain)";
        let label: TargetLabel = string.try_into().unwrap();
        assert_eq!(label.dir(), "//path/to/file");
        assert_eq!(label.name(), "name");
        assert_eq!(&label.label_no_toolchain(), "//path/to/file:name");
        assert_eq!(&label.label_with_toolchain(), string);
    }

    #[test]
    fn test_reject_missing_paren() {
        let string = "//path/to/file:name(//diff/path:toolchain";
        let result = TargetLabel::try_from(string);
        assert!(result.is_err());
    }

    #[test]
    fn test_reject_missing_toolchain() {
        let string = "//path/to/file:name";
        let result = TargetLabel::try_from(string);
        assert!(result.is_err());
    }

    #[test]
    fn test_reject_missing_label_name() {
        let string = "//path/to/file(//diff/path:toolchain)";
        let result = TargetLabel::try_from(string);
        assert!(result.is_err());
    }
    #[test]
    fn test_reject_missing_label_dir() {
        let string = ":file(//diff/path:toolchain)";
        let result = TargetLabel::try_from(string);
        assert!(result.is_err());
    }
    #[test]
    fn test_reject_missing_toolchain_name() {
        let string = "//path/to/file:name(//diff/path)";
        let result = TargetLabel::try_from(string);
        assert!(result.is_err());
    }
    #[test]
    fn test_reject_missing_toolchain_dir() {
        let string = "//path/to/file:name(toolchain)";
        let result = TargetLabel::try_from(string);
        assert!(result.is_err());
    }
    #[test]
    fn test_reject_nested_toolchain() {
        let string = "//path/to/file:name(//diff/path:toolchain(//diff/path:toolchain))";
        let result = TargetLabel::try_from(string);
        assert!(result.is_err());
    }
}
