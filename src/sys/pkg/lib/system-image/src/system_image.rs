// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        get_system_image_hash, CachePackages, CachePackagesInitError, NonStaticAllowList,
        StaticPackages, StaticPackagesInitError,
    },
    anyhow::Context as _,
    fuchsia_hash::Hash,
    package_directory::RootDir,
};

static DISABLE_RESTRICTIONS_FILE_PATH: &str = "data/pkgfs_disable_executability_restrictions";
static NON_STATIC_ALLOW_LIST_FILE_PATH: &str =
    "data/pkgfs_packages_non_static_packages_allowlist.txt";

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum ExecutabilityRestrictions {
    Enforce,
    DoNotEnforce,
}

/// System image package.
pub struct SystemImage {
    root_dir: RootDir<blobfs::Client>,
}

impl SystemImage {
    pub async fn new(
        blobfs: blobfs::Client,
        boot_args: &fidl_fuchsia_boot::ArgumentsProxy,
    ) -> Result<Self, anyhow::Error> {
        let hash = get_system_image_hash(boot_args).await.context("getting system_image hash")?;
        let root_dir =
            RootDir::new(blobfs, hash).await.context("creating RootDir for system_image")?;
        Ok(SystemImage { root_dir })
    }

    /// Make a `SystemImage` from a `RootDir` for the `system_image` package.
    pub fn from_root_dir(root_dir: RootDir<blobfs::Client>) -> Self {
        Self { root_dir }
    }

    pub fn load_executability_restrictions(&self) -> ExecutabilityRestrictions {
        match self.root_dir.has_file(DISABLE_RESTRICTIONS_FILE_PATH) {
            true => ExecutabilityRestrictions::DoNotEnforce,
            false => ExecutabilityRestrictions::Enforce,
        }
    }

    /// The hash of the `system_image` package.
    pub fn hash(&self) -> &Hash {
        self.root_dir.hash()
    }

    /// Load `data/cache_packages.json`.
    pub async fn cache_packages(&self) -> Result<CachePackages, CachePackagesInitError> {
        self.root_dir
            .read_file("data/cache_packages.json")
            .await
            .map_err(CachePackagesInitError::ReadCachePackagesJson)
            .and_then(|content| CachePackages::from_json(content.as_slice()))
    }

    /// Load `data/static_packages`.
    pub async fn static_packages(&self) -> Result<StaticPackages, StaticPackagesInitError> {
        StaticPackages::deserialize(
            self.root_dir
                .read_file("data/static_packages")
                .await
                .map_err(StaticPackagesInitError::ReadStaticPackages)?
                .as_slice(),
        )
        .map_err(StaticPackagesInitError::ProcessingStaticPackages)
    }

    /// Load the non-static allow list from
    /// "data/pkgfs_packages_non_static_packages_allowlist.txt". Errors during loading result in an
    /// empty allow list.
    pub async fn non_static_allow_list(&self) -> NonStaticAllowList {
        async {
            NonStaticAllowList::parse(
                self.root_dir
                    .read_file(NON_STATIC_ALLOW_LIST_FILE_PATH)
                    .await
                    .context("reading allow list contents")?
                    .as_slice(),
            )
            .context("parsing allow list contents")
        }
        .await
        .unwrap_or_else(|e| {
            tracing::warn!(
                "Failed to load non static allow list from system_image, treating as empty: {e:#}"
            );
            NonStaticAllowList::empty()
        })
    }

    /// Consume self and return the contained `package_directory::RootDir`.
    pub fn into_root_dir(self) -> RootDir<blobfs::Client> {
        self.root_dir
    }
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches, fuchsia_pkg_testing::SystemImageBuilder};

    struct TestEnv {
        _blobfs: blobfs_ramdisk::BlobfsRamdisk,
    }

    impl TestEnv {
        async fn new(system_image: SystemImageBuilder<'_>) -> (Self, SystemImage) {
            let blobfs = blobfs_ramdisk::BlobfsRamdisk::start().unwrap();
            let system_image = system_image.build().await;
            system_image.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
            let root_dir =
                RootDir::new(blobfs.client(), *system_image.meta_far_merkle_root()).await.unwrap();
            (Self { _blobfs: blobfs }, SystemImage { root_dir })
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn cache_packages_fails_without_config_files() {
        let (_env, system_image) = TestEnv::new(SystemImageBuilder::new()).await;
        assert_matches!(
            system_image.cache_packages().await,
            Err(CachePackagesInitError::ReadCachePackagesJson(
                package_directory::ReadFileError::NoFileAtPath { .. }
            ))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn cache_packages_deserialize_valid_line_oriented() {
        let (_env, system_image) = TestEnv::new(
            SystemImageBuilder::new()
                .cache_package("name/variant".parse().unwrap(), [0; 32].into()),
        )
        .await;

        assert_eq!(
            system_image.cache_packages().await.unwrap(),
            CachePackages::from_entries(
                vec!["fuchsia-pkg://fuchsia.com/name/variant?hash=0000000000000000000000000000000000000000000000000000000000000000"
                    .parse()
                    .unwrap()
                ]
            )
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn static_packages_deserialize_valid_line_oriented() {
        let (_env, system_image) = TestEnv::new(
            SystemImageBuilder::new()
                .static_package("name/variant".parse().unwrap(), [0; 32].into()),
        )
        .await;

        assert_eq!(
            system_image.static_packages().await.unwrap(),
            StaticPackages::from_entries(vec![("name/variant".parse().unwrap(), [0; 32].into())])
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn non_static_allow_list_succeeds() {
        let (_env, system_image) = TestEnv::new(
            SystemImageBuilder::new().pkgfs_non_static_packages_allowlist(&["allow-me"]),
        )
        .await;

        assert_eq!(
            system_image.non_static_allow_list().await,
            NonStaticAllowList::parse(b"allow-me\n").unwrap()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn non_static_allow_list_missing_file_causes_empty_list() {
        let (_env, system_image) = TestEnv::new(SystemImageBuilder::new()).await;

        assert_eq!(system_image.non_static_allow_list().await, NonStaticAllowList::empty());
    }
}
