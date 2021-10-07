// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _, Error},
    fuchsia_inspect as finspect,
    fuchsia_inspect_contrib::inspectable::InspectableLen,
    fuchsia_merkle::Hash,
    futures::{StreamExt, TryStreamExt},
    pkgfs::{system::Client as SystemImage, versions::Client as Versions},
    std::{collections::HashSet, io::Read as _},
    system_image::StaticPackages,
};

#[derive(Debug)]
pub(crate) struct BlobLocation {
    base_blobs: BaseBlobs,
    // TODO(fxbug.dev/84729)
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

impl BlobLocation {
    pub async fn new(
        system_image: &SystemImage,
        versions: &Versions,
        node: finspect::Node,
    ) -> Result<Self, anyhow::Error> {
        match Self::load_base_blobs(system_image, versions).await {
            Ok(blobs) => Ok(Self {
                base_blobs: BaseBlobs::new(blobs, node.create_child("base-blobs")),
                node,
            }),
            Err(e) => Err(anyhow!(e).context("Error determining base blobs")),
        }
    }

    pub fn list_blobs(&self) -> &HashSet<Hash> {
        &self.base_blobs.blobs
    }

    async fn load_base_blobs(
        system_image: &SystemImage,
        versions: &Versions,
    ) -> Result<HashSet<Hash>, Error> {
        let system_image_meta_file =
            system_image.open_file("meta").await.context("error opening system_image meta")?;
        let mut system_image_hash = String::new();
        (&system_image_meta_file)
            .read_to_string(&mut system_image_hash)
            .context("error reading system_image hash")?;
        let system_image_hash = system_image_hash.parse().context("system_image hash invalid")?;

        let static_packages = StaticPackages::deserialize(
            system_image
                .open_file("data/static_packages")
                .await
                .context("open system_image data/static_packages")?,
        )?;

        let mut futures = futures::stream::iter(
            std::iter::once(&system_image_hash)
                .chain(static_packages.hashes())
                .map(|p| Self::package_blobs(versions, p)),
        )
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
        package: &Hash,
    ) -> Result<impl Iterator<Item = Hash>, Error> {
        let package_dir = versions
            .open_package(package)
            .await
            .with_context(|| format!("failing to open package: {}", package))?;
        let blobs = package_dir
            .blobs()
            .await
            .with_context(|| format!("error reading package blobs of {}", package))?;

        Ok(std::iter::once(package.clone()).chain(blobs))
    }

    // TODO(fxbug.dev/43635) use this function, remove allow
    #[allow(dead_code)]
    pub fn is_blob_in_base(&self, hash: &Hash) -> bool {
        self.base_blobs.blobs.contains(hash)
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
        tempfile::TempDir,
    };

    struct TestPkgfs {
        pkgfs_root: TempDir,
    }

    impl TestPkgfs {
        fn new(
            system_image_hash: &Hash,
            static_packages: &StaticPackages,
            versions_contents: &HashMap<Hash, MetaContents>,
        ) -> Self {
            let pkgfs_root = TempDir::new().unwrap();
            create_dir(pkgfs_root.path().join("system")).unwrap();
            File::create(pkgfs_root.path().join("system/meta"))
                .unwrap()
                .write_all(system_image_hash.to_string().as_bytes())
                .unwrap();
            create_dir(pkgfs_root.path().join("system/data")).unwrap();
            static_packages
                .serialize(
                    File::create(pkgfs_root.path().join("system/data/static_packages")).unwrap(),
                )
                .unwrap();

            create_dir(pkgfs_root.path().join("versions")).unwrap();
            for (hash, contents) in versions_contents.iter() {
                let meta_path = pkgfs_root.path().join(format!("versions/{}/meta", hash));
                create_dir_all(&meta_path).unwrap();
                contents.serialize(&mut File::create(meta_path.join("contents")).unwrap()).unwrap();
            }

            Self { pkgfs_root }
        }

        fn system_image(&self) -> SystemImage {
            SystemImage::open_from_pkgfs_root(&fidl_fuchsia_io::DirectoryProxy::new(
                fuchsia_async::Channel::from_channel(
                    fdio::transfer_fd(File::open(self.pkgfs_root.path()).unwrap()).unwrap().into(),
                )
                .unwrap(),
            ))
            .unwrap()
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
        let env = TestPkgfs::new(&system_image_hash, &static_packages, &versions_contents);
        let inspector = finspect::Inspector::new();

        let _blob_location = BlobLocation::new(
            &env.system_image(),
            &env.versions(),
            inspector.root().create_child("blob-location"),
        )
        .await;

        assert_data_tree!(inspector, root: {
            "blob-location": {
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
        let env = TestPkgfs::new(&system_image_hash, &static_packages, &versions_contents);
        let inspector = finspect::Inspector::new();

        let _blob_location = BlobLocation::new(
            &env.system_image(),
            &env.versions(),
            inspector.root().create_child("blob-location"),
        )
        .await;

        assert_data_tree!(inspector, root: {
            "blob-location": {
                "base-blobs": {
                    count: 3u64,
                }
            }
        });
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn blob_location_fails_when_loading_fails() {
        let system_image_hash: Hash =
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap();
        let static_packages = StaticPackages::from_entries(vec![]);
        // system_image not in versions, so loading will fail
        let versions_contents = HashMap::new();
        let env = TestPkgfs::new(&system_image_hash, &static_packages, &versions_contents);
        let inspector = finspect::Inspector::new();

        let blob_location_result = BlobLocation::new(
            &env.system_image(),
            &env.versions(),
            inspector.root().create_child("blob-location"),
        )
        .await;

        assert!(blob_location_result.is_err());

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
        let env = TestPkgfs::new(&system_image_hash, &static_packages, &versions_contents);

        let blob_location = BlobLocation::new(
            &env.system_image(),
            &env.versions(),
            finspect::Inspector::new().root().create_child("blob-location"),
        )
        .await
        .unwrap();

        assert_eq!(blob_location.is_blob_in_base(&system_image_hash), true);
        assert_eq!(blob_location.is_blob_in_base(&fake_package_hash), true);
        assert_eq!(blob_location.is_blob_in_base(&some_blob_hash), true);
        assert_eq!(blob_location.is_blob_in_base(&other_blob_hash), true);
        assert_eq!(
            blob_location.is_blob_in_base(
                &"4444444444444444444444444444444444444444444444444444444444444444"
                    .parse()
                    .unwrap(),
            ),
            false
        );
    }
}
