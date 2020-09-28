// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _, Error},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::DirectoryMarker,
    fuchsia_inspect as finspect,
    fuchsia_inspect_contrib::inspectable::InspectableLen,
    fuchsia_merkle::Hash,
    fuchsia_pkg::MetaContents,
    fuchsia_syslog::fx_log_err,
    futures::{stream::FuturesUnordered, TryStreamExt},
    pkgfs::{system::Client as SystemImage, versions::Client as Versions},
    std::{collections::HashSet, io::Read as _},
    system_image::StaticPackages,
};

#[derive(Debug)]
pub(crate) struct BlobLocation {
    inner: Inner,
    node: finspect::Node,
}

#[derive(Debug)]
enum Inner {
    AssumeAllInBase(finspect::Node),
    BaseBlobs(BaseBlobs),
}

#[derive(Debug)]
struct BaseBlobs {
    blobs: InspectableLen<HashSet<Hash>>,
    node: finspect::Node,
}

impl BaseBlobs {
    fn new(blobs: HashSet<Hash>, node: finspect::Node) -> Self {
        Self { blobs: InspectableLen::new(blobs, &node, "count"), node }
    }
}

impl BlobLocation {
    pub async fn new(
        system_image: impl FnOnce() -> Result<SystemImage, Error>,
        versions: impl FnOnce() -> Result<Versions, Error>,
        node: finspect::Node,
    ) -> Self {
        match Self::load_base_blobs(system_image, versions).await {
            Ok(base_blobs) => Self {
                inner: Inner::BaseBlobs(BaseBlobs::new(
                    base_blobs,
                    node.create_child("base-blobs"),
                )),
                node,
            },
            Err(e) => {
                fx_log_err!(
                    "Error determining base blobs, assuming all blobs are in base: {:#}",
                    anyhow!(e)
                );
                Self {
                    inner: Inner::AssumeAllInBase(node.create_child("assume-all-in-base")),
                    node,
                }
            }
        }
    }

    async fn load_base_blobs(
        system_image: impl FnOnce() -> Result<SystemImage, Error>,
        versions: impl FnOnce() -> Result<Versions, Error>,
    ) -> Result<HashSet<Hash>, Error> {
        let system_image = system_image()?;
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

        let versions = versions()?;

        let mut futures = std::iter::once(&system_image_hash)
            .chain(static_packages.hashes())
            .map(|p| Self::package_blobs(&versions, p))
            .collect::<FuturesUnordered<_>>();

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
        let meta_contents = MetaContents::deserialize(
            client_end_to_openat(versions.open_package(package).await?.into_client_end())?
                .open_file("meta/contents")
                .with_context(|| format!("error opening meta/contents of {}", package))?,
        )
        .context(format!("error deserializing meta/contents of {}", package))?;

        Ok(std::iter::once(package.clone())
            .chain(meta_contents.into_contents().into_iter().map(|(_, hash)| hash)))
    }

    // TODO(fxbug.dev/43635) use this function, remove allow
    #[allow(dead_code)]
    pub fn is_blob_in_base(&self, hash: &Hash) -> bool {
        match &self.inner {
            Inner::AssumeAllInBase(..) => true,
            Inner::BaseBlobs(BaseBlobs { blobs, .. }) => blobs.contains(hash),
        }
    }
}

fn client_end_to_openat(client: ClientEnd<DirectoryMarker>) -> Result<openat::Dir, Error> {
    fdio::create_fd(client.into()).context("failed to create fd")
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_inspect::assert_inspect_tree,
        fuchsia_pkg::PackagePath,
        maplit::{btreemap, hashmap},
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
            PackagePath::from_name_and_variant("fake-package", "0").unwrap(),
            fake_package_hash,
        )]);
        let versions_contents = hashmap! {
            system_image_hash.clone() => MetaContents::from_map(
                btreemap! {
                    "some-blob".to_string() =>
                        "2222222222222222222222222222222222222222222222222222222222222222".parse().unwrap()
                }
            ).unwrap(),
            fake_package_hash.clone() => MetaContents::from_map(
                btreemap! {
                    "other-blob".to_string() =>
                        "3333333333333333333333333333333333333333333333333333333333333333".parse().unwrap()
                }
            ).unwrap()
        };
        let env = TestPkgfs::new(&system_image_hash, &static_packages, &versions_contents);
        let inspector = finspect::Inspector::new();

        let _blob_location = BlobLocation::new(
            || Ok(env.system_image()),
            || Ok(env.versions()),
            inspector.root().create_child("blob-location"),
        )
        .await;

        assert_inspect_tree!(inspector, root: {
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
            PackagePath::from_name_and_variant("fake-package", "0").unwrap(),
            fake_package_hash,
        )]);
        let versions_contents = hashmap! {
            system_image_hash.clone() => MetaContents::from_map(
                btreemap! {
                    "shared-blob".to_string() =>
                        "2222222222222222222222222222222222222222222222222222222222222222".parse().unwrap()
                }
            ).unwrap(),
            fake_package_hash.clone() => MetaContents::from_map(
                btreemap! {
                    "secretly-the-same-blob".to_string() =>
                        "2222222222222222222222222222222222222222222222222222222222222222".parse().unwrap()
                }
            ).unwrap()
        };
        let env = TestPkgfs::new(&system_image_hash, &static_packages, &versions_contents);
        let inspector = finspect::Inspector::new();

        let _blob_location = BlobLocation::new(
            || Ok(env.system_image()),
            || Ok(env.versions()),
            inspector.root().create_child("blob-location"),
        )
        .await;

        assert_inspect_tree!(inspector, root: {
            "blob-location": {
                "base-blobs": {
                    count: 3u64,
                }
            }
        });
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn inspect_failure_loading_base_blobs_assume_all_in_base() {
        let system_image_hash: Hash =
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap();
        let static_packages = StaticPackages::from_entries(vec![]);
        // system_image not in versions, so loading will fail
        let versions_contents = HashMap::new();
        let env = TestPkgfs::new(&system_image_hash, &static_packages, &versions_contents);
        let inspector = finspect::Inspector::new();

        let _blob_location = BlobLocation::new(
            || Ok(env.system_image()),
            || Ok(env.versions()),
            inspector.root().create_child("blob-location"),
        )
        .await;

        assert_inspect_tree!(inspector, root: {
            "blob-location": {
                "assume-all-in-base": {}
            }
        });
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn is_blob_in_base_when_loading_succeeds() {
        let system_image_hash: Hash =
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap();
        let fake_package_hash: Hash =
            "1111111111111111111111111111111111111111111111111111111111111111".parse().unwrap();
        let static_packages = StaticPackages::from_entries(vec![(
            PackagePath::from_name_and_variant("fake-package", "0").unwrap(),
            fake_package_hash,
        )]);
        let some_blob_hash =
            "2222222222222222222222222222222222222222222222222222222222222222".parse().unwrap();
        let other_blob_hash =
            "3333333333333333333333333333333333333333333333333333333333333333".parse().unwrap();
        let versions_contents = hashmap! {
            system_image_hash.clone() => MetaContents::from_map(
                btreemap! {
                    "some-blob".to_string() => some_blob_hash
                }
            ).unwrap(),
            fake_package_hash.clone() => MetaContents::from_map(
                btreemap! {
                    "other-blob".to_string() => other_blob_hash
                }
            ).unwrap()
        };
        let env = TestPkgfs::new(&system_image_hash, &static_packages, &versions_contents);

        let blob_location = BlobLocation::new(
            || Ok(env.system_image()),
            || Ok(env.versions()),
            finspect::Inspector::new().root().create_child("blob-location"),
        )
        .await;

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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn is_blob_in_base_when_loading_fails() {
        let system_image_hash: Hash =
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap();
        let static_packages = StaticPackages::from_entries(vec![]);
        // system_image not in versions, so loading will fail
        let versions_contents = HashMap::new();
        let env = TestPkgfs::new(&system_image_hash, &static_packages, &versions_contents);

        let blob_location = BlobLocation::new(
            || Ok(env.system_image()),
            || Ok(env.versions()),
            finspect::Inspector::new().root().create_child("blob-location"),
        )
        .await;

        assert_eq!(blob_location.is_blob_in_base(&system_image_hash), true);
        let non_existent_hash =
            "1111111111111111111111111111111111111111111111111111111111111111".parse().unwrap();
        assert_eq!(blob_location.is_blob_in_base(&non_existent_hash), true);
    }
}
