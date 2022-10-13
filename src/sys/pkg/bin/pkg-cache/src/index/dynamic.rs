// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    fuchsia_inspect as finspect,
    fuchsia_merkle::Hash,
    fuchsia_pkg::{PackageName, PackagePath},
    fuchsia_zircon as zx,
    std::{
        collections::{HashMap, HashSet},
        convert::TryInto,
    },
    system_image::CachePackages,
    tracing::{error, warn},
};

#[derive(Debug, Default)]
pub struct DynamicIndex {
    /// map of package hash to package name, variant and set of blob hashes it needs.
    packages: HashMap<Hash, PackageWithInspect>,
    /// map of package path to most recently activated package hash.
    active_packages: HashMap<PackagePath, Hash>,
    /// The index's root node.
    node: finspect::Node,
}

#[derive(thiserror::Error, Debug)]
#[error("the blob being fulfilled ({hash}) is not needed, dynamic index package state is {state}")]
pub struct FulfillNotNeededBlobError {
    pub hash: Hash,
    pub state: &'static str,
}

#[derive(thiserror::Error, Debug)]
pub enum CompleteInstallError {
    #[error("the package is in an unexpected state: {0:?}")]
    UnexpectedPackageState(Option<Package>),
}

impl DynamicIndex {
    pub fn new(node: finspect::Node) -> Self {
        DynamicIndex { node, ..Default::default() }
    }

    #[cfg(test)]
    pub fn packages(&self) -> HashMap<Hash, Package> {
        let mut package_map = HashMap::new();
        for (key, val) in &self.packages {
            package_map.insert(key.clone(), val.package.clone());
        }
        package_map
    }

    /// Returns a snapshot of all active packages and their hashes.
    pub fn active_packages(&self) -> HashMap<PackagePath, Hash> {
        self.active_packages.clone()
    }

    /// Returns package name if the package is active.
    pub fn get_name_if_active(&self, hash: &Hash) -> Option<&PackageName> {
        self.packages.get(hash).and_then(|p| match p {
            PackageWithInspect { package: Package::Active { path, .. }, .. } => Some(path.name()),
            PackageWithInspect { package: Package::Pending, .. }
            | PackageWithInspect { package: Package::WithMetaFar { .. }, .. } => None,
        })
    }

    fn make_package_node(&mut self, package: &Package, hash: &Hash) -> PackageNode {
        let child_node = self.node.create_child(hash.to_string());
        let package_node = match package {
            Package::Pending => PackageNode::Pending {
                state: child_node.create_string("state", "pending"),
                time: child_node.create_int("time", zx::Time::get_monotonic().into_nanos()),
                node: child_node,
            },
            Package::WithMetaFar { path, required_blobs } => PackageNode::WithMetaFar {
                state: child_node.create_string("state", "with_meta_far"),
                path: child_node.create_string("path", format!("{}", path)),
                time: child_node.create_int("time", zx::Time::get_monotonic().into_nanos()),
                required_blobs: child_node
                    .create_int("required_blobs", required_blobs.len().try_into().unwrap_or(-1)),
                node: child_node,
            },
            Package::Active { path, required_blobs } => PackageNode::Active {
                state: child_node.create_string("state", "active"),
                path: child_node.create_string("path", format!("{}", path)),
                time: child_node.create_int("time", zx::Time::get_monotonic().into_nanos()),
                required_blobs: child_node
                    .create_int("required_blobs", required_blobs.len().try_into().unwrap_or(-1)),
                node: child_node,
            },
        };

        package_node
    }

    // Add the given package to the dynamic index.
    fn add_package(&mut self, hash: Hash, package: Package) {
        match &package {
            Package::Pending => {}
            Package::WithMetaFar { .. } => {}
            Package::Active { path, .. } => {
                if let Some(previous_package) = self.active_packages.insert(path.clone(), hash) {
                    self.packages.remove(&previous_package);
                }
            }
        }

        let package_node = self.make_package_node(&package, &hash);
        self.packages.insert(hash, PackageWithInspect { package, package_node });
    }

    /// Returns the content blobs associated with the given package hash, if known.
    pub fn lookup_content_blobs(&self, package_hash: &Hash) -> Option<&HashSet<Hash>> {
        self.packages.get(package_hash)?.package.content_blobs()
    }

    /// Returns all blobs protected by the dynamic index.
    pub fn all_blobs(&self) -> HashSet<Hash> {
        self.packages
            .iter()
            .flat_map(|(hash, package)| {
                let blobs = match &package.package {
                    Package::Pending => None,
                    Package::WithMetaFar { required_blobs, .. } => Some(required_blobs.iter()),
                    Package::Active { required_blobs, .. } => Some(required_blobs.iter()),
                };
                std::iter::once(hash).chain(blobs.into_iter().flatten())
            })
            .cloned()
            .collect()
    }

    /// Notifies dynamic index that the given package is going to be installed, to keep the meta
    /// far blob protected.
    pub fn start_install(&mut self, package_hash: Hash) {
        let Self { packages, node, .. } = self;
        packages.entry(package_hash).or_insert_with(|| {
            let child_node = node.create_child(package_hash.to_string());
            PackageWithInspect {
                package: Package::Pending,
                package_node: PackageNode::Pending {
                    state: child_node.create_string("state", "pending"),
                    time: child_node.create_int("time", zx::Time::get_monotonic().into_nanos()),
                    node: child_node,
                },
            }
        });
    }

    /// Notifies dynamic index that the given package's meta far is now present in blobfs,
    /// providing the meta far's internal name and set of content blobs it references.
    pub fn fulfill_meta_far(
        &mut self,
        package_hash: Hash,
        package_path: PackagePath,
        required_blobs: HashSet<Hash>,
    ) -> Result<(), FulfillNotNeededBlobError> {
        if let Some(wrong_state) = match self.packages.get(&package_hash).map(|pwi| &pwi.package) {
            Some(Package::Pending) => None,
            None => Some("missing"),
            Some(Package::Active { .. }) => Some("Active"),
            Some(Package::WithMetaFar { .. }) => Some("WithMetaFar"),
        } {
            return Err(FulfillNotNeededBlobError { hash: package_hash, state: wrong_state });
        }

        self.add_package(package_hash, Package::WithMetaFar { path: package_path, required_blobs });

        Ok(())
    }

    /// Notifies dynamic index that the given package has completed installation.
    /// Returns the package's name.
    pub fn complete_install(
        &mut self,
        package_hash: Hash,
    ) -> Result<PackageName, CompleteInstallError> {
        let name = match self.packages.get_mut(&package_hash) {
            Some(PackageWithInspect {
                package: package @ Package::WithMetaFar { .. },
                package_node,
            }) => {
                if let Package::WithMetaFar { path, required_blobs } =
                    std::mem::replace(package, Package::Pending)
                {
                    let child_node = self.node.create_child(&package_hash.to_string());
                    let required_blobs_size = required_blobs.len().try_into().unwrap_or(-1);
                    *package = Package::Active { path: path.clone(), required_blobs };
                    *package_node = PackageNode::Active {
                        state: child_node.create_string("state", "active"),
                        path: child_node.create_string("path", format!("{}", path.clone())),
                        required_blobs: child_node
                            .create_int("required_blobs", required_blobs_size),
                        time: child_node.create_int("time", zx::Time::get_monotonic().into_nanos()),
                        node: child_node,
                    };

                    let name = path.name().clone();
                    if let Some(previous_package) = self.active_packages.insert(path, package_hash)
                    {
                        self.packages.remove(&previous_package);
                    }
                    name
                } else {
                    unreachable!()
                }
            }
            package => {
                return Err(CompleteInstallError::UnexpectedPackageState(
                    package.map(|pwi| pwi.package.to_owned()),
                ));
            }
        };
        Ok(name)
    }

    /// Notifies dynamic index that the given package installation has been canceled.
    pub fn cancel_install(&mut self, package_hash: &Hash) {
        match self.packages.get(package_hash).map(|pwi| &pwi.package) {
            Some(Package::Pending) | Some(Package::WithMetaFar { .. }) => {
                self.packages.remove(package_hash);
            }
            Some(Package::Active { .. }) => {
                warn!("Unable to cancel install for active package {}", package_hash);
            }
            None => {
                error!("Unable to cancel install for unknown package {}", package_hash);
            }
        }
    }
}

/// For any package referenced by the cache packages manifest that has all of its blobs present in
/// blobfs, imports those packages into the provided dynamic index.
pub async fn load_cache_packages(
    index: &mut DynamicIndex,
    cache_packages: &CachePackages,
    blobfs: &blobfs::Client,
) {
    // This function is called before anything writes to or deletes from blobfs, so if it needs
    // to be sped up, it might be possible to:
    //   1. get the list of all available blobs with `blobfs.list_known_blobs()`
    //   2. for each cache package, check the list for the meta.far and the necessary content
    //      blobs (obtained with `RootDir::external_file_hashes()`)
    // This alternate approach requires that blobfs responds to `fuchsia.io/Directory.ReadDirents`
    // with *only* blobs that are readable, i.e. blobs for which the `USER_0` signal is set (which
    // is currently checked per-package per-blob by `blobfs.filter_to_missing_blobs()`). Would need
    // to confirm with the storage team that blobfs meets this requirement.
    // TODO(fxbug.dev/90656): ensure non-fuchsia.com URLs are correctly handled in dynamic index.
    for url in cache_packages.contents() {
        let hash = url.hash();
        let path = PackagePath::from_name_and_variant(
            url.name().clone(),
            url.variant().unwrap_or(&fuchsia_pkg::PackageVariant::zero()).clone(),
        );
        let required_blobs = match package_directory::RootDir::new(blobfs.clone(), hash).await {
            Ok(root_dir) => match root_dir.path().await {
                Ok(path_from_far) => {
                    if path_from_far != path {
                        error!(
                            "load_cache_packages: path mismatch for {}, manifest: {}, far: {}",
                            hash, path, path_from_far
                        );
                        continue;
                    }
                    root_dir.external_file_hashes().copied().collect::<HashSet<_>>()
                }
                Err(e) => {
                    error!(
                        "load_cache_packages: obtaining path for {} {} failed: {:#}",
                        hash,
                        path,
                        anyhow!(e)
                    );
                    continue;
                }
            },
            Err(package_directory::Error::MissingMetaFar) => continue,
            Err(e) => {
                error!(
                    "load_cache_packages: creating RootDir for {} {} failed: {:#}",
                    hash,
                    path,
                    anyhow!(e)
                );
                continue;
            }
        };
        if !blobfs.filter_to_missing_blobs(&required_blobs).await.is_empty() {
            continue;
        }
        let () = index.start_install(hash);
        if let Err(e) = index.fulfill_meta_far(hash, path, required_blobs) {
            error!("load_cache_packages: fulfill_meta_far of {} failed: {:#}", hash, anyhow!(e));
            let () = index.cancel_install(&hash);
            continue;
        }
        if let Err(e) = index.complete_install(hash) {
            error!("load_cache_packages: complete_install of {} failed: {:#}", hash, anyhow!(e));
            let () = index.cancel_install(&hash);
            continue;
        }
    }
}

#[derive(Debug, Eq, PartialEq)]
pub enum PackageNode {
    Pending {
        node: finspect::Node,
        state: finspect::StringProperty,
        time: finspect::IntProperty,
    },
    WithMetaFar {
        node: finspect::Node,
        state: finspect::StringProperty,
        path: finspect::StringProperty,
        required_blobs: finspect::IntProperty,
        time: finspect::IntProperty,
    },
    Active {
        node: finspect::Node,
        state: finspect::StringProperty,
        path: finspect::StringProperty,
        required_blobs: finspect::IntProperty,
        time: finspect::IntProperty,
    },
}

#[derive(Debug, Eq, PartialEq)]
struct PackageWithInspect {
    package: Package,
    package_node: PackageNode,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Package {
    /// Only the package hash is known, meta.far isn't available yet.
    Pending,
    /// We have the meta.far
    WithMetaFar {
        /// The name and variant of the package.
        path: PackagePath,
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

impl Package {
    fn content_blobs(&self) -> Option<&HashSet<Hash>> {
        match self {
            Package::Pending => None,
            Package::WithMetaFar { required_blobs, .. } => Some(required_blobs),
            Package::Active { required_blobs, .. } => Some(required_blobs),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fuchsia_async as fasync,
        fuchsia_pkg_testing::PackageBuilder,
        fuchsia_url::PinnedAbsolutePackageUrl,
        maplit::{hashmap, hashset},
        std::str::FromStr,
    };

    #[test]
    fn test_get_name_if_active() {
        let inspector = finspect::Inspector::new();
        let mut dynamic_index = DynamicIndex::new(inspector.root().create_child("index"));
        dynamic_index.add_package(Hash::from([1; 32]), Package::Pending);
        dynamic_index.add_package(
            Hash::from([2; 32]),
            Package::WithMetaFar {
                path: PackagePath::from_name_and_variant(
                    "fake-package".parse().unwrap(),
                    "0".parse().unwrap(),
                ),
                required_blobs: hashset! { Hash::from([3; 32]), Hash::from([4; 32]) },
            },
        );
        dynamic_index.add_package(
            Hash::from([3; 32]),
            Package::Active {
                path: PackagePath::from_name_and_variant(
                    "fake-package-2".parse().unwrap(),
                    "0".parse().unwrap(),
                ),
                required_blobs: hashset! { Hash::from([6; 32]) },
            },
        );

        assert_eq!(dynamic_index.get_name_if_active(&Hash::from([1; 32])), None);
        assert_eq!(dynamic_index.get_name_if_active(&Hash::from([2; 32])), None);
        assert_eq!(
            dynamic_index.get_name_if_active(&Hash::from([3; 32])),
            Some(&PackageName::from_str("fake-package-2").unwrap())
        );
    }

    #[test]
    fn test_all_blobs() {
        let inspector = finspect::Inspector::new();
        let mut dynamic_index = DynamicIndex::new(inspector.root().create_child("index"));
        assert_eq!(dynamic_index.all_blobs(), HashSet::new());

        dynamic_index.add_package(Hash::from([1; 32]), Package::Pending);
        dynamic_index.add_package(
            Hash::from([2; 32]),
            Package::WithMetaFar {
                path: PackagePath::from_name_and_variant(
                    "fake-package".parse().unwrap(),
                    "0".parse().unwrap(),
                ),
                required_blobs: hashset! { Hash::from([3; 32]), Hash::from([4; 32]) },
            },
        );
        dynamic_index.add_package(
            Hash::from([5; 32]),
            Package::Active {
                path: PackagePath::from_name_and_variant(
                    "fake-package-2".parse().unwrap(),
                    "0".parse().unwrap(),
                ),
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

    #[test]
    fn lookup_content_blobs_handles_withmetafar_and_active_states() {
        let inspector = finspect::Inspector::new();
        let mut dynamic_index = DynamicIndex::new(inspector.root().create_child("index"));

        dynamic_index.add_package(Hash::from([1; 32]), Package::Pending);
        dynamic_index.add_package(
            Hash::from([2; 32]),
            Package::WithMetaFar {
                path: PackagePath::from_name_and_variant(
                    "fake-package".parse().unwrap(),
                    "0".parse().unwrap(),
                ),
                required_blobs: hashset! { Hash::from([3; 32]), Hash::from([4; 32]) },
            },
        );
        dynamic_index.add_package(
            Hash::from([5; 32]),
            Package::Active {
                path: PackagePath::from_name_and_variant(
                    "fake-package-2".parse().unwrap(),
                    "0".parse().unwrap(),
                ),
                required_blobs: hashset! { Hash::from([6; 32]) },
            },
        );

        assert_eq!(dynamic_index.lookup_content_blobs(&Hash::from([0; 32])), None);
        assert_eq!(dynamic_index.lookup_content_blobs(&Hash::from([1; 32])), None);
        assert_eq!(
            dynamic_index.lookup_content_blobs(&Hash::from([2; 32])),
            Some(&hashset! { Hash::from([3; 32]), Hash::from([4; 32]) })
        );
        assert_eq!(
            dynamic_index.lookup_content_blobs(&Hash::from([5; 32])),
            Some(&hashset! { Hash::from([6; 32]) })
        );
    }

    #[test]
    fn test_complete_install() {
        let inspector = finspect::Inspector::new();
        let mut dynamic_index = DynamicIndex::new(inspector.root().create_child("index"));

        let hash = Hash::from([2; 32]);
        let path = PackagePath::from_name_and_variant(
            "fake-package".parse().unwrap(),
            "0".parse().unwrap(),
        );
        let required_blobs =
            hashset! { Hash::from([3; 32]), Hash::from([4; 32]), Hash::from([5; 32]) };
        let previous_hash = Hash::from([6; 32]);
        dynamic_index.add_package(
            previous_hash,
            Package::Active {
                path: path.clone(),
                required_blobs: hashset! { Hash::from([7; 32]) },
            },
        );
        dynamic_index.add_package(
            hash,
            Package::WithMetaFar { path: path.clone(), required_blobs: required_blobs.clone() },
        );

        let name = dynamic_index.complete_install(hash).unwrap();
        assert_eq!(name, *path.name());
        assert_eq!(
            dynamic_index.packages(),
            hashmap! {
                hash => Package::Active {
                    path: path.clone(),
                    required_blobs,
                },
            }
        );
        assert_eq!(dynamic_index.active_packages(), hashmap! { path => hash });
    }

    #[test]
    fn complete_install_unknown_package() {
        let inspector = finspect::Inspector::new();
        let mut dynamic_index = DynamicIndex::new(inspector.root().create_child("index"));

        assert_matches!(
            dynamic_index.complete_install(Hash::from([2; 32])),
            Err(CompleteInstallError::UnexpectedPackageState(None))
        );
    }

    #[test]
    fn complete_install_pending_package() {
        let inspector = finspect::Inspector::new();
        let mut dynamic_index = DynamicIndex::new(inspector.root().create_child("index"));

        let hash = Hash::from([2; 32]);
        dynamic_index.add_package(hash, Package::Pending);
        assert_matches!(
            dynamic_index.complete_install(hash),
            Err(CompleteInstallError::UnexpectedPackageState(Some(Package::Pending)))
        );
    }

    #[test]
    fn complete_install_active_package() {
        let inspector = finspect::Inspector::new();
        let mut dynamic_index = DynamicIndex::new(inspector.root().create_child("index"));

        let hash = Hash::from([2; 32]);
        let package = Package::Active {
            path: PackagePath::from_name_and_variant(
                "fake-package".parse().unwrap(),
                "0".parse().unwrap(),
            ),
            required_blobs: hashset! { Hash::from([3; 32]), Hash::from([4; 32]) },
        };
        dynamic_index.add_package(hash, package.clone());
        assert_matches!(
            dynamic_index.complete_install(hash),
            Err(CompleteInstallError::UnexpectedPackageState(Some(p))) if p == package
        );
    }

    #[test]
    fn start_install() {
        let inspector = finspect::Inspector::new();
        let mut dynamic_index = DynamicIndex::new(inspector.root().create_child("index"));

        let hash = Hash::from([2; 32]);
        dynamic_index.start_install(hash);

        assert_eq!(dynamic_index.packages(), hashmap! { hash => Package::Pending {  } });
    }

    #[test]
    fn start_install_do_not_overwrite() {
        let inspector = finspect::Inspector::new();
        let mut dynamic_index = DynamicIndex::new(inspector.root().create_child("index"));

        let hash = Hash::from([2; 32]);
        let package = Package::WithMetaFar {
            path: PackagePath::from_name_and_variant(
                "fake-package".parse().unwrap(),
                "0".parse().unwrap(),
            ),
            required_blobs: hashset! { Hash::from([3; 32]), Hash::from([4; 32]) },
        };
        dynamic_index.add_package(hash, package.clone());

        dynamic_index.start_install(hash);

        assert_eq!(dynamic_index.packages(), hashmap! { hash => package });
    }

    #[test]
    fn cancel_install() {
        let inspector = finspect::Inspector::new();
        let mut dynamic_index = DynamicIndex::new(inspector.root().create_child("index"));

        let hash = Hash::from([2; 32]);
        dynamic_index.start_install(hash);
        assert_eq!(dynamic_index.packages(), hashmap! { hash => Package::Pending });
        dynamic_index.cancel_install(&hash);
        assert_eq!(dynamic_index.packages, hashmap! {});
    }

    #[test]
    fn cancel_install_with_meta_far() {
        let inspector = finspect::Inspector::new();
        let mut dynamic_index = DynamicIndex::new(inspector.root().create_child("index"));

        let hash = Hash::from([2; 32]);
        dynamic_index.add_package(
            hash,
            Package::WithMetaFar {
                path: PackagePath::from_name_and_variant(
                    "fake-package".parse().unwrap(),
                    "0".parse().unwrap(),
                ),
                required_blobs: hashset! { Hash::from([3; 32]), Hash::from([4; 32]) },
            },
        );
        dynamic_index.cancel_install(&hash);
        assert_eq!(dynamic_index.packages(), hashmap! {});
    }

    #[test]
    fn cancel_install_active() {
        let inspector = finspect::Inspector::new();
        let mut dynamic_index = DynamicIndex::new(inspector.root().create_child("index"));

        let hash = Hash::from([2; 32]);
        let path = PackagePath::from_name_and_variant(
            "fake-package".parse().unwrap(),
            "0".parse().unwrap(),
        );
        let package = Package::Active {
            path: path.clone(),
            required_blobs: hashset! { Hash::from([3; 32]), Hash::from([4; 32]) },
        };
        dynamic_index.add_package(hash, package.clone());
        dynamic_index.cancel_install(&hash);
        assert_eq!(dynamic_index.packages(), hashmap! { hash => package });
        assert_eq!(dynamic_index.active_packages(), hashmap! { path => hash });
    }

    #[test]
    fn cancel_install_unknown() {
        let inspector = finspect::Inspector::new();
        let mut dynamic_index = DynamicIndex::new(inspector.root().create_child("index"));

        dynamic_index.start_install(Hash::from([2; 32]));
        dynamic_index.cancel_install(&Hash::from([4; 32]));
        assert_eq!(dynamic_index.packages(), hashmap! { Hash::from([2; 32]) => Package::Pending });
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_cache_packages() {
        let present_package0 = PackageBuilder::new("present0")
            .add_resource_at("present-blob0", &b"contents0"[..])
            .build()
            .await
            .unwrap();
        let missing_content_blob = PackageBuilder::new("missing-content-blob")
            .add_resource_at("missing-blob", &b"missing-contents"[..])
            .build()
            .await
            .unwrap();
        let missing_meta_far = PackageBuilder::new("missing-meta-far")
            .add_resource_at("other-present-blob", &b"other-present-contents"[..])
            .build()
            .await
            .unwrap();
        let present_package1 = PackageBuilder::new("present1")
            .add_resource_at("present-blob1", &b"contents1"[..])
            .build()
            .await
            .unwrap();

        let blobfs = blobfs_ramdisk::BlobfsRamdisk::start().unwrap();
        let blobfs_dir = blobfs.root_dir().unwrap();

        present_package0.write_to_blobfs_dir(&blobfs_dir);
        missing_content_blob.write_to_blobfs_dir(&blobfs_dir);
        missing_meta_far.write_to_blobfs_dir(&blobfs_dir);
        present_package1.write_to_blobfs_dir(&blobfs_dir);

        for (hash, _) in missing_content_blob.contents().1 {
            blobfs_dir.remove_file(hash.to_string()).unwrap();
        }
        blobfs_dir.remove_file(missing_meta_far.contents().0.merkle.to_string()).unwrap();

        let cache_packages = CachePackages::from_entries(vec![
            PinnedAbsolutePackageUrl::new(
                "fuchsia-pkg://fuchsia.test".parse().unwrap(),
                "present0".parse().unwrap(),
                Some(fuchsia_url::PackageVariant::zero()),
                *present_package0.meta_far_merkle_root(),
            ),
            PinnedAbsolutePackageUrl::new(
                "fuchsia-pkg://fuchsia.test".parse().unwrap(),
                "missing-content-blob".parse().unwrap(),
                Some(fuchsia_url::PackageVariant::zero()),
                *missing_content_blob.meta_far_merkle_root(),
            ),
            PinnedAbsolutePackageUrl::new(
                "fuchsia-pkg://fuchsia.test".parse().unwrap(),
                "missing-meta-far".parse().unwrap(),
                Some(fuchsia_url::PackageVariant::zero()),
                *missing_meta_far.meta_far_merkle_root(),
            ),
            PinnedAbsolutePackageUrl::new(
                "fuchsia-pkg://fuchsia.test".parse().unwrap(),
                "present1".parse().unwrap(),
                Some(fuchsia_url::PackageVariant::zero()),
                *present_package1.meta_far_merkle_root(),
            ),
        ]);

        let mut dynamic_index =
            DynamicIndex::new(finspect::Inspector::new().root().create_child("index"));

        let () = load_cache_packages(&mut dynamic_index, &cache_packages, &blobfs.client()).await;

        let present0 = Package::Active {
            path: "present0/0".parse().unwrap(),
            required_blobs: present_package0.contents().1.into_keys().collect(),
        };
        let present1 = Package::Active {
            path: "present1/0".parse().unwrap(),
            required_blobs: present_package1.contents().1.into_keys().collect(),
        };

        assert_eq!(
            dynamic_index.packages(),
            hashmap! {
                *present_package0.meta_far_merkle_root() => present0,
                *present_package1.meta_far_merkle_root() => present1
            }
        );
        assert_eq!(
            dynamic_index.active_packages(),
            hashmap! {
                "present0/0".parse().unwrap() => *present_package0.meta_far_merkle_root(),
                "present1/0".parse().unwrap() => *present_package1.meta_far_merkle_root(),
            }
        );
        assert_eq!(
            dynamic_index.all_blobs(),
            present_package0
                .list_blobs()
                .unwrap()
                .into_iter()
                .chain(present_package1.list_blobs().unwrap().into_iter())
                .collect()
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn fulfill_meta_far_transitions_package_from_pending_to_with_meta_far() {
        let inspector = finspect::Inspector::new();
        let mut dynamic_index = DynamicIndex::new(inspector.root().create_child("index"));

        let hash = Hash::from([2; 32]);
        let path = PackagePath::from_name_and_variant(
            "fake-package".parse().unwrap(),
            "0".parse().unwrap(),
        );
        dynamic_index.start_install(hash);

        let blob_hash = Hash::from([3; 32]);

        let () =
            dynamic_index.fulfill_meta_far(hash, path.clone(), hashset! { blob_hash }).unwrap();

        assert_eq!(
            dynamic_index.packages(),
            hashmap! {
                hash => Package::WithMetaFar {
                    path,
                    required_blobs: hashset! { blob_hash },
                }
            }
        );
        assert_eq!(dynamic_index.active_packages(), hashmap! {});
    }

    #[fasync::run_singlethreaded(test)]
    async fn fulfill_meta_far_fails_on_unknown_package() {
        let inspector = finspect::Inspector::new();
        let mut index = DynamicIndex::new(inspector.root().create_child("index"));

        assert_matches!(
            index.fulfill_meta_far(Hash::from([2; 32]), PackagePath::from_name_and_variant("unknown".parse().unwrap(), "0".parse().unwrap()), hashset!{}),
            Err(FulfillNotNeededBlobError{hash, state}) if hash == Hash::from([2; 32]) && state == "missing"
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn fulfill_meta_far_fails_on_package_with_meta_far() {
        let inspector = finspect::Inspector::new();
        let mut index = DynamicIndex::new(inspector.root().create_child("index"));

        let hash = Hash::from([2; 32]);
        let path = PackagePath::from_name_and_variant(
            "with-meta-far-pkg".parse().unwrap(),
            "0".parse().unwrap(),
        );
        let required_blobs = hashset! { Hash::from([3; 32]), Hash::from([4; 32]) };
        let package =
            Package::WithMetaFar { path: path.clone(), required_blobs: required_blobs.clone() };
        index.add_package(hash, package);

        assert_matches!(
            index.fulfill_meta_far(hash, path, required_blobs),
            Err(FulfillNotNeededBlobError{hash, state}) if hash == Hash::from([2; 32]) && state == "WithMetaFar"
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn fulfill_meta_far_fails_on_active_package() {
        let inspector = finspect::Inspector::new();
        let mut index = DynamicIndex::new(inspector.root().create_child("index"));

        let hash = Hash::from([2; 32]);
        let path =
            PackagePath::from_name_and_variant("active".parse().unwrap(), "0".parse().unwrap());
        let required_blobs = hashset! { Hash::from([3; 32]), Hash::from([4; 32]) };
        let package =
            Package::Active { path: path.clone(), required_blobs: required_blobs.clone() };
        index.add_package(hash, package);

        assert_matches!(
            index.fulfill_meta_far(hash, path, required_blobs),
            Err(FulfillNotNeededBlobError{hash, state}) if hash == Hash::from([2; 32]) && state == "Active"
        );
    }
}
