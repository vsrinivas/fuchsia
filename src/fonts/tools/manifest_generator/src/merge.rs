// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Traits for mergeable structs and collections of font metadata.

use {
    anyhow::Error,
    itertools::Itertools,
    std::{fmt::Debug, hash::Hash},
    thiserror::Error,
};

/// Indicates that multiple items that implement this trait should be merged if they all have the
/// same key.
///
/// See [crate::font_catalog::Family] for example.
pub(crate) trait TryMerge:
    Clone + Debug + Eq + Hash + Sync + Send + Sized + 'static
{
    type Key: Clone + Hash + Eq + PartialEq + Ord + PartialOrd + 'static;

    /// The key by which to group the items.
    fn key(&self) -> Self::Key;

    /// Returns `true` for two items about to be merged if the fields are expected to match up
    /// between them do in fact match up.
    ///
    /// The default implementation just delegates to `==`, but this can be made narrower.
    ///
    /// Usually, overriders will want to exclude the `key` field and any collection fields that will
    /// be merged.
    fn has_matching_fields(&self, other: &Self) -> bool {
        self == other
    }

    /// Merges a group of items into one. At this point, it has already been confirmed that all the
    /// items in the group have matching fields, as defined by [`has_matching_fields`].
    fn try_merge_group(group: Vec<Self>) -> Result<Self, Error>;

    /// Perform validation on the entire set of merged groups.
    ///
    /// The default implementation is a no-op.
    fn post_validate(groups: Vec<Self>) -> Result<Vec<Self>, MergeError<Self>> {
        Ok(groups)
    }
}

/// Try to merge multiple items into a single one. If there are any inconsistencies in the
/// fields that are expected to be identical, an error will be returned.
fn try_match_fields_and_merge_group<T>(group: impl IntoIterator<Item = T>) -> Result<T, Error>
where
    T: TryMerge,
{
    let group: Vec<T> = group.into_iter().collect();

    if !all_have_matching_fields(&group) {
        Err(MergeError::Conflict(group).into())
    } else {
        TryMerge::try_merge_group(group)
    }
}

/// Returns true if all the items in the given group return `true` for
/// [`TryMerge::has_matching_fields`] when compared to the first item in the group.
fn all_have_matching_fields<T>(group: &Vec<T>) -> bool
where
    T: TryMerge,
{
    let mut iter = group.iter();
    let first = iter.next();
    if first.is_none() {
        true
    } else {
        let first = first.unwrap();
        iter.all(|item| item.has_matching_fields(first))
    }
}

/// Import this trait to allow the use of [`TryMergeGroups::try_merge_groups`] on an `Iterator`.
pub(crate) trait TryMergeGroups
where
    Self: Iterator + Sized,
    Self::Item: TryMerge,
{
    /// For an iterator over a list of items with possible duplicates or mergeable items, removes
    /// duplicates, attempts to merge overlapping items, and sorts by the items' keys.
    fn try_merge_groups(self) -> Result<Vec<Self::Item>, Error> {
        let merged: Result<Vec<Self::Item>, _> = self
            .unique()
            .map(|item| (item.key(), item))
            .into_group_map()
            .into_iter()
            .map(|(_key, items)| try_match_fields_and_merge_group(items))
            .collect();

        let mut merged = merged?;
        merged.sort_by_key(|item| item.key());
        TryMerge::post_validate(merged).map_err(|e| e.into())
    }
}

/// Blanket implementation of `TryMergeGroups`.
impl<T, V> TryMergeGroups for T
where
    T: Iterator<Item = V>,
    V: TryMerge,
{
}

/// Errors while merging a group of items.
#[derive(Debug, Error)]
pub(crate) enum MergeError<V>
where
    V: Debug + Send + Sync + 'static,
{
    #[error("Conflict when attempting to merge {:?}", _0)]
    Conflict(Vec<V>),
    #[error("Post validation failed with [{}] on list {:?}", _0, _1)]
    PostInvalid(String, Vec<V>),
}
