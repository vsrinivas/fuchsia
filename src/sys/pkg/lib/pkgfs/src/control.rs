// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Typesafe wrappers around the /pkgfs/ctl filesystem.

use {crate::iou, fidl_fuchsia_io::DirectoryProxy, fuchsia_zircon::Status, thiserror::Error};

/// An error encountered while garbage collecting blobs
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum GcError {
    #[error("while sending request: {}", _0)]
    Fidl(fidl::Error),

    #[error("unlink failed with status: {}", _0)]
    UnlinkError(Status),
}

/// An open handle to /pkgfs/ctl
#[derive(Debug, Clone)]
pub struct Client {
    proxy: DirectoryProxy,
}

impl Client {
    /// Returns an client connected to pkgfs from the current component's namespace
    pub fn open_from_namespace() -> Result<Self, anyhow::Error> {
        let proxy = iou::open_directory_from_namespace("/pkgfs/ctl")?;
        Ok(Client { proxy })
    }

    /// Returns an client connected to pkgfs from the given pkgfs root dir.
    pub fn open_from_pkgfs_root(pkgfs: &DirectoryProxy) -> Result<Self, anyhow::Error> {
        Ok(Client {
            proxy: iou::open_directory_no_describe(
                pkgfs,
                "ctl",
                fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
            )?,
        })
    }

    /// Performs a garbage collection
    pub async fn gc(&self) -> Result<(), GcError> {
        match self.proxy.unlink("garbage").await.map(Status::from_raw) {
            Ok(Status::OK) => Ok(()),
            Ok(status) => Err(GcError::UnlinkError(status)),
            Err(err) => Err(GcError::Fidl(err)),
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, matches::assert_matches, pkgfs_ramdisk::PkgfsRamdisk};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn gc() {
        let pkgfs = PkgfsRamdisk::start().unwrap();
        let root = pkgfs.root_dir_proxy().unwrap();
        let client = Client::open_from_pkgfs_root(&root).unwrap();

        assert_matches!(client.gc().await, Ok(()));

        pkgfs.stop().await.unwrap();
    }
}
