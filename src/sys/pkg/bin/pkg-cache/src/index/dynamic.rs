// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fuchsia_inspect as finspect,
    fuchsia_merkle::Hash,
    fuchsia_pkg::PackagePath,
    fuchsia_syslog::{fx_log_err, fx_log_warn},
    fuchsia_zircon as zx,
    std::{
        collections::{HashMap, HashSet},
        convert::TryInto,
    },
    system_image::CachePackages,
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

    #[cfg(test)]
    pub fn active_packages(&self) -> HashMap<PackagePath, Hash> {
        self.active_packages.clone()
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
    pub fn complete_install(&mut self, package_hash: Hash) -> Result<(), CompleteInstallError> {
        match self.packages.get_mut(&package_hash) {
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

                    if let Some(previous_package) = self.active_packages.insert(path, package_hash)
                    {
                        self.packages.remove(&previous_package);
                    }
                } else {
                    unreachable!()
                }
            }
            package => {
                return Err(CompleteInstallError::UnexpectedPackageState(
                    package.map(|pwi| pwi.package.to_owned()),
                ));
            }
        }
        Ok(())
    }

    /// Notifies dynamic index that the given package installation has been canceled.
    pub fn cancel_install(&mut self, package_hash: &Hash) {
        match self.packages.get(package_hash).map(|pwi| &pwi.package) {
            Some(Package::Pending) | Some(Package::WithMetaFar { .. }) => {
                self.packages.remove(package_hash);
            }
            Some(Package::Active { .. }) => {
                fx_log_warn!("Unable to cancel install for active package {}", package_hash);
            }
            None => {
                fx_log_err!("Unable to cancel install for unknown package {}", package_hash);
            }
        }
    }
}

/// Loads the cache_packages manifest from the provided system_image package, and then, for any
/// package referenced by that manifest that has all of its blobs present in blobfs, imports those
/// packages into the provided dynamic index.
pub async fn load_cache_packages(
    index: &mut DynamicIndex,
    system_image: &pkgfs::system::Client,
    versions: &pkgfs::versions::Client,
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
            Err(pkgfs::versions::OpenError::NotFound) => continue,
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
        fidl_fuchsia_io::DirectoryProxy,
        fuchsia_async as fasync,
        fuchsia_pkg::MetaContents,
        maplit::{hashmap, hashset},
        matches::assert_matches,
        std::{
            collections::HashMap,
            fs::{create_dir, create_dir_all, File},
        },
        tempfile::TempDir,
    };

    #[test]
    fn test_all_blobs() {
        let inspector = finspect::Inspector::new();
        let mut dynamic_index = DynamicIndex::new(inspector.root().create_child("index"));
        assert_eq!(dynamic_index.all_blobs(), HashSet::new());

        dynamic_index.add_package(Hash::from([1; 32]), Package::Pending);
        dynamic_index.add_package(
            Hash::from([2; 32]),
            Package::WithMetaFar {
                path: PackagePath::from_name_and_variant("fake-package", "0").unwrap(),
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

    #[test]
    fn lookup_content_blobs_handles_withmetafar_and_active_states() {
        let inspector = finspect::Inspector::new();
        let mut dynamic_index = DynamicIndex::new(inspector.root().create_child("index"));

        dynamic_index.add_package(Hash::from([1; 32]), Package::Pending);
        dynamic_index.add_package(
            Hash::from([2; 32]),
            Package::WithMetaFar {
                path: PackagePath::from_name_and_variant("fake-package", "0").unwrap(),
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
        let path = PackagePath::from_name_and_variant("fake-package", "0").unwrap();
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

        dynamic_index.complete_install(hash).unwrap();
        assert_eq!(
            dynamic_index.packages(),
            hashmap! {
                hash => Package::Active {
                    path: path.clone(),
                    required_blobs,
                },
            }
        );
        assert_eq!(dynamic_index.active_packages, hashmap! { path => hash });
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
            path: PackagePath::from_name_and_variant("fake-package", "0").unwrap(),
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
            path: PackagePath::from_name_and_variant("fake-package", "0").unwrap(),
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
                path: PackagePath::from_name_and_variant("fake-package", "0").unwrap(),
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
        let path = PackagePath::from_name_and_variant("fake-package", "0").unwrap();
        let package = Package::Active {
            path: path.clone(),
            required_blobs: hashset! { Hash::from([3; 32]), Hash::from([4; 32]) },
        };
        dynamic_index.add_package(hash, package.clone());
        dynamic_index.cancel_install(&hash);
        assert_eq!(dynamic_index.packages(), hashmap! { hash => package });
        assert_eq!(dynamic_index.active_packages, hashmap! { path => hash });
    }

    #[test]
    fn cancel_install_unknown() {
        let inspector = finspect::Inspector::new();
        let mut dynamic_index = DynamicIndex::new(inspector.root().create_child("index"));

        dynamic_index.start_install(Hash::from([2; 32]));
        dynamic_index.cancel_install(&Hash::from([4; 32]));
        assert_eq!(dynamic_index.packages(), hashmap! { Hash::from([2; 32]) => Package::Pending });
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

        fn system_image(&self) -> pkgfs::system::Client {
            pkgfs::system::Client::open_from_pkgfs_root(&self.root_proxy()).unwrap()
        }

        fn versions(&self) -> pkgfs::versions::Client {
            pkgfs::versions::Client::open_from_pkgfs_root(&self.root_proxy()).unwrap()
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
                    hashmap! {
                        "some-blob".to_string() => some_blob_hash,
                        "other-blob".to_string() => other_blob_hash
                    }
                ).unwrap(),
            share_blob_package_hash =>
                MetaContents::from_map(
                    hashmap! {
                        "some-blob".to_string() => some_blob_hash,
                        "yet-another-blob".to_string() => yet_another_blob_hash
                    }
                ).unwrap()
        };
        let pkgfs = TestPkgfs::new(&cache_packages, &versions_contents);
        let inspector = finspect::Inspector::new();
        let mut dynamic_index = DynamicIndex::new(inspector.root().create_child("index"));
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
            dynamic_index.packages(),
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

    #[fasync::run_singlethreaded(test)]
    async fn fulfill_meta_far_transitions_package_from_pending_to_with_meta_far() {
        let inspector = finspect::Inspector::new();
        let mut dynamic_index = DynamicIndex::new(inspector.root().create_child("index"));

        let hash = Hash::from([2; 32]);
        let path = PackagePath::from_name_and_variant("fake-package", "0").unwrap();
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
        assert_eq!(dynamic_index.active_packages, hashmap! {});
    }

    #[fasync::run_singlethreaded(test)]
    async fn fulfill_meta_far_fails_on_unknown_package() {
        let inspector = finspect::Inspector::new();
        let mut index = DynamicIndex::new(inspector.root().create_child("index"));

        assert_matches!(
            index.fulfill_meta_far(Hash::from([2; 32]), PackagePath::from_name_and_variant("unknown", "0").unwrap(), hashset!{}),
            Err(FulfillNotNeededBlobError{hash, state}) if hash == Hash::from([2; 32]) && state == "missing"
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn fulfill_meta_far_fails_on_package_with_meta_far() {
        let inspector = finspect::Inspector::new();
        let mut index = DynamicIndex::new(inspector.root().create_child("index"));

        let hash = Hash::from([2; 32]);
        let path = PackagePath::from_name_and_variant("with-meta-far-pkg", "0").unwrap();
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
        let path = PackagePath::from_name_and_variant("active", "0").unwrap();
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
