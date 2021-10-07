// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use crate::index::dynamic::CompleteInstallError;
use {
    crate::index::{
        dynamic::{DynamicIndex, FulfillNotNeededBlobError},
        retained::RetainedIndex,
        QueryPackageMetadataError,
    },
    fuchsia_hash::Hash,
    fuchsia_inspect as finspect,
    fuchsia_pkg::PackagePath,
    futures::lock::Mutex,
    std::{collections::HashSet, sync::Arc},
};

/// The index of packages known to pkg-cache.
#[derive(Debug)]
pub struct PackageIndex {
    dynamic: DynamicIndex,
    retained: RetainedIndex,
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    node: finspect::Node,
}

#[derive(thiserror::Error, Debug)]
pub enum FulfillMetaFarError {
    #[error("the blob ({0}) is not present in blobfs")]
    BlobNotFound(Hash),

    #[error("failed to query package metadata")]
    QueryPackageMetadata(#[from] QueryPackageMetadataError),

    #[error("the blob is not needed by any index")]
    FulfillNotNeededBlob(#[from] FulfillNotNeededBlobError),
}

impl PackageIndex {
    /// Creates a new, empty instance of the PackageIndex.
    pub fn new(node: finspect::Node) -> Self {
        Self {
            dynamic: DynamicIndex::new(node.create_child("dynamic")),
            retained: RetainedIndex::new(node.create_child("retained")),
            node,
        }
    }

    #[cfg(test)]
    fn new_test() -> Self {
        let inspector = finspect::Inspector::new();
        Self::new(inspector.root().create_child("index"))
    }

    /// Notifies the appropriate indices that the package with the given hash is going to be
    /// installed, ensuring the meta far blob is protected by the index.
    pub fn start_install(&mut self, package_hash: Hash) {
        if self.retained.contains_package(&package_hash) {
            // The retained index intends to track this package, but the first event it wants to
            // take action on is when the meta far is readable. Nothing to do now.

            // This package wasn't already in the dynamic index, so don't add it to it.
            return;
        }

        self.dynamic.start_install(package_hash);
    }

    fn fulfill_meta_far(
        &mut self,
        package_hash: Hash,
        package_path: PackagePath,
        content_blobs: HashSet<Hash>,
    ) -> Result<(), FulfillMetaFarError> {
        // Notify the retained index if it is interested in this package.
        let is_retained = self.retained.set_content_blobs(&package_hash, &content_blobs);

        // Transition the dynamic index state if it is interested in this package.  Report an error if
        // the package is not tracked by any index.
        let () = match self.dynamic.fulfill_meta_far(package_hash, package_path, content_blobs) {
            Err(crate::index::dynamic::FulfillNotNeededBlobError { .. }) if is_retained => Ok(()),
            Err(e @ crate::index::dynamic::FulfillNotNeededBlobError { .. }) => Err(e),
            Ok(()) => Ok(()),
        }?;

        Ok(())
    }

    /// Notifies the appropriate indices that the package with the given hash has completed
    /// installation.
    pub fn complete_install(&mut self, package_hash: Hash) -> Result<(), CompleteInstallError> {
        let is_retained = self.retained.contains_package(&package_hash);

        match self.dynamic.complete_install(package_hash) {
            Err(_) if is_retained => Ok(()),
            res => res,
        }
    }

    /// Notifies the appropriate indices that the installation for the package with the given hash
    /// has ben cancelled.
    pub fn cancel_install(&mut self, package_hash: &Hash) {
        self.dynamic.cancel_install(package_hash)
    }

    fn set_retained_index(&mut self, mut index: RetainedIndex) {
        // Populate the index with content blobs from the dynamic index.
        let missing = index.iter_packages_with_unknown_content_blobs().collect::<Vec<_>>();
        for hash in missing {
            if let Some(blobs) = self.dynamic.lookup_content_blobs(&hash) {
                assert!(index.set_content_blobs(&hash, blobs));
            }
        }

        // Replace our retained index with the new one, which will populate content blobs from the
        // old retained index.
        self.retained.replace(index);
    }

    /// Returns all blobs protected by the dynamic and retained indices.
    pub fn all_blobs(&self) -> HashSet<Hash> {
        let mut all = self.dynamic.all_blobs();
        all.extend(self.retained.all_blobs());
        all
    }
}

/// Load present cache packages into the dynamic index.
pub async fn load_cache_packages(
    index: &mut PackageIndex,
    system_image: &pkgfs::system::Client,
    versions: &pkgfs::versions::Client,
) -> Result<(), anyhow::Error> {
    crate::index::dynamic::load_cache_packages(&mut index.dynamic, system_image, versions).await
}

/// Notifies the appropriate inner indices that the given meta far blob is now available in blobfs.
/// Do not use this for regular blob (unless it's also a meta far).
pub async fn fulfill_meta_far_blob(
    index: &Arc<Mutex<PackageIndex>>,
    blobfs: &blobfs::Client,
    meta_hash: Hash,
) -> Result<HashSet<Hash>, FulfillMetaFarError> {
    let (path, content_blobs) = crate::index::enumerate_package_blobs(blobfs, &meta_hash)
        .await?
        .ok_or_else(|| FulfillMetaFarError::BlobNotFound(meta_hash))?;

    let () = index.lock().await.fulfill_meta_far(meta_hash, path, content_blobs.clone())?;

    Ok(content_blobs)
}

/// Replaces the retained index with one that tracks the given meta far hashes.
pub async fn set_retained_index(
    index: &Arc<Mutex<PackageIndex>>,
    blobfs: &blobfs::Client,
    meta_hashes: &[Hash],
) {
    // To avoid having to hold a lock while reading/parsing meta fars, first produce a fresh
    // retained index from meta fars available in blobfs.
    let new_retained = crate::index::retained::populate_retained_index(blobfs, meta_hashes).await;

    // Then, atomically, merge in available data from the dynamic/retained indices and swap in
    // the new retained index.
    index.lock().await.set_retained_index(new_retained);
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{index::dynamic::Package, test_utils::add_meta_far_to_blobfs},
        maplit::{hashmap, hashset},
        matches::assert_matches,
    };

    fn hash(n: u8) -> Hash {
        Hash::from([n; 32])
    }
    fn path(s: &str) -> PackagePath {
        PackagePath::from_name_and_variant(s.parse().unwrap(), "0".parse().unwrap())
    }

    #[test]
    fn set_retained_index_with_dynamic_package_in_pending_state_puts_package_in_both() {
        let mut index = PackageIndex::new_test();

        index.start_install(hash(0));

        index.set_retained_index(RetainedIndex::from_packages(hashmap! {
            hash(0) => None,
        }));

        index.fulfill_meta_far(hash(0), path("pending"), hashset! {hash(1)}).unwrap();
        index.complete_install(hash(0)).unwrap();

        assert_eq!(
            index.retained.packages(),
            hashmap! {
                hash(0) => Some(vec![hash(1)]),
            }
        );
        assert_eq!(
            index.dynamic.active_packages(),
            hashmap! {
                path("pending") => hash(0),
            }
        );
        assert_eq!(
            index.dynamic.packages(),
            hashmap! {
                hash(0) => Package::Active {
                    path: path("pending"),
                    required_blobs: hashset!{hash(1)},
                },
            }
        );
    }

    #[test]
    fn set_retained_index_with_dynamic_package_in_withmetafar_state_puts_package_in_both() {
        let mut index = PackageIndex::new_test();

        index.start_install(hash(0));
        index.start_install(hash(1));

        index.fulfill_meta_far(hash(0), path("withmetafar1"), hashset! {hash(10)}).unwrap();
        index.fulfill_meta_far(hash(1), path("withmetafar2"), hashset! {hash(11)}).unwrap();

        // Constructing a new RetainedIndex may race with a package install to the dynamic index.
        // Ensure index.set_retained_index handles both cases.
        index.set_retained_index(RetainedIndex::from_packages(hashmap! {
            hash(0) => Some(vec![hash(10)]),
            hash(1) => None,
        }));

        index.complete_install(hash(0)).unwrap();
        index.complete_install(hash(1)).unwrap();

        assert_eq!(
            index.retained.packages(),
            hashmap! {
                hash(0) => Some(vec![hash(10)]),
                hash(1) => Some(vec![hash(11)]),
            }
        );
        assert_eq!(
            index.dynamic.active_packages(),
            hashmap! {
                path("withmetafar1") => hash(0),
                path("withmetafar2") => hash(1),
            }
        );
        assert_eq!(
            index.dynamic.packages(),
            hashmap! {
                hash(0) => Package::Active {
                    path: path("withmetafar1"),
                    required_blobs: hashset!{hash(10)},
                },
                hash(1) => Package::Active {
                    path: path("withmetafar2"),
                    required_blobs: hashset!{hash(11)},
                },
            }
        );
    }

    #[test]
    fn set_retained_index_with_no_dynamic_package_entry_puts_package_in_retained_only() {
        let mut index = PackageIndex::new_test();

        index.set_retained_index(RetainedIndex::from_packages(hashmap! {
            hash(0) => None,
        }));

        index.start_install(hash(0));
        index.fulfill_meta_far(hash(0), path("retaiendonly"), hashset! {hash(123)}).unwrap();
        index.complete_install(hash(0)).unwrap();

        assert_eq!(
            index.retained.packages(),
            hashmap! {
                hash(0) => Some(vec![hash(123)]),
            }
        );
        assert_eq!(index.dynamic.active_packages(), hashmap! {});
        assert_eq!(index.dynamic.packages(), hashmap! {});
    }

    #[test]
    fn retained_index_is_not_informed_of_packages_it_does_not_track() {
        let mut index = PackageIndex::new_test();

        index.set_retained_index(RetainedIndex::from_packages(hashmap! {
            hash(0) => Some(vec![hash(10)]),
            hash(1) => None,
        }));

        // install a package not tracked by the retained index
        index.start_install(hash(2));
        index.fulfill_meta_far(hash(2), path("dynamic-only"), hashset! {hash(10)}).unwrap();
        index.complete_install(hash(2)).unwrap();

        assert_eq!(
            index.retained.packages(),
            hashmap! {
                hash(0) => Some(vec![hash(10)]),
                hash(1) => None,
            }
        );
        assert_eq!(
            index.dynamic.active_packages(),
            hashmap! {
                path("dynamic-only") => hash(2),
            }
        );
        assert_eq!(
            index.dynamic.packages(),
            hashmap! {
                hash(2) => Package::Active {
                    path: path("dynamic-only"),
                    required_blobs: hashset!{hash(10)},
                },
            }
        );
    }

    #[test]
    fn set_retained_index_to_self_is_nop() {
        let mut index = PackageIndex::new_test();

        index.start_install(hash(2));
        index.start_install(hash(3));
        index.fulfill_meta_far(hash(3), path("before"), hashset! {hash(10)}).unwrap();

        index.set_retained_index(RetainedIndex::from_packages(hashmap! {
            hash(0) => Some(vec![hash(11)]),
            hash(1) => None,
            hash(2) => None,
            hash(3) => None,
            hash(4) => None,
            hash(5) => None,
        }));

        index.start_install(hash(4));
        index.start_install(hash(5));
        index.fulfill_meta_far(hash(5), path("after"), hashset! {hash(12)}).unwrap();

        let retained_index = index.retained.clone();

        index.set_retained_index(retained_index.clone());
        assert_eq!(index.retained, retained_index);
    }

    #[test]
    fn cancel_install_only_affects_dynamic_index() {
        let mut index = PackageIndex::new_test();

        index.set_retained_index(RetainedIndex::from_packages(hashmap! {
            hash(0) => None,
        }));

        index.start_install(hash(0));
        index.start_install(hash(1));

        index.fulfill_meta_far(hash(0), path("a"), hashset! {hash(10)}).unwrap();
        index.fulfill_meta_far(hash(1), path("b"), hashset! {hash(11)}).unwrap();

        index.cancel_install(&hash(0));
        index.cancel_install(&hash(1));

        assert_eq!(
            index.retained.packages(),
            hashmap! {
                hash(0) => Some(vec![hash(10)]),
            }
        );
        assert_eq!(index.dynamic.active_packages(), hashmap! {});
        assert_eq!(index.dynamic.packages(), hashmap! {});
    }

    #[test]
    fn all_blobs_produces_union_of_dynamic_and_retained_all_blobs() {
        let mut index = PackageIndex::new_test();

        index.start_install(hash(0));

        index.set_retained_index(RetainedIndex::from_packages(hashmap! {
            hash(0) => None,
            hash(5) => None,
            hash(6) => Some(vec![hash(60), hash(61)]),
        }));

        index.start_install(hash(1));

        index.fulfill_meta_far(hash(0), path("pkg1"), hashset! {hash(10)}).unwrap();
        index.fulfill_meta_far(hash(1), path("pkg2"), hashset! {hash(11), hash(61)}).unwrap();

        index.complete_install(hash(0)).unwrap();
        index.complete_install(hash(1)).unwrap();

        assert_eq!(
            index.all_blobs(),
            hashset! {hash(0), hash(1), hash(5), hash(6), hash(10), hash(11), hash(60), hash(61)}
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn fulfill_meta_far_blob_with_missing_blobs() {
        let mut index = PackageIndex::new_test();

        let path = PackagePath::from_name_and_variant(
            "fake-package".parse().unwrap(),
            "0".parse().unwrap(),
        );
        index.start_install(hash(2));

        let index = Arc::new(Mutex::new(index));

        let (blobfs_fake, blobfs) = fuchsia_pkg_testing::blobfs::Fake::new();

        add_meta_far_to_blobfs(&blobfs_fake, hash(2), "fake-package", vec![hash(3)]);

        fulfill_meta_far_blob(&index, &blobfs, hash(2)).await.unwrap();

        let index = index.lock().await;
        assert_eq!(
            index.dynamic.packages(),
            hashmap! {
                hash(2) => Package::WithMetaFar {
                    path,
                    required_blobs: hashset! { hash(3) },
                }
            }
        );
        assert_eq!(index.dynamic.active_packages(), hashmap! {});
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn fulfill_meta_far_blob_not_needed() {
        let index = PackageIndex::new_test();
        let index = Arc::new(Mutex::new(index));

        let (blobfs_fake, blobfs) = fuchsia_pkg_testing::blobfs::Fake::new();

        let hash = Hash::from([2; 32]);
        add_meta_far_to_blobfs(&blobfs_fake, hash, "fake-package", vec![]);

        assert_matches!(
            fulfill_meta_far_blob(&index, &blobfs, hash).await,
            Err(FulfillMetaFarError::FulfillNotNeededBlob(FulfillNotNeededBlobError{hash, state})) if hash == Hash::from([2; 32]) && state == "missing"
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn fulfill_meta_far_blob_not_found() {
        let mut index = PackageIndex::new_test();

        let meta_far_hash = Hash::from([2; 32]);
        index.start_install(meta_far_hash);

        let index = Arc::new(Mutex::new(index));

        let (_blobfs_fake, blobfs) = fuchsia_pkg_testing::blobfs::Fake::new();

        assert_matches!(
            fulfill_meta_far_blob(&index, &blobfs, meta_far_hash).await,
            Err(FulfillMetaFarError::BlobNotFound(hash)) if hash == meta_far_hash
        );
    }
}
