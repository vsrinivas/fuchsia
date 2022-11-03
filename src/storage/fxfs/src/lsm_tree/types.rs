// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        lsm_tree::merge,
        object_handle::ReadObjectHandle,
        serialized_types::{Version, Versioned, VersionedLatest},
    },
    anyhow::Error,
    async_trait::async_trait,
    async_utils::event::Event,
    serde::{Deserialize, Serialize},
    std::{fmt::Debug, sync::Arc},
    type_hash::TypeHash,
};

/// Keys and values need to implement the following traits.  For merging, they need to implement
/// MergeableKey.  TODO: Use trait_alias when available.
pub trait Key:
    Clone
    + Debug
    + OrdUpperBound
    + Send
    + Sync
    + Versioned
    + VersionedLatest
    + std::marker::Unpin
    + 'static
{
}

pub trait RangeKey: Key {
    /// Returns if two keys overlap.
    fn overlaps(&self, other: &Self) -> bool;
}

impl<K> Key for K where
    K: Clone
        + Debug
        + OrdUpperBound
        + Send
        + Sync
        + Versioned
        + VersionedLatest
        + std::marker::Unpin
        + 'static
{
}

pub trait MergeableKey: Key + Eq + NextKey + OrdLowerBound {}
impl<K> MergeableKey for K where K: Key + Eq + NextKey + OrdLowerBound {}

pub trait Value:
    Clone + Send + Sync + Versioned + VersionedLatest + Debug + std::marker::Unpin + 'static
{
}
impl<V> Value for V where
    V: Clone + Send + Sync + Versioned + VersionedLatest + Debug + std::marker::Unpin + 'static
{
}

/// ItemRef is a struct that contains references to key and value, which is useful since in many
/// cases since keys and values are stored separately so &Item is not possible.
#[derive(Debug, PartialEq, Eq, Serialize)]
pub struct ItemRef<'a, K, V> {
    pub key: &'a K,
    pub value: &'a V,
    pub sequence: u64,
}

impl<K: Clone, V: Clone> ItemRef<'_, K, V> {
    pub fn cloned(&self) -> Item<K, V> {
        Item { key: self.key.clone(), value: self.value.clone(), sequence: self.sequence }
    }
}

impl<'a, K, V> Clone for ItemRef<'a, K, V> {
    fn clone(&self) -> Self {
        ItemRef { key: self.key, value: self.value, sequence: self.sequence }
    }
}
impl<'a, K, V> Copy for ItemRef<'a, K, V> {}

/// Item is a struct that combines a key and a value.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Serialize, Deserialize, TypeHash)]
#[cfg_attr(fuzz, derive(arbitrary::Arbitrary))]
pub struct Item<K, V> {
    pub key: K,
    pub value: V,
    /// |sequence| is a monotonically increasing sequence number for the Item, which is set when the
    /// Item is inserted into the tree.  In practice, this is the journal file offset at the time of
    /// committing the transaction containing the Item.  Note that two or more Items may share the
    /// same |sequence|.
    pub sequence: u64,
}

impl<K, V> Item<K, V> {
    pub fn new(key: K, value: V) -> Item<K, V> {
        Item { key, value, sequence: 0u64 }
    }

    pub fn new_with_sequence(key: K, value: V, sequence: u64) -> Item<K, V> {
        Item { key, value, sequence }
    }

    pub fn as_item_ref(&self) -> ItemRef<'_, K, V> {
        self.into()
    }
}

impl<'a, K, V> From<&'a Item<K, V>> for ItemRef<'a, K, V> {
    fn from(item: &'a Item<K, V>) -> ItemRef<'a, K, V> {
        ItemRef { key: &item.key, value: &item.value, sequence: item.sequence }
    }
}

/// The find functions will return items with keys that are greater-than or equal to the search key,
/// so for keys that are like extents, the keys should sort (via OrdUpperBound) using the end
/// of their ranges, and you should set the search key accordingly.
///
/// For example, let's say the tree holds extents 100..200, 200..250 and you want to perform a read
/// for range 150..250, you should search for 0..151 which will first return the extent 100..200
/// (and then the iterator can be advanced to 200..250 after). When merging, keys can overlap, so
/// consider the case where we want to merge an extent with range 100..300 with an existing extent
/// of 200..250. In that case, we want to treat the extent with range 100..300 as lower than the key
/// 200..250 because we'll likely want to split the extents (e.g. perhaps we want 100..200,
/// 200..250, 250..300), so for merging, we need to use a different comparison function and we deal
/// with that using the OrdLowerBound trait.
///
/// If your keys don't have overlapping ranges that need to be merged, then these can be the same as
/// std::cmp::Ord (use the DefaultOrdUpperBound and DefaultOrdLowerBound traits).

pub trait OrdUpperBound {
    fn cmp_upper_bound(&self, other: &Self) -> std::cmp::Ordering;
}

pub trait DefaultOrdUpperBound: OrdUpperBound + Ord {}

impl<T: DefaultOrdUpperBound> OrdUpperBound for T {
    fn cmp_upper_bound(&self, other: &Self) -> std::cmp::Ordering {
        // Default to using cmp.
        self.cmp(other)
    }
}

pub trait OrdLowerBound {
    fn cmp_lower_bound(&self, other: &Self) -> std::cmp::Ordering;
}

pub trait DefaultOrdLowerBound: OrdLowerBound + Ord {}

impl<T: DefaultOrdLowerBound> OrdLowerBound for T {
    fn cmp_lower_bound(&self, other: &Self) -> std::cmp::Ordering {
        // Default to using cmp.
        self.cmp(other)
    }
}

/// The NextKey trait allows for an optimisation which allows the merger to avoid querying a layer
/// if it knows it has found the next possible key.  Consider the following example showing two
/// layers with range based keys.
///
///      +----------+------------+
///  0   |  0..100  |  100..200  |
///      +----------+------------+
///  1              |  100..200  |
///                 +------------+
///
/// If you search and find the 0..100 key, then only layer 0 will be touched.  If you then want to
/// advance to the 100..200 record, you can find it in layer 0 but unless you know that it
/// immediately follows the 0..100 key i.e. that there is no possible key, K, such that 0..100 < K <
/// 100..200, the merger has to consult all other layers to check.  next_key should return a key, N,
/// such that if the merger encounters a key that is <= N (using OrdLowerBound), it can stop
/// touching more layers.  The key N should also be the the key to search for in other layers if the
/// merger needs to do so.  In the example above, it should be a key that is > 0..100 and 99..100,
/// but <= 100..200 (using OrdUpperBound).  In practice, what this means is that for range based
/// keys, OrdUpperBound should use the end of the range, OrdLowerBound should use the start of the
/// range, and next_key should return end..end + 1.  This is purely an optimisation; the default
/// will be correct but not performant.
pub trait NextKey: Clone {
    fn next_key(&self) -> Option<Self> {
        None
    }
}

/// Layer is a trait that all layers need to implement (mutable and immutable).
#[async_trait]
pub trait Layer<K, V>: Send + Sync {
    fn handle(&self) -> Option<&dyn ReadObjectHandle> {
        None
    }

    /// Searches for a key. Bound::Excluded is not supported. Bound::Unbounded positions the
    /// iterator on the first item in the layer.
    async fn seek(&self, bound: std::ops::Bound<&K>)
        -> Result<BoxedLayerIterator<'_, K, V>, Error>;

    /// Locks the layer preventing it from being closed. This will never block i.e. there can be
    /// many locks concurrently.  The lock is purely advisory: seek will still work even if lock has
    /// not been called; it merely causes close to wait until all locks are released.  Returns None
    /// if close has been called for the layer.
    fn lock(&self) -> Option<Event>;

    /// Waits for existing locks readers to finish and then returns.  Subsequent calls to lock will
    /// return None.
    async fn close(&self);

    /// Returns the version number used by structs in this layer
    fn get_version(&self) -> Version;
}

/// MutableLayer is a trait that only mutable layers need to implement.
#[async_trait]
pub trait MutableLayer<K, V>: Layer<K, V> {
    fn as_layer(self: Arc<Self>) -> Arc<dyn Layer<K, V>>;

    /// Merges the given item into the layer. `lower_bound` is the key to search for that should
    /// provide the first potential item to be merged with.
    async fn merge_into(&self, item: Item<K, V>, lower_bound: &K, merge_fn: merge::MergeFn<K, V>);

    /// Inserts the given item into the layer.
    /// Returns an error if item already exist.
    async fn insert(&self, item: Item<K, V>) -> Result<(), Error>;

    /// Inserts or replaces an item.
    async fn replace_or_insert(&self, item: Item<K, V>);

    /// Returns the number of items in the layer.
    fn len(&self) -> usize;
}

/// Something that implements LayerIterator is returned by the seek function.
#[async_trait]
pub trait LayerIterator<K, V>: Send + Sync {
    /// Advances the iterator.
    async fn advance(&mut self) -> Result<(), Error>;

    /// Returns the current item. This will be None if called when the iterator is first crated i.e.
    /// before either seek or advance has been called, and None if the iterator has reached the end
    /// of the layer.
    fn get(&self) -> Option<ItemRef<'_, K, V>>;
}

pub type BoxedLayerIterator<'iter, K, V> = Box<dyn LayerIterator<K, V> + 'iter>;

#[async_trait]
pub trait LayerIteratorFilter<'a, K, V> {
    /// Mirrors std::iter::Iterator::filter.
    async fn filter<P: for<'b> Fn(ItemRef<'b, K, V>) -> bool + Send + Sync>(
        self,
        predicate: P,
    ) -> Result<Filter<'a, K, V, P>, Error>;
}

#[async_trait]
impl<'a, K, V> LayerIteratorFilter<'a, K, V> for BoxedLayerIterator<'a, K, V> {
    async fn filter<P: for<'b> Fn(ItemRef<'b, K, V>) -> bool + Send + Sync>(
        self,
        predicate: P,
    ) -> Result<Filter<'a, K, V, P>, Error> {
        Filter::new(self, predicate).await
    }
}

#[async_trait]
impl<'iter, K, V> LayerIterator<K, V> for BoxedLayerIterator<'iter, K, V> {
    async fn advance(&mut self) -> Result<(), Error> {
        (**self).advance().await
    }
    fn get(&self) -> Option<ItemRef<'_, K, V>> {
        (**self).get()
    }
}

/// Mutable layers need an iterator that implements this in order to make merge_into work.
#[async_trait]
pub(super) trait LayerIteratorMut<K, V>: LayerIterator<K, V> {
    /// Casts to super-traits.
    fn as_iterator_mut(&mut self) -> &mut dyn LayerIterator<K, V>;
    fn as_iterator(&self) -> &dyn LayerIterator<K, V>;

    /// Erases the item that the iterator is currently pointing at. Afterwards, the iterator will
    /// be pointing at the item that follows.
    fn erase(&mut self);

    /// Inserts the given item immediately prior to the item the iterator is currently pointing at.
    fn insert(&mut self, item: Item<K, V>);

    /// Commits changes and waits for any existing readers to finish.
    async fn commit_and_wait(&mut self);
}

/// Trait for writing new layers.
#[async_trait]
pub trait LayerWriter<K, V>
where
    K: Debug + Send + Versioned + Sync,
    V: Debug + Send + Versioned + Sync,
{
    /// Writes the given item to this layer.
    async fn write(&mut self, item: ItemRef<'_, K, V>) -> Result<(), Error>;

    /// Flushes any buffered items to the backing storage.
    async fn flush(&mut self) -> Result<(), Error>;
}

/// A helper trait that converts arrays of layers into arrays of references to layers.
pub trait IntoLayerRefs<'a, K, V, T: AsRef<U> + 'a, U: ?Sized>
where
    Self: IntoIterator<Item = &'a T>,
{
    fn into_layer_refs(self) -> Box<[&'a dyn Layer<K, V>]>;
}

// Generic implementation where we need the cast to &dyn Layer.
impl<'a, K, V, T: AsRef<U>, U: Layer<K, V> + 'a> IntoLayerRefs<'a, K, V, T, U> for &'a [T] {
    fn into_layer_refs(self) -> Box<[&'a dyn Layer<K, V>]> {
        let refs: Vec<_> = self.iter().map(|x| x.as_ref() as &dyn Layer<K, V>).collect();
        refs.into_boxed_slice()
    }
}

// Generic implementation where we already have &dyn Layer.
impl<'a, K, V, T: AsRef<dyn Layer<K, V>>> IntoLayerRefs<'a, K, V, T, dyn Layer<K, V>>
    for &'a [T]
{
    fn into_layer_refs(self) -> Box<[&'a dyn Layer<K, V>]> {
        let refs: Vec<_> = self.iter().map(|x| x.as_ref()).collect();
        refs.into_boxed_slice()
    }
}

pub struct Filter<'a, K, V, P> {
    iter: BoxedLayerIterator<'a, K, V>,
    predicate: P,
}

impl<'a, K, V, P: for<'b> Fn(ItemRef<'b, K, V>) -> bool + Send + Sync> Filter<'a, K, V, P> {
    async fn new(
        iter: BoxedLayerIterator<'a, K, V>,
        predicate: P,
    ) -> Result<Filter<'a, K, V, P>, Error> {
        let mut filter = Filter { iter, predicate };
        filter.skip_filtered().await?;
        Ok(filter)
    }

    async fn skip_filtered(&mut self) -> Result<(), Error> {
        loop {
            match self.iter.get() {
                Some(item) if !(self.predicate)(item) => {}
                _ => return Ok(()),
            }
            self.iter.advance().await?;
        }
    }
}

#[async_trait]
impl<K: Send + Sync, V: Send + Sync, P: for<'b> Fn(ItemRef<'b, K, V>) -> bool + Send + Sync>
    LayerIterator<K, V> for Filter<'_, K, V, P>
{
    async fn advance(&mut self) -> Result<(), Error> {
        self.iter.advance().await?;
        self.skip_filtered().await
    }

    fn get(&self) -> Option<ItemRef<'_, K, V>> {
        self.iter.get()
    }
}
