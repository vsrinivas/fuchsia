// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        get_system_image_hash,
        path_hash_mapping::{Cache, PathHashMapping},
        CachePackages, CachePackagesInitError,
    },
    anyhow::Context as _,
    fuchsia_hash::Hash,
    package_directory::RootDir,
};

static DISABLE_RESTRICTIONS_FILE_PATH: &str = "data/pkgfs_disable_executability_restrictions";

#[derive(Debug, PartialEq, Eq)]
pub enum ExecutabilityRestrictions {
    Enforce,
    DoNotEnforce,
}

/// System image package.
pub struct SystemImage {
    root_dir: RootDir,
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

    pub fn load_executability_restrictions(&self) -> ExecutabilityRestrictions {
        match self.root_dir.has_file(DISABLE_RESTRICTIONS_FILE_PATH) {
            true => ExecutabilityRestrictions::DoNotEnforce,
            false => ExecutabilityRestrictions::Enforce,
        }
    }

    // TODO(fxb/90513): This method can be removed after BasePackages are migrated to follow
    // this file's pattern.
    pub fn root_dir(&self) -> &RootDir {
        &self.root_dir
    }

    /// The hash of the `system_image` package.
    pub fn hash(&self) -> &Hash {
        &self.root_dir.hash()
    }

    /// Load `data/cache_packages.json`, and fallback to `data/cache_packages` if not found.
    pub async fn cache_packages(&self) -> Result<CachePackages, CachePackagesInitError> {
        // Attempt to read data/cache_packages.json.
        let cache_packages_json = self.root_dir.read_file("data/cache_packages.json").await;

        match cache_packages_json {
            Ok(content) => return CachePackages::from_json(content.as_slice()).map_err(Into::into),
            // data/cache_packages.json not found, fall through to attempt reading data/cache_packages.
            Err(package_directory::ReadFileError::NoFileAtPath { .. }) => {}
            Err(e) => {
                return Err(CachePackagesInitError::ReadCachePackagesJson(e));
            }
        }

        // Attempt to read data/cache_packages.
        CachePackages::from_path_hash_mapping(PathHashMapping::<Cache>::deserialize(
            self.root_dir
                .read_file("data/cache_packages")
                .await
                .map_err(CachePackagesInitError::ReadCachePackages)?
                .as_slice(),
        )?)
        .map_err(Into::into)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_pkg_testing::{blobfs::Fake as FakeBlobfs, PackageBuilder},
        matches::assert_matches,
    };

    struct SystemImageBuilder {
        cache_packages_json: Option<String>,
        cache_packages: Option<String>,
        blobfs_fake: FakeBlobfs,
        blobfs_client: blobfs::Client,
    }

    impl SystemImageBuilder {
        fn new() -> Self {
            let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
            Self { blobfs_fake, blobfs_client, cache_packages: None, cache_packages_json: None }
        }

        fn set_cache_packages(mut self, cache_packages: String) -> Self {
            self.cache_packages = Some(cache_packages);
            self
        }

        fn set_cache_packages_json(mut self, cache_packages_json: String) -> Self {
            self.cache_packages_json = Some(cache_packages_json);
            self
        }

        async fn build(&self) -> SystemImage {
            let mut pkg_builder = PackageBuilder::new("system_image");
            if let Some(cache_packages) = &self.cache_packages {
                pkg_builder =
                    pkg_builder.add_resource_at("data/cache_packages", cache_packages.as_bytes());
            }
            if let Some(cache_packages_json) = &self.cache_packages_json {
                pkg_builder = pkg_builder
                    .add_resource_at("data/cache_packages.json", cache_packages_json.as_bytes());
            }

            let pkg = pkg_builder.build().await.unwrap();
            let (metafar_blob, content_blobs) = pkg.contents();
            self.blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
            for content in content_blobs {
                self.blobfs_fake.add_blob(content.merkle, content.contents);
            }
            let root_dir =
                RootDir::new(self.blobfs_client.clone(), metafar_blob.merkle).await.unwrap();
            SystemImage { root_dir }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn fails_without_config_files() {
        let system_image = SystemImageBuilder::new().build().await;
        assert_matches!(
            system_image.cache_packages().await,
            Err(CachePackagesInitError::ReadCachePackages(
                package_directory::ReadFileError::NoFileAtPath { .. }
            ))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn deserialize_valid_file_list_hashes() {
        let cache_packages =
            "name/variant=0000000000000000000000000000000000000000000000000000000000000000\n\
             other-name/other-variant=1111111111111111111111111111111111111111111111111111111111111111\n";
        let path_hash_packages =
            PathHashMapping::<Cache>::deserialize(cache_packages.as_bytes()).unwrap();
        let expected = CachePackages::from_path_hash_mapping(path_hash_packages).unwrap();
        let builder = SystemImageBuilder::new();
        let cache_packages = builder
            .set_cache_packages(cache_packages.to_string())
            .build()
            .await
            .cache_packages()
            .await
            .unwrap();
        assert_eq!(cache_packages, expected);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn deserialize_valid_cache_packages_json() {
        let cache_packages_json = br#"
        {
            "version": "1",
            "content": [
                "fuchsia-pkg://foo.bar/qwe/0?hash=0000000000000000000000000000000000000000000000000000000000000000",
                "fuchsia-pkg://foo.bar/rty/0?hash=1111111111111111111111111111111111111111111111111111111111111111"
            ]
        }"#;
        let expected = CachePackages::from_json(cache_packages_json).unwrap();
        let cache_packages =
            "name/variant=0000000000000000000000000000000000000000000000000000000000000000\n\
             other-name/other-variant=1111111111111111111111111111111111111111111111111111111111111111\n";
        let builder = SystemImageBuilder::new();
        let cache_packages = builder
            .set_cache_packages(cache_packages.to_string())
            .set_cache_packages_json(
                String::from_utf8(cache_packages_json.to_vec()).expect("invalid utf-8"),
            )
            .build()
            .await
            .cache_packages()
            .await
            .unwrap();
        assert_eq!(cache_packages, expected);
    }
}
