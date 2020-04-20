// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Typesafe wrappers around an "update" package.

mod board;
mod image;

pub use crate::{board::VerifyBoardError, image::OpenImageError};
use fidl_fuchsia_io::DirectoryProxy;

/// An open handle to an "update" package.
#[derive(Debug)]
pub struct UpdatePackage {
    proxy: DirectoryProxy,
}

impl UpdatePackage {
    /// Creates a new [`UpdatePackage`] with the given proxy.
    pub fn new(proxy: DirectoryProxy) -> Self {
        Self { proxy }
    }

    /// Opens the image with the given `name` as a resizable VMO buffer.
    pub async fn open_image(&self, name: &str) -> Result<fidl_fuchsia_mem::Buffer, OpenImageError> {
        image::open(&self.proxy, name).await
    }

    /// Verifies the board file has the given `contents`.
    pub async fn verify_board(&self, contents: &str) -> Result<(), VerifyBoardError> {
        board::verify_board(&self.proxy, contents).await
    }
}

#[cfg(test)]
struct TestUpdatePackage {
    update_pkg: UpdatePackage,
    temp_dir: tempfile::TempDir,
}

#[cfg(test)]
impl TestUpdatePackage {
    fn new() -> Self {
        let temp_dir = tempfile::tempdir().expect("/tmp to exist");
        let update_pkg_proxy = io_util::directory::open_in_namespace(
            temp_dir.path().to_str().unwrap(),
            io_util::OPEN_RIGHT_READABLE,
        )
        .expect("temp dir to open");
        Self { temp_dir, update_pkg: UpdatePackage::new(update_pkg_proxy) }
    }

    async fn add_file(self, path: impl AsRef<std::path::Path>, contents: impl AsRef<[u8]>) -> Self {
        io_util::file::write_in_namespace(
            self.temp_dir.path().join(path).to_str().unwrap(),
            contents,
        )
        .await
        .expect("create test update package file");
        self
    }
}

#[cfg(test)]
impl std::ops::Deref for TestUpdatePackage {
    type Target = UpdatePackage;

    fn deref(&self) -> &Self::Target {
        &self.update_pkg
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl_fuchsia_io::DirectoryMarker};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn lifecycle() {
        let (proxy, _server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        UpdatePackage::new(proxy);
    }
}
