// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{convert::Infallible, ffi::OsStr, path::Path, str::FromStr};

/// A `FileType` represents a type of file. The type is determined based on
/// the file extension of the string with which the `FileType` is constructed.
#[derive(PartialEq, Debug)]
pub enum FileType {
    Cpp,
    Fidl,
    Gn,
    Rust,
    Unknown(String),
}

impl FileType {
    /// Returns whether the file type is supported by `affected_targets`.
    pub fn supported(&self) -> bool {
        match self {
            FileType::Cpp | FileType::Fidl | FileType::Gn | FileType::Rust => true,
            _ => false,
        }
    }
}

impl FromStr for FileType {
    type Err = Infallible;

    fn from_str(file_string: &str) -> Result<Self, Self::Err> {
        let path = Path::new(file_string);
        if let Some(filename) = path.file_name() {
            if filename == "BUILD.gn" {
                return Ok(FileType::Gn);
            }
        }
        let file_type = match &path.extension().and_then(OsStr::to_str) {
            Some(extension) => match *extension {
                "c" | "cc" | "cpp" => FileType::Cpp,
                "fidl" => FileType::Fidl,
                "gni" => FileType::Gn,
                "rs" => FileType::Rust,
                ext => FileType::Unknown(ext.to_string()),
            },
            None => FileType::Unknown("".to_string()),
        };
        Ok(file_type)
    }
}

pub fn contains_disabled_file_types<I, S>(files: I, disabled_file_types: Vec<FileType>) -> bool
where
    I: IntoIterator<Item = S>,
    S: AsRef<str>,
{
    files
        .into_iter()
        .map(|file| FileType::from_str(file.as_ref()).unwrap())
        .any(|f| disabled_file_types.contains(&f))
}

/// Returns true if all `files` are of a type supported by `affected_targets`.
///
/// If this returns true, it is safe to continue analyzing the build, and potentially
/// short-circuit the builder. If it's false, the tool does not support the analysis
/// of the provided files, and can't short-circuit the builder.
pub fn file_types_are_supported<I, S>(files: I) -> bool
where
    I: IntoIterator<Item = S>,
    S: AsRef<str>,
{
    files.into_iter().map(|file| FileType::from_str(file.as_ref()).unwrap()).all(|f| f.supported())
}

#[cfg(test)]
mod tests {
    use {super::*, matches::assert_matches};

    #[test]
    fn cpp_files_parsed_correctly() {
        // Headers are not currently supported.
        assert_matches!(FileType::from_str("this/test.h"), Ok(FileType::Unknown(_)));

        assert_matches!(FileType::from_str("test.c"), Ok(FileType::Cpp));
        assert_matches!(FileType::from_str("test.cc"), Ok(FileType::Cpp));
        assert_matches!(FileType::from_str("test.cpp"), Ok(FileType::Cpp));
    }

    #[test]
    fn fidl_files_parsed_correctly() {
        assert_matches!(
            FileType::from_str("//sdk/fidl/fuchsia.sample/protocol.fidl"),
            Ok(FileType::Fidl)
        );
    }

    #[test]
    fn gn_files_parsed_correctly() {
        assert_matches!(FileType::from_str("//foo/bar/BUILD.gn"), Ok(FileType::Gn));
        assert_matches!(FileType::from_str("//build/rust/rustc_binary.gni"), Ok(FileType::Gn));
        assert_matches!(FileType::from_str("//foo/barBUILD.gn"), Ok(FileType::Unknown(_)));
        assert_matches!(FileType::from_str("//foo/bar.gn"), Ok(FileType::Unknown(_)));
    }

    #[test]
    fn rust_files_parsed_correctly() {
        assert_matches!(FileType::from_str("//this/test.rs"), Ok(FileType::Rust));
    }

    #[test]
    fn no_extension_parsed_correctly() {
        assert_matches!(FileType::from_str("//this/test"), Ok(FileType::Unknown(ext)) if ext == "");
    }

    #[test]
    fn unrecognized_extension_parsed_correctly() {
        assert_matches!(FileType::from_str("//test.foo"), Ok(FileType::Unknown(ext)) if ext == "foo");
    }

    #[test]
    fn test_file_types_are_supported() {
        assert!(file_types_are_supported(&["BUILD.gn", "test.fidl", "test.rs"]));
        assert!(!file_types_are_supported(&[
            "BUILD.gn",
            "test.fidl",
            "test.unsupported",
            "test.rs"
        ]));
        assert!(!file_types_are_supported(&["BUILD.gn", "test.fidl", "no_extension"]));
    }

    #[test]
    fn test_disabled_file_types() {
        assert!(contains_disabled_file_types(
            &["test.cc", "BUILD.gn", "test.fidl"],
            vec![FileType::Cpp]
        ));
        assert!(!contains_disabled_file_types(
            &["test.gn", "BUILD.fidl", "test.rs"],
            vec![FileType::Cpp]
        ));
    }
}
