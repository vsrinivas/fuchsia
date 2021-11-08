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
    pkgfs::versions::Client as Versions,
    std::collections::HashSet,
    system_image::StaticPackages,
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
        versions: &Versions,
        static_packages: StaticPackages,
        system_image_hash: &Hash,
        node: finspect::Node,
    ) -> Result<Self, anyhow::Error> {
        // Add the system image package to the set of static packages to create the set of base
        // packages. If we're constructing BasePackages, we must have a system image package.
        // However, not all systems have a system image, like recovery, which starts pkg-cache with
        // an empty blobfs.
        let paths_to_hashes: Vec<(PackagePath, Hash)> = static_packages
            .into_contents()
            .chain(std::iter::once((
                PackagePath::from_name_and_variant(
                    SYSTEM_IMAGE_NAME.parse().unwrap(),
                    SYSTEM_IMAGE_VARIANT.parse().unwrap(),
                ),
                system_image_hash.clone(),
            )))
            .collect();

        match Self::load_base_blobs(versions, paths_to_hashes.iter().map(|p| p.1)).await {
            Ok(blobs) => Ok(Self {
                base_blobs: BaseBlobs::new(blobs, node.create_child("base-blobs")),
                paths_to_hashes,
                node,
            }),
            Err(e) => Err(anyhow!(e).context("Error determining base blobs")),
        }
    }

    pub fn list_blobs(&self) -> &HashSet<Hash> {
        &self.base_blobs.blobs
    }

    async fn load_base_blobs(
        versions: &Versions,
        base_package_hashes: impl Iterator<Item = Hash>,
    ) -> Result<HashSet<Hash>, Error> {
        let mut futures =
            futures::stream::iter(base_package_hashes.map(|p| Self::package_blobs(versions, p)))
                .buffer_unordered(1000);

        let mut ret = HashSet::new();
        while let Some(p) = futures.try_next().await? {
            ret.extend(p);
        }

        Ok(ret)
    }

    // Return all blobs that make up `package`, including the meta.far.
    async fn package_blobs(
        versions: &Versions,
        package: Hash,
    ) -> Result<impl Iterator<Item = Hash>, Error> {
        let package_dir = versions
            .open_package(&package)
            .await
            .with_context(|| format!("failing to open package: {}", package))?;
        let blobs = package_dir
            .blobs()
            .await
            .with_context(|| format!("error reading package blobs of {}", package))?;

        Ok(std::iter::once(package.clone()).chain(blobs))
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
        node: finspect::Node,
    ) -> Self {
        Self {
            base_blobs: BaseBlobs::new(blobs, node.create_child("base-blobs")),
            paths_to_hashes,
            node,
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_inspect::assert_data_tree,
        fuchsia_pkg::{MetaContents, PackagePath},
        maplit::hashmap,
        std::{
            collections::HashMap,
            fs::{create_dir, create_dir_all, File},
            io::Write as _,
        },
        system_image::StaticPackages,
        tempfile::TempDir,
    };

    struct TestPkgfs {
        pkgfs_root: TempDir,
    }

    impl TestPkgfs {
        fn new(system_image_hash: &Hash, versions_contents: &HashMap<Hash, MetaContents>) -> Self {
            let pkgfs_root = TempDir::new().unwrap();
            create_dir(pkgfs_root.path().join("system")).unwrap();
            File::create(pkgfs_root.path().join("system/meta"))
                .unwrap()
                .write_all(system_image_hash.to_string().as_bytes())
                .unwrap();

            create_dir(pkgfs_root.path().join("versions")).unwrap();
            for (hash, contents) in versions_contents.iter() {
                let meta_path = pkgfs_root.path().join(format!("versions/{}/meta", hash));
                create_dir_all(&meta_path).unwrap();
                contents.serialize(&mut File::create(meta_path.join("contents")).unwrap()).unwrap();
            }

            Self { pkgfs_root }
        }

        fn versions(&self) -> Versions {
            Versions::open_from_pkgfs_root(&fidl_fuchsia_io::DirectoryProxy::new(
                fuchsia_async::Channel::from_channel(
                    fdio::transfer_fd(File::open(self.pkgfs_root.path()).unwrap()).unwrap().into(),
                )
                .unwrap(),
            ))
            .unwrap()
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn inspect_correct_blob_count() {
        let system_image_hash: Hash =
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap();
        let fake_package_hash: Hash =
            "1111111111111111111111111111111111111111111111111111111111111111".parse().unwrap();
        let static_packages = StaticPackages::from_entries(vec![(
            PackagePath::from_name_and_variant(
                "fake-package".parse().unwrap(),
                "0".parse().unwrap(),
            ),
            fake_package_hash,
        )]);

        let versions_contents = hashmap! {
            system_image_hash.clone() => MetaContents::from_map(
                hashmap! {
                    "some-blob".to_string() =>
                        "2222222222222222222222222222222222222222222222222222222222222222".parse().unwrap()
                }
            ).unwrap(),
            fake_package_hash.clone() => MetaContents::from_map(
                hashmap! {
                    "other-blob".to_string() =>
                        "3333333333333333333333333333333333333333333333333333333333333333".parse().unwrap()
                }
            ).unwrap()
        };
        let env = TestPkgfs::new(&system_image_hash, &versions_contents);
        let inspector = finspect::Inspector::new();

        let _base_packages = BasePackages::new(
            &env.versions(),
            static_packages,
            &system_image_hash,
            inspector.root().create_child("base-packages"),
        )
        .await;

        assert_data_tree!(inspector, root: {
            "base-packages": {
                "base-blobs": {
                    count: 4u64,
                }
            }
        });
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn inspect_correct_blob_count_shared_blob() {
        let system_image_hash: Hash =
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap();
        let fake_package_hash: Hash =
            "1111111111111111111111111111111111111111111111111111111111111111".parse().unwrap();
        let static_packages = StaticPackages::from_entries(vec![(
            PackagePath::from_name_and_variant(
                "fake-package".parse().unwrap(),
                "0".parse().unwrap(),
            ),
            fake_package_hash,
        )]);

        let versions_contents = hashmap! {
            system_image_hash.clone() => MetaContents::from_map(
                hashmap! {
                    "shared-blob".to_string() =>
                        "2222222222222222222222222222222222222222222222222222222222222222".parse().unwrap()
                }
            ).unwrap(),
            fake_package_hash.clone() => MetaContents::from_map(
                hashmap! {
                    "secretly-the-same-blob".to_string() =>
                        "2222222222222222222222222222222222222222222222222222222222222222".parse().unwrap()
                }
            ).unwrap()
        };
        let env = TestPkgfs::new(&system_image_hash, &versions_contents);
        let inspector = finspect::Inspector::new();

        let _base_packages = BasePackages::new(
            &env.versions(),
            static_packages,
            &system_image_hash,
            inspector.root().create_child("base-packages"),
        )
        .await;

        assert_data_tree!(inspector, root: {
            "base-packages": {
                "base-blobs": {
                    count: 3u64,
                }
            }
        });
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn base_packages_fails_when_loading_fails() {
        let system_image_hash: Hash =
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap();
        let static_packages = StaticPackages::from_entries(vec![]);
        // system_image not in versions, so loading will fail
        let versions_contents = HashMap::new();
        let env = TestPkgfs::new(&system_image_hash, &versions_contents);
        let inspector = finspect::Inspector::new();

        let base_packages_result = BasePackages::new(
            &env.versions(),
            static_packages,
            &system_image_hash,
            inspector.root().create_child("base-packages"),
        )
        .await;

        assert!(base_packages_result.is_err());

        assert_data_tree!(inspector, root: {});
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn is_blob_in_base_when_loading_succeeds() {
        let system_image_hash: Hash =
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap();
        let fake_package_hash: Hash =
            "1111111111111111111111111111111111111111111111111111111111111111".parse().unwrap();
        let static_packages = StaticPackages::from_entries(vec![(
            PackagePath::from_name_and_variant(
                "fake-package".parse().unwrap(),
                "0".parse().unwrap(),
            ),
            fake_package_hash,
        )]);

        let some_blob_hash =
            "2222222222222222222222222222222222222222222222222222222222222222".parse().unwrap();
        let other_blob_hash =
            "3333333333333333333333333333333333333333333333333333333333333333".parse().unwrap();
        let versions_contents = hashmap! {
            system_image_hash.clone() => MetaContents::from_map(
                hashmap! {
                    "some-blob".to_string() => some_blob_hash
                }
            ).unwrap(),
            fake_package_hash.clone() => MetaContents::from_map(
                hashmap! {
                    "other-blob".to_string() => other_blob_hash
                }
            ).unwrap()
        };
        let env = TestPkgfs::new(&system_image_hash, &versions_contents);

        let base_packages = BasePackages::new(
            &env.versions(),
            static_packages,
            &system_image_hash,
            finspect::Inspector::new().root().create_child("base-packages"),
        )
        .await
        .unwrap();

        assert_eq!(base_packages.is_blob_in_base(&system_image_hash), true);
        assert_eq!(base_packages.is_blob_in_base(&fake_package_hash), true);
        assert_eq!(base_packages.is_blob_in_base(&some_blob_hash), true);
        assert_eq!(base_packages.is_blob_in_base(&other_blob_hash), true);
        assert_eq!(
            base_packages.is_blob_in_base(
                &"4444444444444444444444444444444444444444444444444444444444444444"
                    .parse()
                    .unwrap(),
            ),
            false
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn paths_to_hashes_includes_system_image() {
        let system_image_hash: Hash =
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap();
        let fake_package_hash: Hash =
            "1111111111111111111111111111111111111111111111111111111111111111".parse().unwrap();
        let fake_package_path = PackagePath::from_name_and_variant(
            "fake-package".parse().unwrap(),
            "0".parse().unwrap(),
        );
        let static_packages =
            StaticPackages::from_entries(vec![(fake_package_path.clone(), fake_package_hash)]);

        let some_blob_hash =
            "2222222222222222222222222222222222222222222222222222222222222222".parse().unwrap();
        let other_blob_hash =
            "3333333333333333333333333333333333333333333333333333333333333333".parse().unwrap();
        let versions_contents = hashmap! {
            system_image_hash.clone() => MetaContents::from_map(
                hashmap! {
                    "some-blob".to_string() => some_blob_hash
                }
            ).unwrap(),
            fake_package_hash.clone() => MetaContents::from_map(
                hashmap! {
                    "other-blob".to_string() => other_blob_hash
                }
            ).unwrap()
        };
        let env = TestPkgfs::new(&system_image_hash, &versions_contents);

        let base_packages = BasePackages::new(
            &env.versions(),
            static_packages,
            &system_image_hash,
            finspect::Inspector::new().root().create_child("base-packages"),
        )
        .await
        .unwrap();

        let system_image_path = PackagePath::from_name_and_variant(
            "system_image".parse().unwrap(),
            "0".parse().unwrap(),
        );

        assert_eq!(
            base_packages.paths_to_hashes().cloned().collect::<Vec<(PackagePath, Hash)>>(),
            vec![(fake_package_path, fake_package_hash), (system_image_path, system_image_hash)]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn paths_to_hashes_includes_system_image_even_if_no_static_packages() {
        let static_packages = StaticPackages::from_entries(vec![]);

        let system_image_path = PackagePath::from_name_and_variant(
            "system_image".parse().unwrap(),
            "0".parse().unwrap(),
        );
        let system_image_hash: Hash =
            "1111111111111111111111111111111111111111111111111111111111111111".parse().unwrap();

        let some_blob_hash =
            "2222222222222222222222222222222222222222222222222222222222222222".parse().unwrap();
        let versions_contents = hashmap! {
            system_image_hash.clone() => MetaContents::from_map(
                hashmap! {
                    "some-blob".to_string() => some_blob_hash
                }
            ).unwrap(),
        };
        let env = TestPkgfs::new(&system_image_hash, &versions_contents);
        let inspector = finspect::Inspector::new();

        let base_packages_result = BasePackages::new(
            &env.versions(),
            static_packages,
            &system_image_hash,
            inspector.root().create_child("base-packages"),
        )
        .await;

        assert_eq!(
            base_packages_result
                .unwrap()
                .paths_to_hashes()
                .cloned()
                .collect::<Vec<(PackagePath, Hash)>>(),
            vec![(system_image_path, system_image_hash)]
        );
    }
}
