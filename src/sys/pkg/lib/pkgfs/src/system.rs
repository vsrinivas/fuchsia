// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Typesafe wrappers around the /pkgfs/system filesystem.

use {crate::iou, anyhow::anyhow, fidl_fuchsia_io::DirectoryProxy, std::fs};

/// An open handle to /pkgfs/system
pub struct Client {
    proxy: DirectoryProxy,
}

impl Client {
    /// Returns a `Client` connected to pkgfs from the current component's namespace
    pub fn open_from_namespace() -> Result<Self, anyhow::Error> {
        Ok(Self { proxy: iou::open_directory_from_namespace("/pkgfs/system")? })
    }

    /// Returns a `Client` connected to pkgfs from the given pkgfs root dir.
    pub fn open_from_pkgfs_root(pkgfs: &DirectoryProxy) -> Result<Self, anyhow::Error> {
        Ok(Self {
            proxy: iou::open_directory_no_describe(
                pkgfs,
                "system",
                fidl_fuchsia_io::OPEN_RIGHT_READABLE,
            )?,
        })
    }

    /// Open a file from the system_image package wrapped by this `Client`.
    pub async fn open_file(&self, path: &str) -> Result<fs::File, anyhow::Error> {
        let file_proxy =
            iou::open_file(&self.proxy, path, fidl_fuchsia_io::OPEN_RIGHT_READABLE).await?;
        Ok(fdio::create_fd(
            file_proxy
                .into_channel()
                .map_err(|_| anyhow!("into_channel() on FileProxy failed"))?
                .into_zx_channel()
                .into(),
        )?)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, blobfs_ramdisk::BlobfsRamdisk, fuchsia_pkg_testing::SystemImageBuilder,
        matches::assert_matches, pkgfs_ramdisk::PkgfsRamdisk, std::io::Read as _,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn from_pkgfs_root_open_file_succeeds() {
        let system_image_package = SystemImageBuilder::new(&[]).build().await;
        let blobfs = BlobfsRamdisk::start().unwrap();
        system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
        let pkgfs = PkgfsRamdisk::start_with_blobfs(
            blobfs,
            Some(&system_image_package.meta_far_merkle_root().to_string()),
        )
        .unwrap();
        let client = Client::open_from_pkgfs_root(&pkgfs.root_dir_proxy().unwrap()).unwrap();

        let mut contents = vec![];
        client.open_file("meta").await.unwrap().read_to_end(&mut contents).unwrap();

        assert_eq!(
            contents,
            &b"ac005270136ee566e3a908267e0eb1076fd777430cb0216bb1e96fc866fad6ed"[..]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn from_pkgfs_root_open_file_fails_on_missing_file() {
        let system_image_package = SystemImageBuilder::new(&[]).build().await;
        let blobfs = BlobfsRamdisk::start().unwrap();
        system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
        let pkgfs = PkgfsRamdisk::start_with_blobfs(
            blobfs,
            Some(&system_image_package.meta_far_merkle_root().to_string()),
        )
        .unwrap();
        let client = Client::open_from_pkgfs_root(&pkgfs.root_dir_proxy().unwrap()).unwrap();

        assert_matches!(client.open_file("missing-file").await, Err(_));
    }
}
