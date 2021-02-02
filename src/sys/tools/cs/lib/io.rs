// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxr/66157): Migrate to the new library that substitues io_utils and files_async.
// Ask for host-side support on the new library (fxr/467217).
use {
    anyhow::{Error, Result},
    fidl_fuchsia_io as fio,
    files_async::*,
    fuchsia_async::TimeoutExt,
    futures::FutureExt,
    io_util::{directory::*, file::*},
};

#[cfg(target_os = "fuchsia")]
use std::path::PathBuf;

// A convenience wrapper over a FIDL FileProxy.
// Functions of this struct do not tolerate errors and will panic when they encounter them.
struct File {
    proxy: fio::FileProxy,
}

impl File {
    async fn read_string(&self) -> Result<String, Error> {
        Ok(read_to_string(&self.proxy).await?)
    }

    async fn close(self) {
        io_util::file::close(self.proxy).await.unwrap();
    }
}

// A convenience wrapper over a FIDL DirectoryProxy.
// Functions of this struct do not tolerate errors and will panic when they encounter them.
pub struct Directory {
    pub proxy: fio::DirectoryProxy,
}

// Time to wait before giving up on opening a directory
static DIR_OPEN_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(1);

impl Directory {
    // Create a Directory object from a path in the namespace
    #[cfg(target_os = "fuchsia")]
    pub fn from_namespace(path: PathBuf) -> Result<Directory, Error> {
        let path_str = path.into_os_string().into_string().unwrap();
        let proxy = io_util::directory::open_in_namespace(
            &path_str,
            fio::OPEN_RIGHT_WRITABLE | fio::OPEN_RIGHT_READABLE,
        )?;
        Ok(Directory { proxy })
    }

    // Create a Directory object from a proxy
    pub fn from_proxy(proxy: fio::DirectoryProxy) -> Directory {
        Directory { proxy }
    }

    // Open a file that already exists in the directory with the given |filename|.
    async fn open_file(&self, filename: &str) -> Result<File, Error> {
        let proxy = open_file_no_describe(&self.proxy, filename, fio::OPEN_RIGHT_READABLE).unwrap();
        Ok(File { proxy })
    }

    // Open a directory that already exists in the directory with the given |filename|.
    pub async fn open_dir(&self, filename: &str) -> Directory {
        let proxy = open_directory(&self.proxy, filename, fio::OPEN_RIGHT_READABLE).await.unwrap();
        Directory { proxy }
    }

    // Open a directory with a timeout. If the directory could not be opened in time,
    // None is returned.
    pub async fn open_dir_timeout(&self, filename: &str) -> Option<Directory> {
        self.open_dir(filename).map(|d| Some(d)).on_timeout(DIR_OPEN_TIMEOUT, || None).await
    }

    // Returns the contents of a file in this directory as a string
    pub async fn read_file(&self, filename: &str) -> Result<String, Error> {
        let file = self.open_file(filename).await?;
        let data = file.read_string().await?;
        file.close().await;
        Ok(data)
    }

    // Checks if a file exists in this directory.
    // Function is not recursive. Does not check subdirectories.
    pub async fn exists(&self, filename: &str) -> bool {
        self.entries().await.iter().any(|s| s == filename)
    }

    // Return a list of filenames in the directory
    pub async fn entries(&self) -> Vec<String> {
        readdir(&self.proxy).await.unwrap().iter().map(|entry| entry.name.clone()).collect()
    }
}
