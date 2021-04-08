// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Encapsulates the software distribution to host systems.
//!
//! Uses a unique identifier (FMS Name) to refer to SDK Modules which allows
//! users of FMS lib to not be concerned about where modules are being
//! downloaded from (be it CIPD, MOS-TUF, GCS, or something else).

use {
    anyhow::{bail, Result},
    std::path::{Path, PathBuf},
};

/// Manager for finding available SDK Modules for download or ensuring that an
/// SDK Module (artifact) is downloaded.
struct Modules {
    sdk_path: PathBuf,
    // Contents TBD.
}

impl Modules {
    pub fn new<P: AsRef<Path>>(sdk_path: P) -> Self {
        Self { sdk_path: sdk_path.as_ref().to_path_buf() }
    }

    /// Make a module available for use.
    ///
    /// If the module identified by 'module_name' is already downloaded and up
    /// to date the path to the module will be returned immediately.
    /// If module is missing, it will be downloaded made ready before returning
    /// the path.
    pub fn ensure(&self, module_name: &str) -> Result<PathBuf> {
        // Temporary while API is fleshed out.
        if module_name == "not-an-sdk-module" {
            bail!("Error TBD");
        }
        Ok(self.sdk_path.join("fake").join("path"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_lib() {
        let sdk_dir = Path::new("~/fuchsia-sdk");
        let modules = Modules::new(&sdk_dir);
        assert_eq!(modules.sdk_path, sdk_dir);
        let modules = Modules::new(sdk_dir.to_path_buf());
        assert_eq!(modules.sdk_path, sdk_dir);
    }

    #[test]
    fn test_ensure() {
        let sdk_dir = Path::new("~/fuchsia-sdk");
        let modules = Modules::new(&sdk_dir);
        let path = modules.ensure("fake-sdk-module").unwrap();
        assert_eq!(path, sdk_dir.join("fake").join("path"));
    }

    #[test]
    fn test_ensure_not_found() {
        let sdk_dir = Path::new("~/fuchsia-sdk");
        let modules = Modules::new(&sdk_dir);
        match modules.ensure("not-an-sdk-module") {
            Err(e) => assert_eq!(e.to_string(), "Error TBD"),
            Ok(_) => assert!(false),
        }
    }
}
