// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxr/66157): Migrate to the new library that substitutes io_utils and files_async.
// Ask for host-side support on the new library (fxr/467217).

use {
    anyhow::{format_err, Result},
    fidl_fuchsia_io as fio,
    files_async::readdir,
    io_util::{
        directory::{open_directory_no_describe, open_file_no_describe},
        file::read_to_string,
    },
    std::path::PathBuf,
};

// A convenience wrapper over a FIDL DirectoryProxy.
pub struct Directory {
    path: PathBuf,
    pub proxy: fio::DirectoryProxy,
}

impl Directory {
    // Connect to a directory in the namespace
    #[cfg(target_os = "fuchsia")]
    pub fn from_namespace(path: PathBuf) -> Result<Self> {
        let path_str = path
            .as_os_str()
            .to_str()
            .ok_or_else(|| format_err!("Could not convert path to string"))?;
        let proxy = io_util::directory::open_in_namespace(path_str, fio::OPEN_RIGHT_READABLE)?;
        Ok(Self { path, proxy })
    }

    // Create a Directory object from a proxy
    pub fn from_proxy(proxy: fio::DirectoryProxy) -> Self {
        let path = PathBuf::from(".");
        Self { path, proxy }
    }

    // Open a directory at the given |relative_path|.
    pub fn open_dir(&self, relative_path: &str) -> Result<Self> {
        let path = self.path.join(relative_path);
        match open_directory_no_describe(&self.proxy, relative_path, fio::OPEN_RIGHT_READABLE) {
            Ok(proxy) => Ok(Self { path, proxy }),
            Err(e) => Err(format_err!("Could not open dir `{}`: {}", path.as_path().display(), e)),
        }
    }

    // Read the contents of a file at the given relative |path|
    pub async fn read_file(&self, relative_path: &str) -> Result<String> {
        let path = self.path.join(relative_path);
        let proxy =
            match open_file_no_describe(&self.proxy, relative_path, fio::OPEN_RIGHT_READABLE) {
                Ok(proxy) => proxy,
                Err(e) => {
                    return Err(format_err!(
                        "Could not open file `{}`: {}",
                        path.as_path().display(),
                        e
                    ))
                }
            };

        match read_to_string(&proxy).await {
            Ok(data) => Ok(data),
            Err(e) => Err(format_err!("Could not read file `{}`: {}", path.as_path().display(), e)),
        }
    }

    // Checks if a file with the given |filename| exists in this directory.
    // Function is not recursive. Does not check subdirectories.
    pub async fn exists(&self, filename: &str) -> Result<bool> {
        match self.entries().await {
            Ok(entries) => Ok(entries.iter().any(|s| s == filename)),
            Err(e) => Err(format_err!(
                "Could not check if `{}` exists in `{}`: {}",
                filename,
                self.path.as_path().display(),
                e
            )),
        }
    }

    // Return a list of filenames in the directory
    pub async fn entries(&self) -> Result<Vec<String>> {
        match readdir(&self.proxy).await {
            Ok(entries) => Ok(entries.iter().map(|entry| entry.name.clone()).collect()),
            Err(e) => Err(format_err!(
                "Could not get entries of `{}`: {}",
                self.path.as_path().display(),
                e
            )),
        }
    }
}
