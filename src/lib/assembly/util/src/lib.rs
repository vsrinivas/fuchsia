// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use std::path::Path;

/// Extension trait for getting a String from a Path.
pub trait PathToStringExt {
    /// Convert the path to a string if it is valid UTF-8, and return an error otherwise.
    fn path_to_string(&self) -> Result<String>;
}

impl PathToStringExt for Path {
    fn path_to_string(&self) -> Result<String> {
        self.to_str()
            .context(format!("Path is not valid UTF-8: {}", self.display()))
            .map(str::to_string)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::OsString;
    use std::os::unix::ffi::OsStringExt;
    use std::path::PathBuf;

    #[test]
    fn path_to_string() {
        let path = PathBuf::from("/some/path/to/file.txt");
        let string = path.path_to_string().unwrap();
        assert_eq!(string, "/some/path/to/file.txt".to_string());
    }

    #[test]
    fn invalid_path_to_string() {
        let invalid_path = PathBuf::from(OsString::from_vec(b"invalid\xe7".to_vec()));
        assert!(invalid_path.path_to_string().is_err());
    }
}
