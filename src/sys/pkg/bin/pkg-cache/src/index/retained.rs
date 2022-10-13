// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_hash::Hash,
    fuchsia_inspect::{self as finspect, NumericProperty, Property},
    fuchsia_zircon as zx,
    std::collections::{HashMap, HashSet},
    tracing::error,
};

/// An index of packages considered to be part of a new system's base package set.
#[derive(Debug)]
#[cfg_attr(test, derive(Default, PartialEq, Eq))]
pub struct RetainedIndex {
    /// Map of package hash to all associated content blobs, if known.
    packages: HashMap<Hash, RetainedContentHashes>,

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
struct RetainedContentHashes {
    /// Content blobs contains no duplicates and is ordered by hash.
    content_hashes: Option<Vec<Hash>>,

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
impl Clone for RetainedContentHashes {
    fn clone(&self) -> Self {
        RetainedContentHashes { content_hashes: self.content_hashes.clone(), ..Default::default() }
    }
}

impl RetainedContentHashes {
    fn set_content_hashes(&mut self, content_hashes: Vec<Hash>) {
        self.last_set.set(zx::Time::get_monotonic().into_nanos());
        self.state.set("known");
        self.blobs_count =
            self.inspect_node.create_uint("blobs-count", content_hashes.len() as u64);
        self.content_hashes = Some(content_hashes);
    }

    fn set_inspect_node(&mut self, inspect_node: finspect::Node) {
        self.last_set = inspect_node.create_int("last-set", zx::Time::get_monotonic().into_nanos());
        if let Some(content_hashes) = &self.content_hashes {
            self.state = inspect_node.create_string("state", "known");
            self.blobs_count = inspect_node.create_uint("blobs-count", content_hashes.len() as u64);
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

    /// Produces an iterator of all packages in this index that do not yet know the content blobs
    /// associated with it.
    pub fn iter_packages_with_unknown_content_blobs(&self) -> impl Iterator<Item = Hash> + '_ {
        self.packages.iter().filter_map(|(k, v)| match v.content_hashes {
            None => Some(*k),
            Some(_) => None,
        })
    }

    #[cfg(test)]
    fn packages_with_unknown_content_blobs(&self) -> Vec<Hash> {
        let mut res = self.iter_packages_with_unknown_content_blobs().collect::<Vec<_>>();
        res.sort_unstable();
        res
    }

    #[cfg(test)]
    pub fn from_packages(packages: HashMap<Hash, Option<Vec<Hash>>>) -> Self {
        Self {
            packages: packages
                .into_iter()
                .map(|(hash, content_hashes)| {
                    (hash, RetainedContentHashes { content_hashes, ..Default::default() })
                })
                .collect(),
            ..Default::default()
        }
    }

    #[cfg(test)]
    pub fn packages(&self) -> HashMap<Hash, Option<Vec<Hash>>> {
        self.packages
            .iter()
            .map(|(&hash, packages_entry)| (hash, packages_entry.content_hashes.clone()))
            .collect()
    }

    /// Clears this index, replacing it with an empty set of retained packages
    // TODO(fxbug.dev/77363) use this, remove dead_code allow
    #[allow(dead_code)]
    pub fn clear(&mut self) {
        self.packages.clear();
    }

    /// Populates the content blobs associated with the given package hash if that package is known
    /// to the retained index, protecting those content blobs from garbage collection. Returns true
    /// if the package is known to this index.
    ///
    /// # Panics
    ///
    /// Panics if the content blobs for the given package are already known and the provided
    /// content_blobs are different.
    pub fn set_content_blobs(&mut self, meta_hash: &Hash, content_blobs: &HashSet<Hash>) -> bool {
        let packages_entry = match self.packages.get_mut(meta_hash) {
            Some(content_hashes) => content_hashes,
            None => return false,
        };

        let mut content_blobs = content_blobs.iter().copied().collect::<Vec<Hash>>();
        content_blobs.sort_unstable();
        let content_blobs = content_blobs;

        if let Some(hashes) = &packages_entry.content_hashes {
            assert_eq!(
                hashes, &content_blobs,
                "index already has content blobs for {}, and the new set is different",
                meta_hash
            );
        }

        packages_entry.set_content_hashes(content_blobs);
        true
    }

    /// Replaces this retained index instance with other, populating other's content blob sets
    /// using data from `self` when possible.
    ///
    /// # Panics
    ///
    /// Panics if `self` and `other` know of a package's content blobs but disagree as to what they
    /// are.
    pub fn replace(&mut self, mut other: Self) {
        // Update inspect nodes for newly added content blob sets.
        for (meta_hash, packages_entry) in other.packages.iter_mut() {
            match self.packages.get_mut(&meta_hash) {
                // The package already present in retained index.
                Some(_) => {}

                // New package to be added to retained index.
                None => {
                    packages_entry.set_inspect_node(
                        self.inspect.packages_inspect_node.create_child(meta_hash.to_string()),
                    );
                }
            }
        }

        // Prepopulate missing content blob sets in other from known data in self
        for (old_meta_hash, packages_entry) in self.packages.drain() {
            // Skip unpopulated entries.
            if packages_entry.content_hashes.is_none() {
                continue;
            }

            match other.packages.get_mut(&old_meta_hash) {
                None => {
                    // Package is being removed from the retained index.
                }
                Some(new_packages_entry @ RetainedContentHashes { content_hashes: None, .. }) => {
                    // New package is added to retained index, and content hashes are already
                    // known. Move that data into the new index, along with the inspect data.
                    *new_packages_entry = packages_entry;
                }

                Some(
                    new_packages_entry @ RetainedContentHashes { content_hashes: Some(_), .. },
                ) => {
                    // Both indices know the content hashes for this package. They should be the
                    // same.
                    assert_eq!(packages_entry.content_hashes, new_packages_entry.content_hashes);
                    // Move packages entry to preserve inspect state.
                    *new_packages_entry = packages_entry;
                }
            }
        }

        self.packages = other.packages;
        self.inspect.generation.add(1);
        self.inspect.last_set.set(zx::Time::get_monotonic().into_nanos());
    }

    /// Returns the set of all blobs currently protected by the retained index.
    pub fn all_blobs(&self) -> HashSet<Hash> {
        self.packages
            .iter()
            .flat_map(|(meta_hash, content_hashes)| {
                std::iter::once(meta_hash).chain(content_hashes.content_hashes.iter().flatten())
            })
            .copied()
            .collect()
    }
}

/// Constructs a new [`RetainedIndex`] from the given blobfs client and set of package meta far
/// hashes, populating content blob hashes for any packages with a meta far present in blobfs.
pub async fn populate_retained_index(
    blobfs: &blobfs::Client,
    meta_hashes: &[Hash],
) -> RetainedIndex {
    let mut packages = HashMap::with_capacity(meta_hashes.len());
    for meta_hash in meta_hashes {
        let content_hashes = match package_directory::RootDir::new(blobfs.clone(), *meta_hash).await
        {
            Ok(root_dir) => {
                let mut hashes = root_dir.external_file_hashes().copied().collect::<Vec<_>>();
                hashes.sort_unstable();
                hashes.dedup();
                Some(hashes)
            }
            Err(package_directory::Error::MissingMetaFar) => None,
            Err(e) => {
                // The package isn't readable yet, so the system updater will need to fetch it.
                // Assume None for now and let the package fetch flow populate this later.
                error!(
                    "failed to enumerate content blobs for package {}: {:#}",
                    meta_hash,
                    anyhow::anyhow!(e)
                );
                None
            }
        };
        let content_hashes = RetainedContentHashes { content_hashes, ..Default::default() };
        packages.insert(*meta_hash, content_hashes);
    }

    let inspect = RetainedIndexInspect::default();
    RetainedIndex { packages, inspect }
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
    fn clear_removes_all_data() {
        let empty = RetainedIndex::default();
        let mut index = RetainedIndex::from_packages(hashmap! {
            hash(0) => Some(vec![hash(1), hash(2), hash(3)]),
            hash(4) => None,
            hash(5) => Some(vec![hash(0), hash(4)]),
        });

        index.clear();
        assert_eq!(index, empty);
    }

    #[test]
    fn contains_package_returns_true_on_any_known_state() {
        let index = RetainedIndex::from_packages(hashmap! {
            hash(0) => None,
            hash(1) => Some(vec![]),
            hash(2) => Some(vec![hash(7), hash(8), hash(9)]),
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
            hash(1) => Some(vec![]),
            hash(2) => Some(vec![hash(7), hash(8), hash(9)]),
            hash(3) => None,
        });

        assert_eq!(index.packages_with_unknown_content_blobs(), vec![hash(0), hash(3)]);
    }

    #[test]
    fn set_content_blobs_nops_if_content_blobs_are_already_known() {
        let mut index = RetainedIndex::from_packages(hashmap! {
            hash(0) => Some(vec![hash(1)]),
        });

        assert!(index.set_content_blobs(&hash(0), &hashset! {hash(1)}));

        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(0) => Some(vec![hash(1)]),
            })
        );
    }

    #[test]
    fn set_content_blobs_ignores_unknown_packages() {
        let mut index = RetainedIndex::from_packages(hashmap! {
            hash(0) => Some(vec![]),
        });

        assert!(!index.set_content_blobs(&hash(1), &hashset! {}));

        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(0) => Some(vec![]),
            })
        );
    }

    #[test]
    fn set_content_blobs_remembers_content_blobs_for_package() {
        let mut index = RetainedIndex::from_packages(hashmap! {
            hash(0) => None,
        });

        assert!(index.set_content_blobs(&hash(0), &hashset! {hash(1), hash(2), hash(3)}));

        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(0) => Some(vec![hash(1), hash(2), hash(3)]),
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
            hash(2) => Some(vec![ hash(10), hash(11) ]),
            hash(3) => Some(vec![ hash(12), hash(13) ]),
            hash(4) => None,
        });

        index.replace(other.clone());
        assert_eq!(index, other);
    }

    #[test]
    fn replace_populates_known_values_from_self() {
        let mut index = RetainedIndex::from_packages(hashmap! {
            hash(0) => Some(vec![ hash(123) ]),
            hash(1) => None,
            hash(2) => Some(vec![ hash(10), hash(11) ]),
        });
        let other = RetainedIndex::from_packages(hashmap! {
            hash(2) => None,
            hash(3) => Some(vec![ hash(12), hash(13) ]),
            hash(4) => None,
        });
        let merged = RetainedIndex::from_packages(hashmap! {
            hash(2) => Some(vec![ hash(10), hash(11) ]),
            hash(3) => Some(vec![ hash(12), hash(13) ]),
            hash(4) => None,
        });

        index.replace(other);
        assert_eq!(index, merged);
    }

    #[test]
    fn replace_allows_both_self_and_other_values_to_be_populated() {
        let mut index = RetainedIndex::from_packages(hashmap! {
            hash(2) => Some(vec![ hash(10), hash(11) ]),
        });
        let other = RetainedIndex::from_packages(hashmap! {
            hash(2) => Some(vec![ hash(10), hash(11) ]),
        });

        index.replace(other.clone());
        assert_eq!(index, other);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn replace_index_containing_blobs_preserves_inspect_when_blobs_known() {
        let inspector = finspect::Inspector::new();
        let node = inspector.root().create_child("test-node");

        let mut index = RetainedIndex::from_packages(hashmap! {
            hash(2) => Some(vec![ hash(10), hash(11) ]),
        });
        if let Some(content_hashes) = index.packages.get_mut(&hash(2)) {
            content_hashes.set_inspect_node(node);
        }
        let other = RetainedIndex::from_packages(hashmap! {
            hash(2) => Some(vec![ hash(10), hash(11) ]),
        });

        index.replace(other);
        let hierarchy = finspect::reader::read(&inspector).await.unwrap();
        assert_data_tree!(
            hierarchy,
            root: contains {
                "test-node": {
                    "last-set": AnyProperty,
                    "state": "known",
                    "blobs-count": 2u64,
                }
            }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn replace_index_preserves_inspect_when_blobs_empty() {
        let inspector = finspect::Inspector::new();
        let node = inspector.root().create_child("test-node");

        let mut index = RetainedIndex::from_packages(hashmap! {
            hash(2) => Some(vec![ hash(10), hash(11) ]),
        });
        if let Some(content_hashes) = index.packages.get_mut(&hash(2)) {
            content_hashes.set_inspect_node(node);
        }
        let other = RetainedIndex::from_packages(hashmap! {
            hash(2) => None,
        });

        index.replace(other);
        let hierarchy = finspect::reader::read(&inspector).await.unwrap();
        assert_data_tree!(
            hierarchy,
            root: contains {
                "test-node": {
                    "last-set": AnyProperty,
                    "state": "known",
                    "blobs-count": 2u64,
                }
            }
        );
    }

    #[test]
    fn all_blobs_produces_union_of_meta_and_content_hashes() {
        let index = RetainedIndex::from_packages(hashmap! {
            hash(0) => Some(vec![hash(1), hash(2), hash(3)]),
            hash(4) => None,
            hash(5) => Some(vec![hash(0), hash(4)]),
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

        add_meta_far_to_blobfs(&blobfs_fake, hash(0), "pkg-0", vec![hash(1), hash(2), hash(30)]);
        add_meta_far_to_blobfs(&blobfs_fake, hash(10), "pkg-1", vec![hash(11), hash(12), hash(30)]);

        let index = populate_retained_index(&blobfs, &[hash(0), hash(10)]).await;

        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(0) => Some(vec![hash(1), hash(2), hash(30)]),
                hash(10) => Some(vec![hash(11), hash(12), hash(30)]),
            })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn populate_retained_index_maps_invalid_meta_far_to_unknown_content_blobs() {
        let (blobfs_fake, blobfs) = fuchsia_pkg_testing::blobfs::Fake::new();

        blobfs_fake.add_blob(hash(0), b"invalid blob");
        add_meta_far_to_blobfs(&blobfs_fake, hash(10), "pkg-0", vec![hash(1), hash(2), hash(3)]);

        let index = populate_retained_index(&blobfs, &[hash(0), hash(10)]).await;

        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(0) => None,
                hash(10) => Some(vec![hash(1), hash(2), hash(3)]),
            })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn populate_retained_index_dedupes_content_blobs() {
        let (blobfs_fake, blobfs) = fuchsia_pkg_testing::blobfs::Fake::new();

        add_meta_far_to_blobfs(&blobfs_fake, hash(0), "pkg-0", vec![hash(1), hash(1)]);

        let index = populate_retained_index(&blobfs, &[hash(0)]).await;

        assert_eq!(
            index,
            RetainedIndex::from_packages(hashmap! {
                hash(0) => Some(vec![hash(1)]),
            })
        );
    }
}
