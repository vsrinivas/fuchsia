// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use {
    anyhow::{Context as _, Error},
    fuchsia_merkle::Hash,
    fuchsia_pkg::PackagePath,
    pkgfs::{system::Client as SystemImage, versions::Client as Versions},
    std::collections::{HashMap, HashSet},
    system_image::CachePackages,
};

#[derive(Debug, Default)]
pub struct DynamicIndex {
    /// map of package hash to package name, variant and set of blob hashes it needs.
    packages: HashMap<Hash, Package>,
    /// map of package path to most recently activated package hash.
    active_packages: HashMap<PackagePath, Hash>,
    /// map of hashes of missing blobs to a set of package hashes that need that blob.
    needed_blobs: HashMap<Hash, HashSet<Hash>>,
}

impl DynamicIndex {
    pub fn new() -> Self {
        Self::default()
    }

    // Add the given package to the dynamic index.
    fn add_package(&mut self, hash: Hash, package: Package) {
        match &package {
            Package::Pending => {}
            Package::WithMetaFar { missing_blobs, .. } => {
                for &blob in missing_blobs {
                    self.needed_blobs.entry(blob).or_default().insert(hash);
                }
            }
            Package::Active { path, .. } => {
                if let Some(previous_package) = self.active_packages.insert(path.clone(), hash) {
                    self.packages.remove(&previous_package);
                }
            }
        };
        self.packages.insert(hash, package);
    }

    /// Returns all blobs protected by the dynamic index.
    pub fn all_blobs(&self) -> HashSet<Hash> {
        self.packages
            .iter()
            .flat_map(|(hash, package)| {
                let blobs = match package {
                    Package::Pending => None,
                    Package::WithMetaFar { required_blobs, .. } => Some(required_blobs.iter()),
                    Package::Active { required_blobs, .. } => Some(required_blobs.iter()),
                };
                std::iter::once(hash).chain(blobs.into_iter().flatten())
            })
            .cloned()
            .collect()
    }
}

// Load present cache packages into the dynamic index.
pub async fn load_cache_packages(
    index: &mut DynamicIndex,
    system_image: &SystemImage,
    versions: &Versions,
) -> Result<(), Error> {
    let cache_packages = CachePackages::deserialize(
        system_image
            .open_file("data/cache_packages")
            .await
            .context("open system_image data/cache_packages")?,
    )?;

    for (path, package_hash) in cache_packages.into_contents() {
        let package = match versions.open_package(&package_hash).await {
            Ok(package) => package,
            Err(pkgfs::package::OpenError::NotFound) => continue,
            Err(err) => {
                return Err(err)
                    .with_context(|| format!("error opening package of {}", package_hash))
            }
        };
        let required_blobs = package
            .blobs()
            .await
            .with_context(|| format!("error reading package blobs of {}", package_hash))?
            .collect();

        index.add_package(package_hash, Package::Active { path, required_blobs });
    }
    Ok(())
}

#[derive(Debug, Eq, PartialEq)]
enum Package {
    /// Only the package hash is known, meta.far isn't available yet.
    Pending,
    /// We have the meta.far
    WithMetaFar {
        /// The name and variant of the package.
        path: PackagePath,
        /// Subset of `required_blobs` that does not exist in blobfs.
        missing_blobs: HashSet<Hash>,
        /// Set of blobs this package depends on, does not include meta.far blob itself.
        required_blobs: HashSet<Hash>,
    },
    /// All blobs are present and the package is activated.
    Active {
        /// The name and variant of the package.
        path: PackagePath,
        /// Set of blobs this package depends on, does not include meta.far blob itself.
        required_blobs: HashSet<Hash>,
    },
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_io::DirectoryProxy,
        fuchsia_async as fasync,
        fuchsia_pkg::MetaContents,
        maplit::{btreemap, hashmap, hashset},
        std::{
            collections::HashMap,
            fs::{create_dir, create_dir_all, File},
        },
        tempfile::TempDir,
    };

    #[test]
    fn test_all_blobs() {
        let mut dynamic_index = DynamicIndex::new();
        assert_eq!(dynamic_index.all_blobs(), HashSet::new());

        dynamic_index.add_package(Hash::from([1; 32]), Package::Pending);
        dynamic_index.add_package(
            Hash::from([2; 32]),
            Package::WithMetaFar {
                path: PackagePath::from_name_and_variant("fake-package", "0").unwrap(),
                missing_blobs: hashset! { Hash::from([3; 32]) },
                required_blobs: hashset! { Hash::from([3; 32]), Hash::from([4; 32]) },
            },
        );
        dynamic_index.add_package(
            Hash::from([5; 32]),
            Package::Active {
                path: PackagePath::from_name_and_variant("fake-package-2", "0").unwrap(),
                required_blobs: hashset! { Hash::from([6; 32]) },
            },
        );
        assert_eq!(
            dynamic_index.all_blobs(),
            hashset! {
                Hash::from([1; 32]),
                Hash::from([2; 32]),
                Hash::from([3; 32]),
                Hash::from([4; 32]),
                Hash::from([5; 32]),
                Hash::from([6; 32]),
            }
        );
    }

    struct TestPkgfs {
        pkgfs_root: TempDir,
    }

    impl TestPkgfs {
        fn new(
            cache_packages: &CachePackages,
            versions_contents: &HashMap<Hash, MetaContents>,
        ) -> Self {
            let pkgfs_root = TempDir::new().unwrap();
            create_dir_all(pkgfs_root.path().join("system/data")).unwrap();
            cache_packages
                .serialize(
                    File::create(pkgfs_root.path().join("system/data/cache_packages")).unwrap(),
                )
                .unwrap();

            create_dir(pkgfs_root.path().join("versions")).unwrap();
            for (hash, contents) in versions_contents {
                let meta_path = pkgfs_root.path().join(format!("versions/{}/meta", hash));
                create_dir_all(&meta_path).unwrap();
                contents.serialize(&mut File::create(meta_path.join("contents")).unwrap()).unwrap();
            }

            Self { pkgfs_root }
        }

        fn root_proxy(&self) -> DirectoryProxy {
            DirectoryProxy::new(
                fuchsia_async::Channel::from_channel(
                    fdio::transfer_fd(File::open(self.pkgfs_root.path()).unwrap()).unwrap().into(),
                )
                .unwrap(),
            )
        }

        fn system_image(&self) -> SystemImage {
            SystemImage::open_from_pkgfs_root(&self.root_proxy()).unwrap()
        }

        fn versions(&self) -> Versions {
            Versions::open_from_pkgfs_root(&self.root_proxy()).unwrap()
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_cache_packages() {
        let fake_package_hash = Hash::from([1; 32]);
        let fake_package_path = PackagePath::from_name_and_variant("fake-package", "0").unwrap();
        let not_present_package_hash = Hash::from([2; 32]);
        let not_present_package_path =
            PackagePath::from_name_and_variant("not-present-package", "0").unwrap();
        let share_blob_package_hash = Hash::from([3; 32]);
        let share_blob_package_path =
            PackagePath::from_name_and_variant("share-blob-package", "1").unwrap();
        let cache_packages = CachePackages::from_entries(vec![
            (fake_package_path.clone(), fake_package_hash),
            (not_present_package_path, not_present_package_hash),
            (share_blob_package_path.clone(), share_blob_package_hash),
        ]);
        let some_blob_hash = Hash::from([4; 32]);
        let other_blob_hash = Hash::from([5; 32]);
        let yet_another_blob_hash = Hash::from([6; 32]);
        let versions_contents = hashmap! {
            fake_package_hash =>
                MetaContents::from_map(
                    btreemap! {
                        "some-blob".to_string() => some_blob_hash,
                        "other-blob".to_string() => other_blob_hash
                    }
                ).unwrap(),
            share_blob_package_hash =>
                MetaContents::from_map(
                    btreemap! {
                        "some-blob".to_string() => some_blob_hash,
                        "yet-another-blob".to_string() => yet_another_blob_hash
                    }
                ).unwrap()
        };
        let pkgfs = TestPkgfs::new(&cache_packages, &versions_contents);

        let mut dynamic_index = DynamicIndex::new();
        load_cache_packages(&mut dynamic_index, &pkgfs.system_image(), &pkgfs.versions())
            .await
            .unwrap();

        let fake_package = Package::Active {
            path: fake_package_path.clone(),
            required_blobs: hashset! { some_blob_hash, other_blob_hash },
        };
        let share_blob_package = Package::Active {
            path: share_blob_package_path.clone(),
            required_blobs: hashset! { some_blob_hash, yet_another_blob_hash },
        };

        assert_eq!(
            dynamic_index.packages,
            hashmap! {
                fake_package_hash => fake_package,
                share_blob_package_hash => share_blob_package
            }
        );
        assert_eq!(
            dynamic_index.active_packages,
            hashmap! {
                fake_package_path => fake_package_hash,
                share_blob_package_path => share_blob_package_hash
            }
        );
        assert_eq!(dynamic_index.needed_blobs, hashmap! {});
        assert_eq!(
            dynamic_index.all_blobs(),
            hashset! {
                fake_package_hash,
                share_blob_package_hash,
                some_blob_hash,
                other_blob_hash,
                yet_another_blob_hash,
            }
        );
    }
}
