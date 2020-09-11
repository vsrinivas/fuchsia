// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Typesafe wrappers around an "update" package.

mod board;
mod hash;
mod image;
mod images;
mod name;
mod packages;
mod update_mode;
mod version;

pub use crate::{
    board::VerifyBoardError,
    hash::HashError,
    image::{Image, ImageClass, OpenImageError},
    images::{ImageList, ResolveImagesError, UnverifiedImageList},
    name::VerifyNameError,
    packages::ParsePackageError,
    update_mode::{ParseUpdateModeError, UpdateMode},
    version::{ReadVersionError, SystemVersion},
};

use {fidl_fuchsia_io::DirectoryProxy, fuchsia_hash::Hash, fuchsia_url::pkg_url::PkgUrl};

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

    /// Verifies that the package's name/variant is "update/0".
    pub async fn verify_name(&self) -> Result<(), VerifyNameError> {
        name::verify(&self.proxy).await
    }

    /// Searches for the requested images in the update package, returning the resolved sequence of
    /// images in the same order as the requests.
    ///
    /// If a request ends in `[_type]`, that request is expanded to all found images with the
    /// prefix of the request, sorted alphabetically.
    pub async fn resolve_images(
        &self,
        requests: &[String],
    ) -> Result<UnverifiedImageList, ResolveImagesError> {
        images::resolve_images(&self.proxy, requests).await
    }

    /// Opens the given `image` as a resizable VMO buffer.
    pub async fn open_image(
        &self,
        image: &Image,
    ) -> Result<fidl_fuchsia_mem::Buffer, OpenImageError> {
        image::open(&self.proxy, image).await
    }

    /// Verifies the board file has the given `contents`.
    pub async fn verify_board(&self, contents: &str) -> Result<(), VerifyBoardError> {
        board::verify_board(&self.proxy, contents).await
    }

    /// Parses the update-mode file to obtain update mode.
    pub async fn update_mode(&self) -> Result<Option<UpdateMode>, ParseUpdateModeError> {
        update_mode::update_mode(&self.proxy).await
    }

    /// Returns the list of package urls that go in the universe of this update package.
    pub async fn packages(&self) -> Result<Vec<PkgUrl>, ParsePackageError> {
        packages::packages(&self.proxy).await
    }

    /// Returns the package hash of this update package.
    pub async fn hash(&self) -> Result<Hash, HashError> {
        hash::hash(&self.proxy).await
    }

    /// Returns the version of this update package.
    pub async fn version(&self) -> Result<SystemVersion, ReadVersionError> {
        version::read_version(&self.proxy).await
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

    fn proxy(&self) -> &DirectoryProxy {
        &self.update_pkg.proxy
    }

    async fn add_file(self, path: impl AsRef<std::path::Path>, contents: impl AsRef<[u8]>) -> Self {
        let path = path.as_ref();
        match path.parent() {
            Some(empty) if empty == std::path::Path::new("") => {}
            None => {}
            Some(parent) => std::fs::create_dir_all(self.temp_dir.path().join(parent)).unwrap(),
        }
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
