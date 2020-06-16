// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    std::{ffi::OsStr, path::Path, str::FromStr},
};

/// A `FileType` represents a type of file. The type is determined based on
/// the file extension of the string with which the `FileType` is constructed.
#[derive(PartialEq, Debug)]
pub enum FileType {
    Cpp,
    Unknown(String),
}

impl FromStr for FileType {
    type Err = Error;

    fn from_str(file_string: &str) -> Result<Self, Self::Err> {
        let file_type = match &Path::new(file_string).extension().and_then(OsStr::to_str) {
            Some("h") | Some("cc") => FileType::Cpp,
            Some(extension) => FileType::Unknown(extension.to_string()),
            None => FileType::Unknown("".to_string()),
        };
        Ok(file_type)
    }
}

/// Returns `true` iff all `files` are of the same type, as determined by their extension.
///
/// For example, if all files have either a ".h" or ".cc" file extension, they are all
/// considered to be `FileType::Cpp` and `are_all_files_of_type` will return `true`.alloc
///
/// For `FileType::Unknown`, the associated string will be used to check for equality. That
/// means that for unsupported file types, all files will be considered the same if they share
/// the same file extension, or if none of the files have an extension.
pub fn are_all_files_of_type(files: &[&str], file_type: FileType) -> bool {
    !files
        .iter()
        .map(|file| FileType::from_str(*file))
        .filter_map(Result::ok) // FileType::from_str only returns Ok, so this filter is fine.
        .any(|parsed_file_type| parsed_file_type != file_type)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_all_files_same_type_cpp() {
        assert_eq!(are_all_files_of_type(&["test.h", "test.cc"], FileType::Cpp), true);
    }

    #[test]
    fn test_different_types_cpp() {
        assert_eq!(are_all_files_of_type(&["test.foo", "test.cc"], FileType::Cpp), false);
    }

    #[test]
    fn test_all_files_same_type_unknown() {
        assert_eq!(
            are_all_files_of_type(&["test.foo", "test.foo"], FileType::Unknown("foo".to_string())),
            true
        );
    }

    #[test]
    fn test_different_types_unknown() {
        assert_eq!(
            are_all_files_of_type(&["test.foo", "test.bar"], FileType::Unknown("foo".to_string())),
            false
        );
    }
}
