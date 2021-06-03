// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_hash::Hash,
    fuchsia_syslog::fx_log_err,
    std::collections::{HashMap, HashSet},
};

/// An index of packages considered to be part of a new system's base package set.
#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq, Eq, Clone))]
pub struct RetainedIndex {
    /// map of package hash to all associated content blobs, if known.
    /// content blobs contains no duplicates and is ordered by hash.
    packages: HashMap<Hash, Option<Vec<Hash>>>,
}

/// An error encountered while setting the content blobs associated with a package.
#[derive(thiserror::Error, Debug, PartialEq, Eq)]
pub enum SetContentBlobsError {
    #[error("{0} is not a package in the retained index")]
    UnknownPackage(Hash),

    #[error("{0} already has known content blobs")]
    Collision(Hash),
}

impl RetainedIndex {
    /// Clears this index, replacing it with an empty set of retained packages
    pub fn clear(&mut self) {
        self.packages.clear();
    }

    /// Populates the content blobs associated with the given package hash, protecting those
    /// content blobs from garbage collection.  It is an error to set the content blobs for a
    /// package that is not in the index or to set the content blobs for a package that already has
    /// its content blobs known.
    ///
    /// # Panics
    ///
    /// Panics if the content blobs for the given package are already known and the provided
    /// content_blobs are different.
    pub fn set_content_blobs(
        &mut self,
        meta_hash: &Hash,
        content_blobs: &HashSet<Hash>,
    ) -> Result<(), SetContentBlobsError> {
        let mut content_blobs = content_blobs.iter().copied().collect::<Vec<Hash>>();
        content_blobs.sort_unstable();
        let content_blobs = content_blobs;

        let content_hashes: &mut Option<Vec<Hash>> = self
            .packages
            .get_mut(meta_hash)
            .ok_or_else(|| SetContentBlobsError::UnknownPackage(*meta_hash))?;

        if let Some(hashes) = content_hashes {
            // TODO(fxbug.dev/77361) may need this path to silently ignore duplicate sets, or
            // provide an API to see if this is already Some().
            assert_eq!(hashes, &content_blobs);
            return Err(SetContentBlobsError::Collision(*meta_hash));
        }

        *content_hashes = Some(content_blobs);
        Ok(())
    }

    /// Replaces this retained index instance with other, populating other's content blob sets
    /// using data from `self` when possible.
    ///
    /// # Panics
    ///
    /// Panics if `self` and `other` know of a package's content blobs but disagree as to what they
    /// are.
    pub fn replace(&mut self, mut other: Self) {
        // Prepopulate missing content blob sets in other from known data in self
        for (old_meta_hash, old_content_hashes) in self.packages.drain() {
            // Skip unpopulated entries.
            let old_content_hashes = match old_content_hashes {
                Some(hashes) => hashes,
                None => continue,
            };

            match other.packages.get_mut(&old_meta_hash) {
                None => {
                    // Package is being removed from the retained index.
                }
                Some(new_content_hashes @ None) => {
                    // We already know the content hashes for this package, move that data into the
                    // new index.
                    *new_content_hashes = Some(old_content_hashes);
                }
                Some(Some(new_content_hashes)) => {
                    // Both indices know the content hashes for this package. They should be the
                    // same.
                    assert_eq!(&old_content_hashes, new_content_hashes);
                }
            }
        }

        *self = other;
    }

    /// Returns the set of all blobs currently protected by the retained index.
    pub fn all_blobs(&self) -> HashSet<Hash> {
        self.packages
            .iter()
            .flat_map(|(meta_hash, content_hashes)| {
                std::iter::once(meta_hash).chain(content_hashes.into_iter().flatten())
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
        let content_hashes =
            match crate::dynamic_index::enumerate_package_blobs(blobfs, meta_hash).await {
                Ok(Some((_path, content_hashes))) => {
                    let mut content_hashes = content_hashes.iter().copied().collect::<Vec<Hash>>();
                    content_hashes.sort_unstable();
                    Some(content_hashes)
                }
                Ok(None) => None,
                Err(e) => {
                    // The package isn't readable yet, so the system updater will need to fetch it.
                    // Assume None for now and let the package fetch flow populate this later.
                    fx_log_err!(
                        "failed to enumerate content blobs for package {}: {:#}",
                        meta_hash,
                        anyhow::anyhow!(e)
                    );
                    None
                }
            };

        packages.insert(*meta_hash, content_hashes);
    }

    RetainedIndex { packages }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::test_utils::add_meta_far_to_blobfs,
        maplit::{hashmap, hashset},
    };

    fn hash(n: u8) -> Hash {
        Hash::from([n; 32])
    }

    #[test]
    fn clear_removes_all_data() {
        let empty = RetainedIndex { packages: hashmap! {} };
        let mut index = RetainedIndex {
            packages: hashmap! {
                hash(0) => Some(vec![hash(1), hash(2), hash(3)]),
                hash(4) => None,
                hash(5) => Some(vec![hash(0), hash(4)]),
            },
        };

        index.clear();
        assert_eq!(index, empty);
    }

    #[test]
    fn set_content_blobs_fails_if_content_blobs_are_already_known() {
        let mut index = RetainedIndex {
            packages: hashmap! {
                hash(0) => Some(vec![]),
            },
        };

        assert_eq!(
            index.set_content_blobs(&hash(0), &hashset! {}),
            Err(SetContentBlobsError::Collision(hash(0)))
        );
    }

    #[test]
    fn set_content_blobs_fails_if_meta_far_is_unknown() {
        let mut index = RetainedIndex {
            packages: hashmap! {
                hash(0) => Some(vec![]),
            },
        };

        assert_eq!(
            index.set_content_blobs(&hash(1), &hashset! {}),
            Err(SetContentBlobsError::UnknownPackage(hash(1)))
        );
    }

    #[test]
    fn set_content_blobs_remembers_content_blobs_for_package() {
        let mut index = RetainedIndex {
            packages: hashmap! {
                hash(0) => None,
            },
        };

        let () = index.set_content_blobs(&hash(0), &hashset! {hash(1), hash(2), hash(3)}).unwrap();

        assert_eq!(
            index,
            RetainedIndex {
                packages: hashmap! {
                    hash(0) => Some(vec![hash(1), hash(2), hash(3)]),
                },
            }
        );
    }

    #[test]
    fn replace_retains_keys_from_other_only() {
        let mut index = RetainedIndex {
            packages: hashmap! {
                hash(0) => None,
                hash(1) => None,
                hash(2) => None,
            },
        };
        let other = RetainedIndex {
            packages: hashmap! {
                hash(2) => None,
                hash(3) => None,
                hash(4) => None,
            },
        };

        index.replace(other.clone());
        assert_eq!(index, other);
    }

    #[test]
    fn replace_retains_values_already_present_in_other() {
        let mut index = RetainedIndex {
            packages: hashmap! {
                hash(0) => None,
                hash(1) => None,
                hash(2) => None,
            },
        };
        let other = RetainedIndex {
            packages: hashmap! {
                hash(2) => Some(vec![ hash(10), hash(11) ]),
                hash(3) => Some(vec![ hash(12), hash(13) ]),
                hash(4) => None,
            },
        };

        index.replace(other.clone());
        assert_eq!(index, other);
    }

    #[test]
    fn replace_populates_known_values_from_self() {
        let mut index = RetainedIndex {
            packages: hashmap! {
                hash(0) => Some(vec![ hash(123) ]),
                hash(1) => None,
                hash(2) => Some(vec![ hash(10), hash(11) ]),
            },
        };
        let other = RetainedIndex {
            packages: hashmap! {
                hash(2) => None,
                hash(3) => Some(vec![ hash(12), hash(13) ]),
                hash(4) => None,
            },
        };
        let merged = RetainedIndex {
            packages: hashmap! {
                hash(2) => Some(vec![ hash(10), hash(11) ]),
                hash(3) => Some(vec![ hash(12), hash(13) ]),
                hash(4) => None,
            },
        };

        index.replace(other);
        assert_eq!(index, merged);
    }

    #[test]
    fn replace_allows_both_self_and_other_values_to_be_populated() {
        let mut index = RetainedIndex {
            packages: hashmap! {
                hash(2) => Some(vec![ hash(10), hash(11) ]),
            },
        };
        let other = RetainedIndex {
            packages: hashmap! {
                hash(2) => Some(vec![ hash(10), hash(11) ]),
            },
        };

        index.replace(other.clone());
        assert_eq!(index, other);
    }

    #[test]
    fn all_blobs_produces_union_of_meta_and_content_hashes() {
        let index = RetainedIndex {
            packages: hashmap! {
                hash(0) => Some(vec![hash(1), hash(2), hash(3)]),
                hash(4) => None,
                hash(5) => Some(vec![hash(0), hash(4)]),
            },
        };

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
            RetainedIndex {
                packages: hashmap! {
                    hash(0) => None,
                    hash(1) => None,
                    hash(2) => None,
                },
            }
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
            RetainedIndex {
                packages: hashmap! {
                    hash(0) => Some(vec![hash(1), hash(2), hash(30)]),
                    hash(10) => Some(vec![hash(11), hash(12), hash(30)]),
                },
            }
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
            RetainedIndex {
                packages: hashmap! {
                    hash(0) => None,
                    hash(10) => Some(vec![hash(1), hash(2), hash(3)]),
                },
            }
        );
    }
}
