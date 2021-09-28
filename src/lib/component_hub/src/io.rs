// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxr/66157): Migrate to the new library that substitutes io_utils and files_async.
// Ask for host-side support on the new library (fxr/467217).

use {
    anyhow::{format_err, Result},
    fidl_fuchsia_io as fio,
    files_async::readdir,
    fuchsia_zircon_status::Status,
    io_util::{
        directory::{open_directory_no_describe, open_file_no_describe},
        file::{close, read, read_to_string, write},
    },
    std::path::{Path, PathBuf},
};

// A convenience wrapper over a FIDL DirectoryProxy.
#[derive(Clone)]
pub struct Directory {
    path: PathBuf,
    pub proxy: fio::DirectoryProxy,
}

impl Directory {
    // Connect to a directory in the namespace
    #[cfg(target_os = "fuchsia")]
    pub fn from_namespace<P: AsRef<Path>>(path: P) -> Result<Self> {
        let path_str = path
            .as_ref()
            .as_os_str()
            .to_str()
            .ok_or_else(|| format_err!("Could not convert path to string"))?;
        let proxy = io_util::directory::open_in_namespace(path_str, fio::OPEN_RIGHT_READABLE)?;
        let path = path.as_ref().to_path_buf();
        Ok(Self { path, proxy })
    }

    // Create a Directory object from a proxy
    pub fn from_proxy(proxy: fio::DirectoryProxy) -> Self {
        let path = PathBuf::from(".");
        Self { path, proxy }
    }

    // Open a directory at the given `relative_path` as readable.
    pub fn open_dir_readable<P: AsRef<Path>>(&self, relative_path: P) -> Result<Self> {
        self.open_dir(relative_path, fio::OPEN_RIGHT_READABLE)
    }

    // Open a directory at the given `relative_path` with the provided flags.
    pub fn open_dir<P: AsRef<Path>>(&self, relative_path: P, flags: u32) -> Result<Self> {
        let path = self.path.join(relative_path.as_ref());
        let relative_path = match relative_path.as_ref().to_str() {
            Some(relative_path) => relative_path,
            None => return Err(format_err!("Could not convert relative path to &str")),
        };
        match open_directory_no_describe(&self.proxy, relative_path, flags) {
            Ok(proxy) => Ok(Self { path, proxy }),
            Err(e) => Err(format_err!("Could not open dir `{}`: {}", path.as_path().display(), e)),
        }
    }

    // Read the contents of a file at the given `relative_path` as a string
    pub async fn read_file<P: AsRef<Path>>(&self, relative_path: P) -> Result<String> {
        let path = self.path.join(relative_path.as_ref());
        let relative_path = match relative_path.as_ref().to_str() {
            Some(relative_path) => relative_path,
            None => return Err(format_err!("Relative path is not valid unicode")),
        };
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
            Err(e) => Err(format_err!(
                "Could not read file `{}` as string: {}",
                path.as_path().display(),
                e
            )),
        }
    }

    // Read the contents of a file at the given `relative_path` as bytes
    pub async fn read_file_bytes<P: AsRef<Path>>(&self, relative_path: P) -> Result<Vec<u8>> {
        let path = self.path.join(relative_path.as_ref());
        let relative_path = match relative_path.as_ref().to_str() {
            Some(relative_path) => relative_path,
            None => return Err(format_err!("Relative path is not valid unicode")),
        };
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

        match read(&proxy).await {
            Ok(data) => Ok(data),
            Err(e) => Err(format_err!("Could not read file `{}`: {}", path.as_path().display(), e)),
        }
    }

    // Checks if a file with the given `filename` exists in this directory.
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

    // Attempts to create a new file at the given |relative_path|.
    // Replaces the file contents if it already exists.
    pub async fn create_file<P: AsRef<Path>>(&self, relative_path: P, data: &[u8]) -> Result<()> {
        let path = self.path.join(relative_path.as_ref());
        let relative_path = match relative_path.as_ref().to_str() {
            Some(relative_path) => relative_path,
            None => return Err(format_err!("Relative path is not valid unicode")),
        };

        let file = match open_file_no_describe(
            &self.proxy,
            relative_path,
            fio::OPEN_RIGHT_WRITABLE | fio::OPEN_FLAG_CREATE,
        ) {
            Ok(proxy) => proxy,
            Err(e) => {
                return Err(format_err!(
                    "Could not open file `{}`: {}",
                    path.as_path().display(),
                    e
                ))
            }
        };

        match file.truncate(0).await {
            Ok(s) => {
                Status::ok(s).map_err(|e| {
                    format_err!("Could not truncate file `{}`: {}", path.as_path().display(), &e)
                })?;
            }
            Err(e) => {
                return Err(format_err!(
                    "Could not truncate file `{}`: {}",
                    path.as_path().display(),
                    &e
                ))
            }
        }

        match write(&file, data).await {
            Ok(()) => {}
            Err(e) => {
                return Err(format_err!(
                    "Could not write to file `{}`: {}",
                    path.as_path().display(),
                    e
                ))
            }
        }

        match close(file).await {
            Ok(()) => Ok(()),
            Err(e) => {
                Err(format_err!("Could not close file `{}`: {}", path.as_path().display(), e))
            }
        }
    }

    // Return a list of directory entries in the directory
    pub async fn entries(&self) -> Result<Vec<String>> {
        match readdir(&self.proxy).await {
            Ok(entries) => Ok(entries.into_iter().map(|e| e.name).collect()),
            Err(e) => Err(format_err!(
                "Could not get entries of `{}`: {}",
                self.path.as_path().display(),
                e
            )),
        }
    }
}
