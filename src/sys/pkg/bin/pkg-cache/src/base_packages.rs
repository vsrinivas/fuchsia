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
    std::collections::{HashMap, HashSet},
};

const SYSTEM_IMAGE_NAME: &str = "system_image";
const SYSTEM_IMAGE_VARIANT: &str = "0";

/// BasePackages represents the set of packages in the static_packages list, plus the system_image
/// package.
#[derive(Debug)]
pub struct BasePackages {
    base_blobs: BaseBlobs,
    hashes_to_paths: HashMap<Hash, Vec<PackagePath>>,
    #[allow(unused)]
    node: finspect::Node,
}

/// All of the base blobs.
/// If the system is configured to have a system_image package, then:
///   1. the meta.far and content blobs of the system_image package
///   2. the meta.fars and content blobs of all the static_packages.
/// Otherwise, empty.
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
        let paths_and_hashes = system_image
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
            )));

        let mut hashes_to_paths =
            HashMap::<Hash, Vec<PackagePath>>::with_capacity(paths_and_hashes.size_hint().0);
        for (path, hash) in paths_and_hashes {
            hashes_to_paths.entry(hash).or_default().push(path)
        }

        match Self::load_base_blobs(blobfs, hashes_to_paths.iter().map(|p| *p.0)).await {
            Ok(blobs) => Ok(Self {
                base_blobs: BaseBlobs::new(blobs, node.create_child("base-blobs")),
                hashes_to_paths,
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
            hashes_to_paths: HashMap::new(),
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
        Ok(std::iter::once(package).chain(external_hashes))
    }

    /// Returns `true` iff `pkg` is the hash of a base package.
    pub fn is_base_package(&self, pkg: Hash) -> bool {
        self.hashes_to_paths.contains_key(&pkg)
    }

    /// Iterator over tuples of the base package paths and hashes. Iteration order is arbitrary.
    pub fn paths_and_hashes(&self) -> impl Iterator<Item = (&PackagePath, &Hash)> {
        self.hashes_to_paths.iter().flat_map(|(hash, paths)| paths.iter().map(move |p| (p, hash)))
    }

    /// Test-only constructor to allow testing with this type without constructing a pkgfs.
    #[cfg(test)]
    pub(crate) fn new_test_only(
        blobs: HashSet<Hash>,
        paths_and_hashes: impl IntoIterator<Item = (PackagePath, Hash)>,
    ) -> Self {
        let paths_and_hashes = paths_and_hashes.into_iter();
        let mut hashes_to_paths =
            HashMap::<Hash, Vec<PackagePath>>::with_capacity(paths_and_hashes.size_hint().0);
        for (path, hash) in paths_and_hashes {
            hashes_to_paths.entry(hash).or_default().push(path)
        }

        let inspector = finspect::Inspector::new();
        Self {
            base_blobs: BaseBlobs::new(blobs, inspector.root().clone_weak()),
            hashes_to_paths,
            node: inspector.root().clone_weak(),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, fuchsia_inspect::assert_data_tree, fuchsia_pkg_testing::PackageBuilder,
        std::iter::FromIterator as _,
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
    async fn paths_and_hashes_includes_system_image() {
        let a_base_package = PackageBuilder::new("a-base-package")
            .add_resource_at("a-base-blob", &b"a-base-blob-contents"[..])
            .build()
            .await
            .unwrap();
        let a_base_package_hash = *a_base_package.meta_far_merkle_root();
        let (env, base_packages) = TestEnv::new(&[&a_base_package]).await;

        assert_eq!(
            base_packages.paths_and_hashes().map(|(p, h)| (p.clone(), *h)).collect::<HashSet<_>>(),
            HashSet::from_iter([
                ("system_image/0".parse().unwrap(), *env.system_image.meta_far_merkle_root()),
                ("a-base-package/0".parse().unwrap(), a_base_package_hash),
            ])
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn paths_and_hashes_includes_system_image_even_if_no_static_packages() {
        let (env, base_packages) = TestEnv::new(&[]).await;

        assert_eq!(
            base_packages.paths_and_hashes().map(|(p, h)| (p.clone(), *h)).collect::<HashSet<_>>(),
            HashSet::from_iter([(
                "system_image/0".parse().unwrap(),
                *env.system_image.meta_far_merkle_root()
            ),])
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn is_base_package() {
        let (env, base_packages) = TestEnv::new(&[]).await;
        let system_image = *env.system_image.meta_far_merkle_root();
        let mut not_system_image = Into::<[u8; 32]>::into(system_image);
        not_system_image[0] = !not_system_image[0];
        let not_system_image = Hash::from(not_system_image);

        assert!(base_packages.is_base_package(system_image));
        assert!(!base_packages.is_base_package(not_system_image));
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
