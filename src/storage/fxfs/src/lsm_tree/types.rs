// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::lsm_tree::merge,
    anyhow::Error,
    async_trait::async_trait,
    serde::{Deserialize, Serialize},
    std::sync::Arc,
};

// Keys and values need to implement the following traits. For merging, they also need to implement
// OrdLowerBound.
// TODO: Use trait_alias when available.
pub trait Key:
    std::cmp::Ord
    + std::fmt::Debug
    + Send
    + Sync
    + std::marker::Unpin
    + serde::de::DeserializeOwned
    + serde::Serialize
    + 'static
{
}
impl<K> Key for K where
    K: std::cmp::Ord
        + std::fmt::Debug
        + Send
        + Sync
        + std::marker::Unpin
        + serde::de::DeserializeOwned
        + serde::Serialize
        + 'static
{
}

pub trait Value:
    std::fmt::Debug
    + Send
    + Sync
    + serde::de::DeserializeOwned
    + serde::Serialize
    + std::marker::Unpin
    + 'static
{
}
impl<V> Value for V where
    V: std::fmt::Debug
        + Send
        + Sync
        + std::marker::Unpin
        + serde::de::DeserializeOwned
        + serde::Serialize
        + 'static
{
}

/// ItemRef is a struct that contains references to key and value, which is useful since in many
/// cases since keys and values are stored separately so &Item is not possible.
#[derive(Debug, Eq, PartialEq, Serialize)]
pub struct ItemRef<'a, K, V> {
    pub key: &'a K,
    pub value: &'a V,
}

impl<'a, K, V> Clone for ItemRef<'a, K, V> {
    fn clone(&self) -> Self {
        ItemRef { key: self.key, value: self.value }
    }
}
impl<'a, K, V> Copy for ItemRef<'a, K, V> {}

/// Item is a struct that combines a key and a value.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct Item<K, V> {
    pub key: K,
    pub value: V,
}

impl<K, V> Item<K, V> {
    pub fn new(key: K, value: V) -> Item<K, V> {
        Item { key, value }
    }

    pub fn as_item_ref(&self) -> ItemRef<'_, K, V> {
        self.into()
    }
}

impl<'a, K, V> From<&'a Item<K, V>> for ItemRef<'a, K, V> {
    fn from(item: &'a Item<K, V>) -> ItemRef<'a, K, V> {
        ItemRef { key: &item.key, value: &item.value }
    }
}

/// The find functions will return items with keys that are greater-than or equal to the search key,
/// so for keys that are like extents, the keys should sort (via std::cmp::Ord) using the end of
/// their ranges, and you should set the search key accordingly.
///
/// For example, let's say the tree holds an extent with range, 100..200 and you want to perform a
/// read for range 150..250, you should search for 150..151 and that will return the extent
/// 150..250. When merging, keys can overlap, so consider the case where we want to merge an extent
/// with range 100..300 with an existing extent of 200..250. In that case, we want to treat the
/// extent with range 100..300 as lower than the key 200..250 because we'll likely want to split the
/// extents (e.g. perhaps we want 100..200, 200..250, 250..300), so for merging, we need to use a
/// different comparison function and we deal with that using the OrdLowerBound trait.
///
/// If your keys don't have overlapping ranges that need to be merged, then this can be the same as
/// std::cmp::Ord.
pub trait OrdLowerBound {
    fn cmp_lower_bound(&self, other: &Self) -> std::cmp::Ordering;
}

/// Layer is a trait that all layers need to implement (mutable and immutable).
pub trait Layer<K, V>: Send + Sync {
    /// Returns an iterator for the layer. The iterator that is returned is positioned prior to the
    /// first item in the layer. The caller needs to call advance() or seek() to position the
    /// iterator on an item.
    fn get_iterator(&self) -> BoxedLayerIterator<'_, K, V>;
}

/// MutableLayer is a trait that only mutable layers need to implement.
#[async_trait]
pub trait MutableLayer<K, V>: Layer<K, V> {
    fn as_layer(self: Arc<Self>) -> Arc<dyn Layer<K, V>>;

    /// Inserts the given item into the layer. The item *must* not already exist.
    async fn insert(&self, item: Item<K, V>);

    /// Merges the given item into the layer. `lower_bound` is the key to search for that should
    /// provide the first potential item to be merged with.
    async fn merge_into(&self, item: Item<K, V>, lower_bound: &K, merge_fn: merge::MergeFn<K, V>);

    /// Inserts or replaces an item.
    async fn replace_or_insert(&self, item: Item<K, V>);
}

/// Something that implements LayerIterator is returned by the get_iterator function.
#[async_trait]
pub trait LayerIterator<K, V>: Send {
    /// Searches for a key. Bound::Excluded is not supported. Bound::Unbounded positions the
    /// iterator on the first item in the layer.
    async fn seek(&mut self, bound: std::ops::Bound<&K>) -> Result<(), Error>;

    /// Advances the iterator.
    async fn advance(&mut self) -> Result<(), Error>;

    /// Returns the current item. This will be None if called when the iterator is first crated i.e.
    /// before either seek or advance has been called, and None if the iterator has reached the end
    /// of the layer.
    fn get(&self) -> Option<ItemRef<'_, K, V>>;
}

pub type BoxedLayerIterator<'iter, K, V> = Box<dyn LayerIterator<K, V> + 'iter>;

/// Mutable layers need an iterator that implements this in order to make merge_into work.
pub(super) trait LayerIteratorMut<K, V>: LayerIterator<K, V> {
    /// Casts to super-traits.
    fn as_iterator_mut(&mut self) -> &mut dyn LayerIterator<K, V>;
    fn as_iterator(&self) -> &dyn LayerIterator<K, V>;

    /// Erases the item that the iterator is currently pointing at. Afterwards, the iterator will
    /// be pointing at the item that follows.
    fn erase(&mut self);

    /// Inserts the given item immediately prior to the item the iterator is currently pointing at.
    fn insert_before(&mut self, item: Item<K, V>);
}
