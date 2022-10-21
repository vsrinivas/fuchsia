// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_hash::Hash,
    fuchsia_inspect::{self as finspect, NumericProperty, Property},
    fuchsia_zircon as zx,
    futures::future::FutureExt as _,
    std::collections::{HashMap, HashSet},
    tracing::{error, info},
};

/// An index of packages considered to be part of a new system's base package set.
#[derive(Debug)]
#[cfg_attr(test, derive(Default, PartialEq, Eq))]
pub struct RetainedIndex {
    /// Map from package hash to content and subpackage blobs (meta.fars and content blobs of
    /// all subpackages, plus the subpackages' subpackage blobs, recursively), if known.
    /// If the package is not cached (or is in the process of being cached), the list of
    /// content and subpackage blobs may not be complete.
    packages: HashMap<Hash, RetainedHashes>,

    /// Inspect nodes and properties for the retained index.
    inspect: RetainedIndexInspect,
}

#[derive(Debug, Default)]
#[cfg_attr(test, derive(PartialEq, Eq))]
struct RetainedIndexInspect {
    /// Inspect node for the index.
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    inspect_node: finspect::Node,

    /// Inspect node for the inspect packages collection.
    /// Child nodes in it are created with package hash as a key.
    packages_inspect_node: finspect::Node,

    /// Inspect property for the timestamp when the retained index was updated.
    last_set: finspect::IntProperty,

    /// Inspect property for the sequential generation number of the retained index.
    generation: finspect::UintProperty,
}

#[derive(Debug, Default)]
#[cfg_attr(test, derive(PartialEq, Eq))]
struct RetainedHashes {
    /// Content and subpackage blobs to be protected from GC. None if no hashes have been added
    /// yet, i.e. if the package's meta.far was not available when the package was added to the
    /// index. Some if at least some of the required blobs are known.
    /// TODO(fxbug.dev/112568) Explicitly model the intermediate state in which the meta.far is
    /// cached but not all subpackage meta.fars are cached. The blobs cached in the intermediate
    /// state are protected from GC (serve_needed_blobs gives MissingBlobs a callback that adds
    /// blobs to the retained index as they are encountered in the caching process), but this could
    /// be made more obvious with more specific types.
    hashes: Option<HashSet<Hash>>,

    /// Inspect node for the package.
    inspect_node: finspect::Node,

    /// Inspect property for last timestamp the index was updated.
    last_set: finspect::IntProperty,

    /// Inspect node for the package state.
    /// May be either "need-meta-far" or "known".
    state: finspect::StringProperty,

    /// Inspect node for number of blobs in the package, if known.
    blobs_count: finspect::UintProperty,
}

#[cfg(test)]
impl Clone for RetainedIndex {
    fn clone(&self) -> Self {
        let inspect = RetainedIndexInspect::default();
        RetainedIndex { packages: self.packages.clone(), inspect }
    }
}

#[cfg(test)]
impl Clone for RetainedHashes {
    fn clone(&self) -> Self {
        RetainedHashes { hashes: self.hashes.clone(), ..Default::default() }
    }
}

impl RetainedHashes {
    fn add_hashes(&mut self, new_hashes: &HashSet<Hash>) {
        self.last_set.set(zx::Time::get_monotonic().into_nanos());
        self.state.set("known");
        let count = if let Some(hashes) = &mut self.hashes {
            hashes.extend(new_hashes);
            hashes.len()
        } else {
            self.hashes = Some(new_hashes.clone());
            new_hashes.len()
        };
        self.blobs_count = self.inspect_node.create_uint("blobs-count", count as u64);
    }

    fn set_inspect_node(&mut self, inspect_node: finspect::Node) {
        self.last_set = inspect_node.create_int("last-set", zx::Time::get_monotonic().into_nanos());
        if let Some(hashes) = &self.hashes {
            self.state = inspect_node.create_string("state", "known");
            self.blobs_count = inspect_node.create_uint("blobs-count", hashes.len() as u64);
        } else {
            self.state = inspect_node.create_string("state", "need-meta-far");
        }
        self.inspect_node = inspect_node;
    }
}

impl RetainedIndex {
    /// Creates a new, empty instance of the RetainedIndex.
    pub fn new(inspect_node: finspect::Node) -> Self {
        let packages_inspect_node = inspect_node.create_child("entries");
        let generation = inspect_node.create_uint("generation", 0);
        let last_set = inspect_node.create_int("last-set", zx::Time::get_monotonic().into_nanos());
        let inspect =
            RetainedIndexInspect { inspect_node, packages_inspect_node, generation, last_set };
        Self { packages: HashMap::default(), inspect }
    }

    /// Determines if the given meta_hash is tracked by this index.
    pub fn contains_package(&self, meta_hash: &Hash) -> bool {
        self.packages.contains_key(meta_hash)
    }

    /// The packages protected by this index.
    pub fn retained_packages(&self) -> impl Iterator<Item = &Hash> {
        self.packages.keys()
    }

    #[cfg(test)]
    fn packages_with_unknown_blobs(&self) -> Vec<Hash> {
        let mut res = self
            .packages
            .iter()
            .filter_map(|(k, v)| match v.hashes {
                None => Some(*k),
                Some(_) => None,
            })
            .collect::<Vec<_>>();
        res.sort_unstable();
        res
    }

    #[cfg(test)]
    pub fn from_packages(packages: HashMap<Hash, Option<HashSet<Hash>>>) -> Self {
        Self {
            packages: packages
                .into_iter()
                .map(|(hash, hashes)| (hash, RetainedHashes { hashes, ..Default::default() }))
                .collect(),
            ..Default::default()
        }
    }

    #[cfg(test)]
    pub fn packages(&self) -> HashMap<Hash, Option<HashSet<Hash>>> {
        self.packages
            .iter()
            .map(|(&hash, packages_entry)| (hash, packages_entry.hashes.clone()))
            .collect()
    }

    /// Associates blobs with the given package hash if that package is known to the retained
    /// index, protecting those blobs from garbage collection.
    /// Returns true iff the package is known to this index.
    pub fn add_blobs(&mut self, meta_hash: &Hash, blobs: &HashSet<Hash>) -> bool {
        let packages_entry = match self.packages.get_mut(meta_hash) {
            Some(packages_entry) => packages_entry,
            // `meta_hash` is not a retained package.
            None => return false,
        };

        packages_entry.add_hashes(blobs);
        true
    }

    /// Replaces this retained index instance with other, populating other's blob sets
    /// using data from `self` when possible.
    pub fn replace(&mut self, mut other: Self) {
        for (meta_hash, other_packages_entry) in other.packages.iter_mut() {
            if let Some(this_packages_entry) = self.packages.remove(&meta_hash) {
                if let Some(this_retained_hashes) = this_packages_entry.hashes {
                    if let Some(ref mut other_retained_hashes) = other_packages_entry.hashes {
                        other_retained_hashes.extend(this_retained_hashes)
                    } else {
                        other_packages_entry.hashes = Some(this_retained_hashes);
                    }
                }
            }
            other_packages_entry.set_inspect_node(
                self.inspect.packages_inspect_node.create_child(meta_hash.to_string()),
            )
        }

        self.packages = other.packages;
        self.inspect.generation.add(1);
        self.inspect.last_set.set(zx::Time::get_monotonic().into_nanos());
    }

    /// Returns the set of all blobs currently protected by the retained index.
    pub fn all_blobs(&self) -> HashSet<Hash> {
        self.packages
            .iter()
            .flat_map(|(meta_hash, retained_hashes)| {
                std::iter::once(meta_hash).chain(retained_hashes.hashes.iter().flatten())
            })
            .copied()
            .collect()
    }
}

/// Constructs a new [`RetainedIndex`] from the given blobfs client and set of package meta.far
/// hashes, populating blob hashes for any packages with a meta.far present in blobfs.
/// Populated blob hashes are not guaranteed to be complete because a subpackage meta.far may
/// not be cached.
pub async fn populate_retained_index(
    blobfs: &blobfs::Client,
    meta_hashes: &[Hash],
) -> RetainedIndex {
    let mut packages = HashMap::with_capacity(meta_hashes.len());
    let mut found_packages = HashMap::new();
    for meta_hash in meta_hashes {
        let found = find_required_blobs_recursive(blobfs, meta_hash, &mut found_packages).await;
        let retained_hashes = RetainedHashes { hashes: found.clone(), ..Default::default() };
        found.map(|f| found_packages.insert(*meta_hash, f));
        packages.insert(*meta_hash, retained_hashes);
    }

    let inspect = RetainedIndexInspect::default();
    RetainedIndex { packages, inspect }
}

/// Returns all known required blobs of package `meta_hash`, not guaranteed to be complete.
/// Caches intermediate results in `found_packages` to de-duplicate work in case there are
/// shared subpackages.
/// `found_packages` is *not* updated with the (`meta_hash`, return value) entry, callers should
/// do so.
/// TODO(fxbug.dev/112773) Replace with an iterative implementation to avoid stack overflows.
fn find_required_blobs_recursive<'a>(
    blobfs: &'a blobfs::Client,
    meta_hash: &'a Hash,
    found_packages: &'a mut HashMap<Hash, HashSet<Hash>>,
) -> futures::future::BoxFuture<'a, Option<HashSet<Hash>>> {
    async move {
        if let Some(found) = found_packages.get(meta_hash) {
            return Some(found.clone());
        }

        let root_dir = match package_directory::RootDir::new(blobfs.clone(), *meta_hash).await {
            Ok(root_dir) => root_dir,
            Err(package_directory::Error::MissingMetaFar) => return None,
            Err(e) => {
                // The package isn't readable, it may be in the process of being cached. The
                // required blobs will be added later by the caching process or when the retained
                // index under construction is swapped into the package index.
                info!(
                    "failed to enumerate content blobs for package {}: {:#}",
                    meta_hash,
                    anyhow::anyhow!(e)
                );
                return None;
            }
        };

        let mut found = root_dir.external_file_hashes().copied().collect::<HashSet<_>>();

        let subpackages = match root_dir.subpackages().await {
            Ok(subpackages) => subpackages.into_subpackages(),
            Err(e) => {
                error!(
                    "error obtaining subpackages for package {}: {:#}",
                    meta_hash,
                    anyhow::anyhow!(e)
                );
                return Some(found);
            }
        };
        for subpackage in subpackages.values() {
            found.insert(*subpackage);
            if let Some(subpackage_found) =
                find_required_blobs_recursive(blobfs, subpackage, found_packages).await
            {
                found.extend(subpackage_found.iter().copied());
                found_packages.insert(*subpackage, subpackage_found);
            }
        }

        Some(found)
    }
    .boxed()
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::test_utils::add_meta_far_to_blobfs,
        fuchsia_inspect::{assert_data_tree, testing::AnyProperty},
        maplit::{hashmap, hashset},
    };

    fn hash(n: u8) -> Hash {
        Hash::from([n; 32])
    }

    #[test]
    fn contains_package_returns_true_on_any_known_state() {
        let index = RetainedIndex::from_packages(hashmap! {
            hash(0) => None,
            hash(1) => Some(HashSet::new()),
            hash(2) => Some(HashSet::from_iter([hash(7), hash(8), hash(9)])),
        });

        assert!(index.contains_package(&hash(0)));
        assert!(index.contains_package(&hash(1)));
        assert!(index.contains_package(&hash(2)));

        assert!(!index.contains_package(&hash(3)));
        assert!(!index.contains_package(&hash(7)));
    }

    #[test]
    fn iter_packages_with_unknown_content_blobs_does_what_it_says_it_does() {
        let index = RetainedIndex::from_packages(hashmap! {
            hash(0) => None,
            hash(1) => Some(HashSet::new()),
            hash(2) => Some(HashSet::from_iter([hash(7), hash(8), hash(9)])),
            hash(3) => None,
        });

        assert_eq!(index.packages_with_unknown_blobs(), vec![hash(0), hash(3)]);
    }

    #[test]
    fn add_blobs_nops_if_content_blobs_are_already_known() {
        let mut index = RetainedIndex::from_packages(hashmap! {
            hash(0) => Some(HashSet::from_iter([hash(1)])),
        });

        assert!(index.add_blobs(&hash(0), &hashset! {hash(1)}));

        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(0) => Some(HashSet::from_iter([hash(1)])),
            })
        );
    }

    #[test]
    fn add_blobs_ignores_unknown_packages() {
        let mut index = RetainedIndex::from_packages(hashmap! {
            hash(0) => Some(HashSet::new()),
        });

        assert!(!index.add_blobs(&hash(1), &hashset! {}));

        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(0) => Some(HashSet::new()),
            })
        );
    }

    #[test]
    fn add_blobs_remembers_content_blobs_for_package() {
        let mut index = RetainedIndex::from_packages(hashmap! {
            hash(0) => None,
        });

        assert!(index.add_blobs(&hash(0), &hashset! {hash(1), hash(2), hash(3)}));

        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(0) => Some(HashSet::from_iter([hash(1), hash(2), hash(3)])),
            })
        );
    }

    #[test]
    fn add_blobs_add_to_existing_blobs() {
        let mut index = RetainedIndex::from_packages(hashmap! {
            hash(0) => None,
        });

        assert!(index.add_blobs(&hash(0), &hashset! {hash(1)}));
        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(0) => Some(HashSet::from_iter([hash(1)])),
            })
        );

        assert!(index.add_blobs(&hash(0), &hashset! {hash(2)}));
        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(0) => Some(HashSet::from_iter([hash(1), hash(2)])),
            })
        );
    }

    #[test]
    fn replace_retains_keys_from_other_only() {
        let mut index = RetainedIndex::from_packages(hashmap! {
            hash(0) => None,
            hash(1) => None,
            hash(2) => None,
        });
        let other = RetainedIndex::from_packages(hashmap! {
            hash(2) => None,
            hash(3) => None,
            hash(4) => None,
        });

        index.replace(other.clone());
        assert_eq!(index, other);
    }

    #[test]
    fn replace_retains_values_already_present_in_other() {
        let mut index = RetainedIndex::from_packages(hashmap! {
            hash(0) => None,
            hash(1) => None,
            hash(2) => None,
        });
        let other = RetainedIndex::from_packages(hashmap! {
            hash(2) => Some(HashSet::from_iter([ hash(10), hash(11) ])),
            hash(3) => Some(HashSet::from_iter([ hash(12), hash(13) ])),
            hash(4) => None,
        });

        index.replace(other.clone());
        assert_eq!(index, other);
    }

    #[test]
    fn replace_populates_known_values_from_self() {
        let mut index = RetainedIndex::from_packages(hashmap! {
            hash(0) => Some(HashSet::from_iter([ hash(123) ])),
            hash(1) => None,
            hash(2) => Some(HashSet::from_iter([ hash(10), hash(11) ])),
        });
        let other = RetainedIndex::from_packages(hashmap! {
            hash(2) => None,
            hash(3) => Some(HashSet::from_iter([ hash(12), hash(13) ])),
            hash(4) => None,
        });
        let merged = RetainedIndex::from_packages(hashmap! {
            hash(2) => Some(HashSet::from_iter([ hash(10), hash(11) ])),
            hash(3) => Some(HashSet::from_iter([ hash(12), hash(13) ])),
            hash(4) => None,
        });

        index.replace(other);
        assert_eq!(index, merged);
    }

    #[test]
    fn replace_allows_both_self_and_other_values_to_be_populated() {
        let mut index = RetainedIndex::from_packages(hashmap! {
            hash(2) => Some(HashSet::from_iter([ hash(10), hash(11) ])),
        });
        let other = RetainedIndex::from_packages(hashmap! {
            hash(2) => Some(HashSet::from_iter([ hash(10), hash(11) ])),
        });

        index.replace(other.clone());
        assert_eq!(index, other);
    }

    #[test]
    fn replace_extends_other_from_self() {
        let mut index = RetainedIndex::from_packages(hashmap! {
            hash(2) => Some(HashSet::from_iter([ hash(10)])),
        });
        let other = RetainedIndex::from_packages(hashmap! {
            hash(2) => Some(HashSet::from_iter([ hash(11) ])),
        });

        index.replace(other);
        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(2) => Some(HashSet::from_iter([ hash(10), hash(11) ])),
            })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn replace_index_sets_inspect() {
        let inspector = finspect::Inspector::new();

        let mut index = RetainedIndex::new(inspector.root().create_child("test-node"));

        let other = RetainedIndex::from_packages(hashmap! {
            hash(2) => Some(HashSet::from_iter([hash(10), hash(11)])),
        });

        index.replace(other);

        let hierarchy = finspect::reader::read(&inspector).await.unwrap();
        assert_data_tree!(
            hierarchy,
            "root": {
                "test-node": {
                    "last-set": AnyProperty,
                    "entries": {
                        "0202020202020202020202020202020202020202020202020202020202020202": {
                            "blobs-count": 2u64,
                            "state": "known",
                            "last-set": AnyProperty,
                        }
                    },
                    "generation": 1u64,
                }
            }
        );
    }

    #[test]
    fn all_blobs_produces_union_of_meta_and_content_hashes() {
        let index = RetainedIndex::from_packages(hashmap! {
            hash(0) => Some(HashSet::from_iter([hash(1), hash(2), hash(3)])),
            hash(4) => None,
            hash(5) => Some(HashSet::from_iter([hash(0), hash(4)])),
        });

        let expected = (0..=5).map(hash).collect::<HashSet<Hash>>();
        assert_eq!(expected.len(), 6);

        assert_eq!(expected, index.all_blobs());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn populate_retained_index_maps_missing_packages_to_unknown_content_blobs() {
        let (_blobfs_fake, blobfs) = fuchsia_pkg_testing::blobfs::Fake::new();

        let index = populate_retained_index(&blobfs, &[hash(0), hash(1), hash(2)]).await;

        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(0) => None,
                hash(1) => None,
                hash(2) => None,
            })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn populate_retained_index_maps_present_packages_to_all_content_blobs() {
        let (blobfs_fake, blobfs) = fuchsia_pkg_testing::blobfs::Fake::new();

        add_meta_far_to_blobfs(
            &blobfs_fake,
            hash(0),
            "pkg-0",
            vec![hash(1), hash(2), hash(30)],
            [],
        );
        add_meta_far_to_blobfs(
            &blobfs_fake,
            hash(10),
            "pkg-1",
            vec![hash(11), hash(12), hash(30)],
            [],
        );

        let index = populate_retained_index(&blobfs, &[hash(0), hash(10)]).await;

        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(0) => Some(HashSet::from_iter([hash(1), hash(2), hash(30)])),
                hash(10) => Some(HashSet::from_iter([hash(11), hash(12), hash(30)])),
            })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn populate_retained_index_maps_invalid_meta_far_to_unknown_content_blobs() {
        let (blobfs_fake, blobfs) = fuchsia_pkg_testing::blobfs::Fake::new();

        blobfs_fake.add_blob(hash(0), b"invalid blob");
        add_meta_far_to_blobfs(
            &blobfs_fake,
            hash(10),
            "pkg-0",
            vec![hash(1), hash(2), hash(3)],
            [],
        );

        let index = populate_retained_index(&blobfs, &[hash(0), hash(10)]).await;

        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(0) => None,
                hash(10) => Some(HashSet::from_iter([hash(1), hash(2), hash(3)])),
            })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn populate_retained_index_dedupes_content_blobs() {
        let (blobfs_fake, blobfs) = fuchsia_pkg_testing::blobfs::Fake::new();

        add_meta_far_to_blobfs(&blobfs_fake, hash(0), "pkg-0", vec![hash(1), hash(1)], []);

        let index = populate_retained_index(&blobfs, &[hash(0)]).await;

        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(0) => Some(HashSet::from_iter([hash(1)])),
            })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn populate_retained_index_adds_subpackage() {
        let (blobfs_fake, blobfs) = fuchsia_pkg_testing::blobfs::Fake::new();

        add_meta_far_to_blobfs(&blobfs_fake, hash(0), "pkg-0", [], [hash(1)]);
        add_meta_far_to_blobfs(&blobfs_fake, hash(1), "pkg-1", [hash(2)], []);

        let index = populate_retained_index(&blobfs, &[hash(0)]).await;

        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(0) => Some(HashSet::from_iter([hash(1), hash(2)])),
            })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn populate_retained_index_adds_subpackage_of_subpackage() {
        let (blobfs_fake, blobfs) = fuchsia_pkg_testing::blobfs::Fake::new();

        add_meta_far_to_blobfs(&blobfs_fake, hash(0), "pkg-0", [], [hash(1)]);
        add_meta_far_to_blobfs(&blobfs_fake, hash(1), "pkg-1", [], [hash(2)]);
        add_meta_far_to_blobfs(&blobfs_fake, hash(2), "pkg-2", [hash(3)], []);

        let index = populate_retained_index(&blobfs, &[hash(0)]).await;

        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(0) => Some(HashSet::from_iter([hash(1), hash(2), hash(3)])),
            })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn populate_retained_index_ignores_missing_subpackage() {
        let (blobfs_fake, blobfs) = fuchsia_pkg_testing::blobfs::Fake::new();

        add_meta_far_to_blobfs(&blobfs_fake, hash(0), "pkg-0", [], [hash(1), hash(2)]);
        add_meta_far_to_blobfs(&blobfs_fake, hash(2), "pkg-2", [hash(3)], []);

        let index = populate_retained_index(&blobfs, &[hash(0)]).await;

        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(0) => Some(HashSet::from_iter([hash(1), hash(2), hash(3)])),
            })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn populate_retained_index_duplicate_subpackage() {
        let (blobfs_fake, blobfs) = fuchsia_pkg_testing::blobfs::Fake::new();

        add_meta_far_to_blobfs(&blobfs_fake, hash(0), "pkg-0", [], [hash(2)]);
        add_meta_far_to_blobfs(&blobfs_fake, hash(1), "pkg-1", [], [hash(2)]);
        add_meta_far_to_blobfs(&blobfs_fake, hash(2), "pkg-2", [], [hash(3)]);

        let index = populate_retained_index(&blobfs, &[hash(0), hash(1)]).await;

        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(0) => Some(HashSet::from_iter([hash(2), hash(3)])),
                hash(1) => Some(HashSet::from_iter([hash(2), hash(3)])),
            })
        );
    }
}
