// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Typesafe wrappers around the /pkgfs/system filesystem.

use {
    anyhow::anyhow,
    fidl::endpoints::Proxy,
    fidl_fuchsia_io as fio,
    fuchsia_hash::{Hash, ParseHashError},
    std::fs,
    thiserror::Error,
};

/// An error encountered while opening the system image.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum SystemImageFileOpenError {
    #[error("the file does not exist in the system image package")]
    NotFound,

    #[error("while opening the file: {0}")]
    Io(fuchsia_fs::node::OpenError),

    #[error("unexpected error: {0}")]
    Other(anyhow::Error),
}

/// An error encountered while reading the system image hash.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum SystemImageFileHashError {
    #[error("failed to open \"meta\" file: {0}")]
    Open(#[from] SystemImageFileOpenError),

    #[error("while reading the file: {0}")]
    Read(#[from] fuchsia_fs::file::ReadError),

    #[error("while parsing the hash: {0}")]
    Parse(#[from] ParseHashError),
}

/// An open handle to /pkgfs/system.
#[derive(Debug, Clone)]
pub struct Client {
    proxy: fio::DirectoryProxy,
}

impl Client {
    /// Returns a `Client` connected to pkgfs from the current component's namespace.
    pub fn open_from_namespace() -> Result<Self, fuchsia_fs::node::OpenError> {
        Ok(Self {
            proxy: fuchsia_fs::directory::open_in_namespace(
                "/pkgfs/system",
                fio::OpenFlags::RIGHT_READABLE,
            )?,
        })
    }

    /// Returns a `Client` connected to pkgfs from the given pkgfs root dir.
    pub fn open_from_pkgfs_root(
        pkgfs: &fio::DirectoryProxy,
    ) -> Result<Self, fuchsia_fs::node::OpenError> {
        Ok(Self {
            proxy: fuchsia_fs::directory::open_directory_no_describe(
                pkgfs,
                "system",
                fio::OpenFlags::RIGHT_READABLE,
            )?,
        })
    }

    /// Open a file from the system_image package wrapped by this `Client`.
    pub async fn open_file(&self, path: &str) -> Result<fs::File, SystemImageFileOpenError> {
        let file_proxy = self.open_file_as_proxy(path).await?;
        fdio::create_fd(
            file_proxy
                .into_channel()
                .map_err(|_| {
                    SystemImageFileOpenError::Other(anyhow!("into_channel() on FileProxy failed"))
                })?
                .into_zx_channel()
                .into(),
        )
        .map_err(|e| SystemImageFileOpenError::Other(e.into()))
    }

    /// Returns the system image hash.
    pub async fn hash(&self) -> Result<Hash, SystemImageFileHashError> {
        let file_proxy = self.open_file_as_proxy("meta").await?;
        let hashstr = fuchsia_fs::file::read_to_string(&file_proxy).await?;
        let hash: Hash = hashstr.parse()?;

        Ok(hash)
    }

    async fn open_file_as_proxy(
        &self,
        path: &str,
    ) -> Result<fio::FileProxy, SystemImageFileOpenError> {
        fuchsia_fs::directory::open_file(&self.proxy, path, fio::OpenFlags::RIGHT_READABLE)
            .await
            .map_err(|e| match e {
                fuchsia_fs::node::OpenError::OpenError(fuchsia_zircon::Status::NOT_FOUND) => {
                    SystemImageFileOpenError::NotFound
                }
                other => SystemImageFileOpenError::Io(other),
            })
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, assert_matches::assert_matches, blobfs_ramdisk::BlobfsRamdisk,
        fuchsia_pkg_testing::SystemImageBuilder, pkgfs_ramdisk::PkgfsRamdisk, std::io::Read as _,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn from_pkgfs_root_open_file_succeeds() {
        let system_image_package = SystemImageBuilder::new().build().await;
        let blobfs = BlobfsRamdisk::start().unwrap();
        system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
        let pkgfs = PkgfsRamdisk::builder()
            .blobfs(blobfs)
            .system_image_merkle(system_image_package.meta_far_merkle_root())
            .start()
            .unwrap();
        let client = Client::open_from_pkgfs_root(&pkgfs.root_dir_proxy().unwrap()).unwrap();

        let mut contents = String::new();
        client.open_file("meta").await.unwrap().read_to_string(&mut contents).unwrap();

        assert_eq!(contents, "8bc6622d99b5fee3cf42c54d7f17b8b38c31b69da4573db98a647b692b760311");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn from_pkgfs_root_open_file_fails_on_missing_file() {
        let system_image_package = SystemImageBuilder::new().build().await;
        let blobfs = BlobfsRamdisk::start().unwrap();
        system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
        let pkgfs = PkgfsRamdisk::builder()
            .blobfs(blobfs)
            .system_image_merkle(system_image_package.meta_far_merkle_root())
            .start()
            .unwrap();
        let client = Client::open_from_pkgfs_root(&pkgfs.root_dir_proxy().unwrap()).unwrap();

        assert_matches!(
            client.open_file("missing-file").await,
            Err(SystemImageFileOpenError::NotFound)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn from_pkgfs_root_hash_succeeds() {
        let system_image_package = SystemImageBuilder::new().build().await;
        let blobfs = BlobfsRamdisk::start().unwrap();
        system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
        let pkgfs = PkgfsRamdisk::builder()
            .blobfs(blobfs)
            .system_image_merkle(system_image_package.meta_far_merkle_root())
            .start()
            .unwrap();
        let client = Client::open_from_pkgfs_root(&pkgfs.root_dir_proxy().unwrap()).unwrap();

        assert_eq!(
            client.hash().await.unwrap(),
            "8bc6622d99b5fee3cf42c54d7f17b8b38c31b69da4573db98a647b692b760311"
                .parse::<Hash>()
                .unwrap()
        );
    }
}
