// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _, Error},
    fuchsia_inspect as finspect,
    fuchsia_inspect_contrib::inspectable::InspectableLen,
    fuchsia_merkle::Hash,
    fuchsia_pkg::PackagePath,
    futures::{StreamExt, TryStreamExt},
    std::collections::HashSet,
};

const SYSTEM_IMAGE_NAME: &str = "system_image";
const SYSTEM_IMAGE_VARIANT: &str = "0";

/// BasePackages represents the set of packages in the static_packages list, plus the system_image
/// package.
#[derive(Debug)]
pub struct BasePackages {
    base_blobs: BaseBlobs,
    paths_to_hashes: Vec<(PackagePath, Hash)>,
    #[allow(unused)]
    node: finspect::Node,
}

#[derive(Debug)]
struct BaseBlobs {
    blobs: InspectableLen<HashSet<Hash>>,
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    node: finspect::Node,
}

impl BaseBlobs {
    fn new(blobs: HashSet<Hash>, node: finspect::Node) -> Self {
        Self { blobs: InspectableLen::new(blobs, &node, "count"), node }
    }
}

impl BasePackages {
    pub async fn new(
        blobfs: &blobfs::Client,
        system_image: &system_image::SystemImage,
        node: finspect::Node,
    ) -> Result<Self, anyhow::Error> {
        // Add the system image package to the set of static packages to create the set of base
        // packages. If we're constructing BasePackages, we must have a system image package.
        // However, not all systems have a system image, like recovery, which starts pkg-cache with
        // an empty blobfs.
        let paths_to_hashes: Vec<(PackagePath, Hash)> = system_image
            .static_packages()
            .await
            .context("failed to load static packages from system image")?
            .into_contents()
            .chain(std::iter::once((
                PackagePath::from_name_and_variant(
                    SYSTEM_IMAGE_NAME.parse().unwrap(),
                    SYSTEM_IMAGE_VARIANT.parse().unwrap(),
                ),
                *system_image.hash(),
            )))
            .collect();

        match Self::load_base_blobs(blobfs, paths_to_hashes.iter().map(|p| p.1)).await {
            Ok(blobs) => Ok(Self {
                base_blobs: BaseBlobs::new(blobs, node.create_child("base-blobs")),
                paths_to_hashes,
                node,
            }),
            Err(e) => Err(anyhow!(e).context("Error determining base blobs")),
        }
    }

    /// Create an empty `BasePackages`, i.e. a `BasePackages` that does not have any packages (and
    /// therefore does not have any blobs). Useful for when there is no system_image package.
    pub fn empty(node: finspect::Node) -> Self {
        Self {
            base_blobs: BaseBlobs::new(HashSet::new(), node.create_child("base-blobs")),
            paths_to_hashes: vec![],
            node,
        }
    }

    pub fn list_blobs(&self) -> &HashSet<Hash> {
        &self.base_blobs.blobs
    }

    async fn load_base_blobs(
        blobfs: &blobfs::Client,
        base_package_hashes: impl Iterator<Item = Hash>,
    ) -> Result<HashSet<Hash>, Error> {
        let mut futures =
            futures::stream::iter(base_package_hashes.map(|p| Self::package_blobs(blobfs, p)))
                .buffer_unordered(1000);

        let mut ret = HashSet::new();
        while let Some(p) = futures.try_next().await? {
            ret.extend(p);
        }

        Ok(ret)
    }

    // Return all blobs that make up `package`, including the meta.far.
    async fn package_blobs(
        blobfs: &blobfs::Client,
        package: Hash,
    ) -> Result<impl Iterator<Item = Hash>, Error> {
        let package_dir = package_directory::RootDir::new(blobfs.clone(), package)
            .await
            .with_context(|| format!("making RootDir for {}", package))?;
        let external_hashes = package_dir.external_file_hashes().copied().collect::<Vec<_>>();
        Ok(std::iter::once(package.clone()).chain(external_hashes))
    }

    /// Iterator over the mapping of package paths to hashes.
    pub fn paths_to_hashes(
        &self,
    ) -> impl Iterator<Item = &(PackagePath, Hash)> + ExactSizeIterator {
        self.paths_to_hashes.iter()
    }

    // TODO(fxbug.dev/43635) use this function, remove allow
    #[allow(dead_code)]
    pub fn is_blob_in_base(&self, hash: &Hash) -> bool {
        self.base_blobs.blobs.contains(hash)
    }

    /// Test-only constructor to allow testing with this type without constructing a pkgfs.
    #[cfg(test)]
    pub(crate) fn new_test_only(
        blobs: HashSet<Hash>,
        paths_to_hashes: Vec<(PackagePath, Hash)>,
    ) -> Self {
        let inspector = finspect::Inspector::new();
        Self {
            base_blobs: BaseBlobs::new(blobs, inspector.root().clone_weak()),
            paths_to_hashes,
            node: inspector.root().clone_weak(),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, fuchsia_inspect::assert_data_tree, fuchsia_pkg::PackagePath,
        fuchsia_pkg_testing::PackageBuilder, maplit::hashset,
    };

    struct TestEnv {
        _blobfs_fake: blobfs::TempDirFake,
        system_image: fuchsia_pkg_testing::Package,
        inspector: finspect::types::Inspector,
    }

    impl TestEnv {
        async fn new(static_packages: &[&fuchsia_pkg_testing::Package]) -> (Self, BasePackages) {
            let (blobfs_client, blobfs_fake) = blobfs::Client::new_temp_dir_fake();
            let blobfs_dir = blobfs_fake.backing_dir_as_openat_dir();
            for p in static_packages.iter() {
                p.write_to_blobfs_dir(&blobfs_dir);
            }

            let system_image = fuchsia_pkg_testing::SystemImageBuilder::new()
                .static_packages(static_packages)
                .build()
                .await;
            system_image.write_to_blobfs_dir(&blobfs_dir);

            let inspector = finspect::Inspector::new();

            let base_packages = BasePackages::new(
                &blobfs_client,
                &system_image::SystemImage::from_root_dir(
                    package_directory::RootDir::new(
                        blobfs_client.clone(),
                        *system_image.meta_far_merkle_root(),
                    )
                    .await
                    .unwrap(),
                ),
                inspector.root().create_child("base-packages"),
            )
            .await
            .unwrap();

            (Self { _blobfs_fake: blobfs_fake, system_image, inspector }, base_packages)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn identifies_all_blobs() {
        let a_base_package = PackageBuilder::new("a-base-package")
            .add_resource_at("a-base-blob", &b"a-base-blob-contents"[..])
            .build()
            .await
            .unwrap();
        let a_base_package_blobs = a_base_package.list_blobs().unwrap();
        let (env, base_packages) = TestEnv::new(&[&a_base_package]).await;

        let expected_blobs = env
            .system_image
            .list_blobs()
            .unwrap()
            .into_iter()
            .chain(a_base_package_blobs.into_iter())
            .collect();
        assert_eq!(base_packages.list_blobs(), &expected_blobs);

        for blob in expected_blobs.iter() {
            assert!(base_packages.is_blob_in_base(blob));
        }

        assert_data_tree!(env.inspector, root: {
            "base-packages": {
                "base-blobs": {
                    "count": 4u64,
                }
            }
        });
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn inspect_correct_blob_count_shared_blob() {
        let a_base_package0 = PackageBuilder::new("a-base-package0")
            .add_resource_at("a-base-blob0", &b"duplicate-blob-contents"[..])
            .build()
            .await
            .unwrap();
        let a_base_package1 = PackageBuilder::new("a-base-package1")
            .add_resource_at("a-base-blob1", &b"duplicate-blob-contents"[..])
            .build()
            .await
            .unwrap();
        let (env, _base_packages) = TestEnv::new(&[&a_base_package0, &a_base_package1]).await;

        // Expect 5 blobs:
        //   * system_image meta.far
        //   * system_image data/static_packages
        //   * a-base-package0 meta.far
        //   * a-base-package0 a-base-blob0
        //   * a-base-package1 meta.far -> differs with a-base-package0 meta.far because
        //       meta/package and meta/contents differ
        //   * a-base-package1 a-base-blob1 -> duplicate of a-base-package0 a-base-blob0
        assert_data_tree!(env.inspector, root: {
            "base-packages": {
                "base-blobs": {
                    "count": 5u64,
                }
            }
        });
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn paths_to_hashes_includes_system_image() {
        let a_base_package = PackageBuilder::new("a-base-package")
            .add_resource_at("a-base-blob", &b"a-base-blob-contents"[..])
            .build()
            .await
            .unwrap();
        let a_base_package_hash = *a_base_package.meta_far_merkle_root();
        let (env, base_packages) = TestEnv::new(&[&a_base_package]).await;

        assert_eq!(
            base_packages.paths_to_hashes().cloned().collect::<HashSet<(PackagePath, Hash)>>(),
            hashset! {
                ("system_image/0".parse().unwrap(), *env.system_image.meta_far_merkle_root()),
                ("a-base-package/0".parse().unwrap(), a_base_package_hash)
            }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn paths_to_hashes_includes_system_image_even_if_no_static_packages() {
        let (env, base_packages) = TestEnv::new(&[]).await;

        assert_eq!(
            base_packages.paths_to_hashes().cloned().collect::<HashSet<(PackagePath, Hash)>>(),
            hashset! {
                ("system_image/0".parse().unwrap(), *env.system_image.meta_far_merkle_root()),
            }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn base_packages_fails_when_loading_fails() {
        let (blobfs_client, blobfs_fake) = blobfs::Client::new_temp_dir_fake();
        let blobfs_dir = blobfs_fake.backing_dir_as_openat_dir();
        // system_image package has no data/static_packages file
        let system_image = PackageBuilder::new("system_image").build().await.unwrap();
        system_image.write_to_blobfs_dir(&blobfs_dir);

        let inspector = finspect::Inspector::new();

        let base_packages_res = BasePackages::new(
            &blobfs_client,
            &system_image::SystemImage::from_root_dir(
                package_directory::RootDir::new(
                    blobfs_client.clone(),
                    *system_image.meta_far_merkle_root(),
                )
                .await
                .unwrap(),
            ),
            inspector.root().create_child("base-packages"),
        )
        .await;

        assert!(base_packages_res.is_err());
        assert_data_tree!(inspector, root: {});
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn empty_inspect() {
        let inspector = finspect::Inspector::new();
        let _base_packages = BasePackages::empty(inspector.root().create_child("base-packages"));

        assert_data_tree!(inspector, root: {
            "base-packages": {
                "base-blobs": {
                    "count": 0u64,
                }
            }
        });
    }
}
