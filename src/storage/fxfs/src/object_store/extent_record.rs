// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/96139): need validation after deserialization.

use {
    crate::lsm_tree::types::{OrdLowerBound, OrdUpperBound},
    serde::{Deserialize, Serialize},
    std::cmp::{max, min},
    std::ops::Range,
};

/// The common case for extents which cover the data payload of some object.
pub const DEFAULT_DATA_ATTRIBUTE_ID: u64 = 0;

/// ExtentKey is a child of ObjectKey for Object attributes that have attached extents
/// (at time of writing this was only the used for file contents).
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[cfg_attr(fuzz, derive(arbitrary::Arbitrary))]
pub struct ExtentKey {
    pub range: Range<u64>,
}

impl ExtentKey {
    /// Creates an ExtentKey.
    pub fn new(range: std::ops::Range<u64>) -> Self {
        Self { range }
    }

    /// Returns the range of bytes common between this extent and |other|.
    pub fn overlap(&self, other: &ExtentKey) -> Option<Range<u64>> {
        if self.range.end <= other.range.start || self.range.start >= other.range.end {
            None
        } else {
            Some(max(self.range.start, other.range.start)..min(self.range.end, other.range.end))
        }
    }

    /// Returns the search key for this extent; that is, a key which is <= this key under Ord and
    /// OrdLowerBound.
    /// This would be used when searching for an extent with |find| (when we want to find any
    /// overlapping extent, which could include extents that start earlier).
    /// For example, if the tree has extents 50..150 and 150..200 and we wish to read 100..200,
    /// we'd search for 0..101 which would set the iterator to 50..150.
    pub fn search_key(&self) -> Self {
        assert_ne!(self.range.start, self.range.end);
        ExtentKey::search_key_from_offset(self.range.start)
    }

    /// Similar to previous, but from an offset.  Returns a search key that will find the first
    /// extent that touches offset..
    pub fn search_key_from_offset(offset: u64) -> Self {
        Self { range: 0..offset + 1 }
    }

    /// Returns the merge key for this extent; that is, a key which is <= this extent and any other
    /// possibly overlapping extent, under Ord. This would be used to set the hint for |merge_into|.
    ///
    /// For example, if the tree has extents 0..50, 50..150 and 150..200 and we wish to insert
    /// 100..150, we'd use a merge hint of 0..100 which would set the iterator to 50..150 (the first
    /// element > 100..150 under Ord).
    pub fn key_for_merge_into(&self) -> Self {
        Self { range: 0..self.range.start }
    }
}

// The normal comparison uses the end of the range before the start of the range. This makes
// searching for records easier because it's easy to find K.. (where K is the key you are searching
// for), which is what we want since our search routines find items with keys >= a search key.
// OrdLowerBound orders by the start of an extent.
impl OrdUpperBound for ExtentKey {
    fn cmp_upper_bound(&self, other: &ExtentKey) -> std::cmp::Ordering {
        // The comparison uses the end of the range so that we can more easily do queries.  Whilst
        // it might be tempting to break ties by comparing the range start, next_key currently
        // relies on keys with the same end being equal, and since we do not support overlapping
        // keys within the same layer, ties can always be broken using layer index.  Insertions into
        // the mutable layer should always be done using merge_into, which will ensure keys don't
        // end up overlapping.
        self.range.end.cmp(&other.range.end)
    }
}

impl OrdLowerBound for ExtentKey {
    // Orders by the start of the range rather than the end, and doesn't include the end in the
    // comparison. This is used when merging, where we want to merge keys in lower-bound order.
    fn cmp_lower_bound(&self, other: &ExtentKey) -> std::cmp::Ordering {
        self.range.start.cmp(&other.range.start)
    }
}

impl Ord for ExtentKey {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        // We expect cmp_upper_bound and cmp_lower_bound to be used mostly, but ObjectKey needs an
        // Ord method in order to compare other enum variants, and Transaction requires an ObjectKey
        // to implement Ord.
        self.range.start.cmp(&other.range.start).then(self.range.end.cmp(&other.range.end))
    }
}

impl PartialOrd for ExtentKey {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[cfg_attr(fuzz, derive(arbitrary::Arbitrary))]
pub enum Checksums {
    None,
    /// A vector of checksums, one per block.
    Fletcher(Vec<u64>),
}

impl Checksums {
    pub fn split_off(&mut self, at: usize) -> Checksums {
        match self {
            Checksums::None => Checksums::None,
            Checksums::Fletcher(sums) => Checksums::Fletcher(sums.split_off(at)),
        }
    }
}

/// ExtentValue is the payload for an extent in the object store, which describes where the extent
/// is physically located.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[cfg_attr(fuzz, derive(arbitrary::Arbitrary))]
pub enum ExtentValue {
    /// Indicates a deleted extent; that is, the logical range described by the extent key is
    /// considered to be deleted.
    None,
    /// The location of the extent and other related information.  `key_id` identifies which
    /// of the object's keys should be used.  `checksums` hold post-encryption checksums.
    Some { device_offset: u64, checksums: Checksums, key_id: u64 },
}

impl ExtentValue {
    pub fn new(device_offset: u64) -> ExtentValue {
        ExtentValue::Some { device_offset, checksums: Checksums::None, key_id: 0 }
    }

    /// Creates an ExtentValue with a checksum
    pub fn with_checksum(device_offset: u64, checksums: Checksums) -> ExtentValue {
        ExtentValue::Some { device_offset, checksums, key_id: 0 }
    }

    /// Creates an ObjectValue for a deletion of an object extent.
    pub fn deleted_extent() -> ExtentValue {
        ExtentValue::None
    }

    pub fn is_deleted(&self) -> bool {
        if let ExtentValue::None = self {
            true
        } else {
            false
        }
    }

    /// Returns a new ExtentValue offset by `amount`.  Both `amount` and `extent_len` must be
    /// multiples of the underlying block size.
    pub fn offset_by(&self, amount: u64, extent_len: u64) -> Self {
        match self {
            ExtentValue::None => Self::deleted_extent(),
            ExtentValue::Some { device_offset, checksums, key_id } => {
                if let Checksums::Fletcher(checksums) = checksums {
                    if checksums.len() > 0 {
                        let index = (amount / (extent_len / checksums.len() as u64)) as usize;
                        return ExtentValue::Some {
                            device_offset: device_offset + amount,
                            checksums: Checksums::Fletcher(checksums[index..].to_vec()),
                            key_id: *key_id,
                        };
                    }
                }
                ExtentValue::Some {
                    device_offset: device_offset + amount,
                    checksums: Checksums::None,
                    key_id: *key_id,
                }
            }
        }
    }

    /// Returns a new ExtentValue after shrinking the extent from |original_len| to |new_len|.
    pub fn shrunk(&self, original_len: u64, new_len: u64) -> Self {
        match self {
            ExtentValue::None => Self::deleted_extent(),
            ExtentValue::Some { device_offset, checksums, key_id } => {
                if let Checksums::Fletcher(checksums) = checksums {
                    if checksums.len() > 0 {
                        let checksum_len =
                            (new_len / (original_len / checksums.len() as u64)) as usize;
                        return ExtentValue::Some {
                            device_offset: *device_offset,
                            checksums: Checksums::Fletcher(checksums[..checksum_len].to_vec()),
                            key_id: *key_id,
                        };
                    }
                }
                ExtentValue::Some {
                    device_offset: *device_offset,
                    checksums: Checksums::None,
                    key_id: *key_id,
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::ExtentKey,
        crate::lsm_tree::types::{OrdLowerBound, OrdUpperBound},
        std::cmp::Ordering,
    };

    #[test]
    fn test_extent_cmp() {
        let extent = ExtentKey::new(100..150);
        assert_eq!(extent.cmp_upper_bound(&ExtentKey::new(0..100)), Ordering::Greater);
        assert_eq!(extent.cmp_upper_bound(&ExtentKey::new(0..110)), Ordering::Greater);
        assert_eq!(extent.cmp_upper_bound(&ExtentKey::new(0..150)), Ordering::Equal);
        assert_eq!(extent.cmp_upper_bound(&ExtentKey::new(99..150)), Ordering::Equal);
        assert_eq!(extent.cmp_upper_bound(&ExtentKey::new(100..150)), Ordering::Equal);
        assert_eq!(extent.cmp_upper_bound(&ExtentKey::new(0..151)), Ordering::Less);
        assert_eq!(extent.cmp_upper_bound(&ExtentKey::new(100..151)), Ordering::Less);
        assert_eq!(extent.cmp_upper_bound(&ExtentKey::new(150..1000)), Ordering::Less);
    }

    #[test]
    fn test_extent_cmp_lower_bound() {
        let extent = ExtentKey::new(100..150);
        assert_eq!(extent.cmp_lower_bound(&ExtentKey::new(0..100)), Ordering::Greater);
        assert_eq!(extent.cmp_lower_bound(&ExtentKey::new(0..110)), Ordering::Greater);
        assert_eq!(extent.cmp_lower_bound(&ExtentKey::new(0..150)), Ordering::Greater);
        assert_eq!(extent.cmp_lower_bound(&ExtentKey::new(0..1000)), Ordering::Greater);
        assert_eq!(extent.cmp_lower_bound(&ExtentKey::new(99..1000)), Ordering::Greater);
        assert_eq!(extent.cmp_lower_bound(&ExtentKey::new(100..150)), Ordering::Equal);
        // cmp_lower_bound does not check the upper bound of the range
        assert_eq!(extent.cmp_lower_bound(&ExtentKey::new(100..1000)), Ordering::Equal);
        assert_eq!(extent.cmp_lower_bound(&ExtentKey::new(101..102)), Ordering::Less);
    }

    #[test]
    fn test_extent_search_and_insertion_key() {
        let extent = ExtentKey::new(100..150);
        assert_eq!(extent.search_key(), ExtentKey::new(0..101));
        assert_eq!(extent.cmp_lower_bound(&extent.search_key()), Ordering::Greater);
        assert_eq!(extent.cmp_upper_bound(&extent.search_key()), Ordering::Greater);
        assert_eq!(extent.key_for_merge_into(), ExtentKey::new(0..100));
        assert_eq!(extent.cmp_lower_bound(&extent.key_for_merge_into()), Ordering::Greater);
    }
}
