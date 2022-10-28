// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod merge;
pub mod simple_persistent_layer;
pub mod skip_list_layer;
pub mod types;

use {
    crate::{
        log::*,
        object_handle::{ReadObjectHandle, WriteBytes, WriteObjectHandle, Writer},
        serialized_types::{Version, LATEST_VERSION},
        trace_duration,
    },
    anyhow::Error,
    async_utils::event::Event,
    futures::FutureExt,
    simple_persistent_layer::SimplePersistentLayerWriter,
    std::{
        fmt,
        future::Future,
        ops::Bound,
        sync::{Arc, RwLock},
    },
    types::{
        IntoLayerRefs, Item, ItemRef, Key, Layer, LayerIterator, LayerWriter, MergeableKey,
        MutableLayer, NextKey, OrdLowerBound, Value,
    },
};

const SKIP_LIST_LAYER_ITEMS: usize = 512;

// For serialization.
pub use simple_persistent_layer::LayerInfo;

pub async fn layers_from_handles<K: Key, V: Value>(
    handles: Box<[impl ReadObjectHandle + 'static]>,
) -> Result<Vec<Arc<dyn Layer<K, V>>>, Error> {
    let mut layers = Vec::new();
    for handle in Vec::from(handles) {
        layers.push(
            simple_persistent_layer::SimplePersistentLayer::open(handle).await?
                as Arc<dyn Layer<K, V>>,
        );
    }
    Ok(layers)
}

#[derive(Eq, PartialEq, Debug)]
pub enum Operation {
    Insert,
    ReplaceOrInsert,
    MergeInto,
}

pub type MutationCallback<K, V> = Option<Box<dyn Fn(Operation, &Item<K, V>) + Send + Sync>>;

struct Inner<K, V> {
    // The Event allows us to wait for any impending mutations to complete.  See the seal method
    // below.
    mutable_layer: (Event, Arc<dyn MutableLayer<K, V>>),
    layers: Vec<Arc<dyn Layer<K, V>>>,
    mutation_callback: MutationCallback<K, V>,
}

/// LSMTree manages a tree of layers to provide a key/value store.  Each layer contains deltas on
/// the preceding layer.  The top layer is an in-memory mutable layer.  Layers can be compacted to
/// form a new combined layer.
pub struct LSMTree<K, V> {
    data: RwLock<Inner<K, V>>,
    merge_fn: merge::MergeFn<K, V>,
}

impl<'tree, K: MergeableKey, V: Value> LSMTree<K, V> {
    /// Creates a new empty tree.
    pub fn new(merge_fn: merge::MergeFn<K, V>) -> Self {
        LSMTree {
            data: RwLock::new(Inner {
                mutable_layer: (
                    Event::new(),
                    skip_list_layer::SkipListLayer::new(SKIP_LIST_LAYER_ITEMS),
                ),
                layers: Vec::new(),
                mutation_callback: None,
            }),
            merge_fn,
        }
    }

    /// Opens an existing tree from the provided handles to the layer objects.
    pub async fn open(
        merge_fn: merge::MergeFn<K, V>,
        handles: Box<[impl ReadObjectHandle + 'static]>,
    ) -> Result<Self, Error> {
        Ok(LSMTree {
            data: RwLock::new(Inner {
                mutable_layer: (
                    Event::new(),
                    skip_list_layer::SkipListLayer::new(SKIP_LIST_LAYER_ITEMS),
                ),
                layers: layers_from_handles(handles).await?,
                mutation_callback: None,
            }),
            merge_fn,
        })
    }

    /// Replaces the immutable layers.
    pub fn set_layers(&self, layers: Vec<Arc<dyn Layer<K, V>>>) {
        self.data.write().unwrap().layers = layers;
    }

    /// Appends to the given layers at the end i.e. they should be base layers.  This is supposed
    /// to be used after replay when we are opening a tree and we have discovered the base layers.
    pub async fn append_layers(
        &self,
        handles: Box<[impl ReadObjectHandle + 'static]>,
    ) -> Result<(), Error> {
        let mut layers = layers_from_handles(handles).await?;
        self.data.write().unwrap().layers.append(&mut layers);
        Ok(())
    }

    /// Resets the immutable layers.
    pub fn reset_immutable_layers(&self) {
        self.data.write().unwrap().layers = Vec::new();
    }

    /// Seals the current mutable layer and creates a new one.  Returns a future
    /// that the caller should wait on to guarantee existing mutations have completed.
    pub fn seal(&self) -> impl Future<Output = ()> + '_ {
        let mut data = self.data.write().unwrap();
        let (event, layer) = std::mem::replace(
            &mut data.mutable_layer,
            (Event::new(), skip_list_layer::SkipListLayer::new(SKIP_LIST_LAYER_ITEMS)),
        );
        data.layers.insert(0, layer.as_layer());
        // The caller should wait for any mutations to the old mutable layer to complete and that's
        // done by waiting for the event to be dropped and ensuring that the event is cloned
        // whenever we plan to mutate the layer.
        event.wait_or_dropped().map(|r| {
            r.unwrap_err();
        })
    }

    /// Resets the tree to an empty state.
    pub fn reset(&self) {
        let mut data = self.data.write().unwrap();
        data.layers = Vec::new();
        data.mutable_layer =
            (Event::new(), skip_list_layer::SkipListLayer::new(SKIP_LIST_LAYER_ITEMS));
    }

    /// Writes the items yielded by the iterator into the supplied object.
    pub async fn compact_with_iterator<W: WriteBytes + Send>(
        &self,
        mut iterator: impl LayerIterator<K, V>,
        writer: W,
        block_size: u64,
    ) -> Result<(), Error> {
        trace_duration!("LSMTree::compact_with_iterator");
        let mut writer = SimplePersistentLayerWriter::<W, K, V>::new(writer, block_size).await?;
        while let Some(item_ref) = iterator.get() {
            debug!(?item_ref, "compact: writing");
            writer.write(item_ref).await?;
            iterator.advance().await?;
        }
        writer.flush().await
    }

    /// Compacts all the immutable layers.
    pub async fn compact(&self, object_handle: &impl WriteObjectHandle) -> Result<(), Error> {
        let layer_set = self.immutable_layer_set();
        let mut merger = layer_set.merger();
        let iter = merger.seek(Bound::Unbounded).await?;
        self.compact_with_iterator(iter, Writer::new(object_handle), object_handle.block_size())
            .await
    }

    /// Returns an empty layer-set for this tree.
    pub fn empty_layer_set(&self) -> LayerSet<K, V> {
        LayerSet { layers: Vec::new(), merge_fn: self.merge_fn }
    }

    /// Adds all the layers (including the mutable layer) to `layer_set`.
    pub fn add_all_layers_to_layer_set(&self, layer_set: &mut LayerSet<K, V>) {
        let data = self.data.read().unwrap();
        layer_set.layers.push(data.mutable_layer.1.clone().as_layer().into());
        for layer in &data.layers {
            layer_set.layers.push(layer.clone().into());
        }
    }

    /// Returns a clone of the current set of layers (including the mutable layer), after which one
    /// can get an iterator.
    pub fn layer_set(&self) -> LayerSet<K, V> {
        let mut layer_set = self.empty_layer_set();
        self.add_all_layers_to_layer_set(&mut layer_set);
        layer_set
    }

    /// Returns the current set of immutable layers after which one can get an iterator (for e.g.
    /// compacting).  Since these layers are immutable, getting an iterator should not block
    /// anything else.
    pub fn immutable_layer_set(&self) -> LayerSet<K, V> {
        let mut layers = Vec::new();
        {
            let data = self.data.read().unwrap();
            for layer in &data.layers {
                layers.push(layer.clone().into());
            }
        }
        LayerSet { layers, merge_fn: self.merge_fn }
    }

    /// Inserts an item into the mutable layer.
    /// Returns error if item already exists.
    pub async fn insert(&self, item: Item<K, V>) -> Result<(), Error> {
        let (_event, mutable_layer) = {
            let data = self.data.read().unwrap();
            if let Some(mutation_callback) = data.mutation_callback.as_ref() {
                mutation_callback(Operation::Insert, &item);
            }
            data.mutable_layer.clone()
        };
        mutable_layer.insert(item).await
    }

    /// Replaces or inserts an item into the mutable layer.
    pub async fn replace_or_insert(&self, item: Item<K, V>) {
        let (_event, mutable_layer) = {
            let data = self.data.read().unwrap();
            if let Some(mutation_callback) = data.mutation_callback.as_ref() {
                mutation_callback(Operation::ReplaceOrInsert, &item);
            }
            data.mutable_layer.clone()
        };
        mutable_layer.replace_or_insert(item).await;
    }

    /// Merges the given item into the mutable layer.
    pub async fn merge_into(&self, item: Item<K, V>, lower_bound: &K) {
        let (_event, mutable_layer) = {
            let data = self.data.read().unwrap();
            if let Some(mutation_callback) = data.mutation_callback.as_ref() {
                mutation_callback(Operation::MergeInto, &item);
            }
            data.mutable_layer.clone()
        };
        mutable_layer.merge_into(item, lower_bound, self.merge_fn).await
    }

    /// Searches for an exact match for the given key.
    pub async fn find(&self, search_key: &K) -> Result<Option<Item<K, V>>, Error>
    where
        K: Eq,
    {
        let layer_set = self.layer_set();
        let mut merger = layer_set.merger();
        let iter = merger.seek(Bound::Included(search_key)).await?;
        Ok(match iter.get() {
            Some(ItemRef { key, value, sequence }) if key == search_key => {
                Some(Item { key: key.clone(), value: value.clone(), sequence })
            }
            _ => None,
        })
    }

    pub fn mutable_layer(&self) -> Arc<dyn MutableLayer<K, V>> {
        self.data.read().unwrap().mutable_layer.1.clone()
    }

    /// Sets a mutation callback which is a callback that is triggered whenever any mutations are
    /// applied to the tree.  This might be useful for tests that want to record the precise
    /// sequence of mutations that are applied to the tree.
    pub fn set_mutation_callback(&self, mutation_callback: MutationCallback<K, V>) {
        self.data.write().unwrap().mutation_callback = mutation_callback;
    }

    /// Returns the earliest version used by a layer in the tree.
    pub fn get_earliest_version(&self) -> Version {
        let mut earliest_version = LATEST_VERSION;
        for layer in self.layer_set().layers {
            let layer_version = layer.get_version();
            if layer_version < earliest_version {
                earliest_version = layer_version;
            }
        }
        return earliest_version;
    }
}

/// This is an RAII wrapper for a layer which holds a lock on the layer (via the Layer::lock
/// method).
pub struct LockedLayer<K, V>(Event, Arc<dyn Layer<K, V>>);

impl<K, V> LockedLayer<K, V> {
    pub async fn close_layer(self) {
        let layer = self.1;
        std::mem::drop(self.0);
        layer.close().await;
    }
}

impl<K, V> From<Arc<dyn Layer<K, V>>> for LockedLayer<K, V> {
    fn from(layer: Arc<dyn Layer<K, V>>) -> Self {
        let event = layer.lock().unwrap();
        Self(event, layer)
    }
}

impl<K, V> std::ops::Deref for LockedLayer<K, V> {
    type Target = Arc<dyn Layer<K, V>>;

    fn deref(&self) -> &Self::Target {
        &self.1
    }
}

impl<K, V> AsRef<dyn Layer<K, V>> for LockedLayer<K, V> {
    fn as_ref(&self) -> &(dyn Layer<K, V> + 'static) {
        self.1.as_ref()
    }
}

/// A LayerSet provides a snapshot of the layers at a particular point in time, and allows you to
/// get an iterator.  Iterators borrow the layers so something needs to hold reference count.
pub struct LayerSet<K, V> {
    pub layers: Vec<LockedLayer<K, V>>,
    merge_fn: merge::MergeFn<K, V>,
}

impl<K: Key + NextKey + OrdLowerBound, V: Value> LayerSet<K, V> {
    pub fn merger(&self) -> merge::Merger<'_, K, V> {
        merge::Merger::new(&self.layers.as_slice().into_layer_refs(), self.merge_fn)
    }
}

impl<K, V> fmt::Debug for LayerSet<K, V> {
    fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt.debug_list()
            .entries(self.layers.iter().map(|l| {
                if let Some(handle) = l.handle() {
                    format!("{}", handle.object_id())
                } else {
                    format!("{:?}", Arc::as_ptr(l))
                }
            }))
            .finish()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::LSMTree,
        crate::{
            lsm_tree::{
                layers_from_handles,
                merge::{MergeLayerIterator, MergeResult},
                types::{
                    Item, ItemRef, LayerIterator, LayerIteratorFilter, NextKey, OrdLowerBound,
                    OrdUpperBound,
                },
            },
            serialized_types::{
                versioned_type, Version, Versioned, VersionedLatest, LATEST_VERSION,
            },
            testing::fake_object::{FakeObject, FakeObjectHandle},
        },
        fuchsia_async as fasync,
        rand::{seq::SliceRandom, thread_rng},
        std::{ops::Bound, sync::Arc},
    };

    #[derive(Clone, Eq, PartialEq, Debug, serde::Serialize, serde::Deserialize, Versioned)]
    struct TestKey(std::ops::Range<u64>);

    versioned_type! { 1.. => TestKey }

    impl NextKey for TestKey {}

    impl OrdUpperBound for TestKey {
        fn cmp_upper_bound(&self, other: &TestKey) -> std::cmp::Ordering {
            self.0.end.cmp(&other.0.end)
        }
    }

    impl OrdLowerBound for TestKey {
        fn cmp_lower_bound(&self, other: &Self) -> std::cmp::Ordering {
            self.0.start.cmp(&other.0.start)
        }
    }

    fn emit_left_merge_fn(
        _left: &MergeLayerIterator<'_, TestKey, u64>,
        _right: &MergeLayerIterator<'_, TestKey, u64>,
    ) -> MergeResult<TestKey, u64> {
        MergeResult::EmitLeft
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_iteration() {
        let tree = LSMTree::new(emit_left_merge_fn);
        let items = [Item::new(TestKey(1..1), 1), Item::new(TestKey(2..2), 2)];
        tree.insert(items[0].clone()).await.expect("insert error");
        tree.insert(items[1].clone()).await.expect("insert error");
        let layers = tree.layer_set();
        let mut merger = layers.merger();
        let mut iter = merger.seek(Bound::Unbounded).await.expect("seek failed");
        let ItemRef { key, value, .. } = iter.get().expect("missing item");
        assert_eq!((key, value), (&items[0].key, &items[0].value));
        iter.advance().await.expect("advance failed");
        let ItemRef { key, value, .. } = iter.get().expect("missing item");
        assert_eq!((key, value), (&items[1].key, &items[1].value));
        iter.advance().await.expect("advance failed");
        assert!(iter.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_compact() {
        let tree = LSMTree::new(emit_left_merge_fn);
        let items = [
            Item::new(TestKey(1..1), 1),
            Item::new(TestKey(2..2), 2),
            Item::new(TestKey(3..3), 3),
            Item::new(TestKey(4..4), 4),
        ];
        tree.insert(items[0].clone()).await.expect("insert error");
        tree.insert(items[1].clone()).await.expect("insert error");
        tree.seal().await;
        tree.insert(items[2].clone()).await.expect("insert error");
        tree.insert(items[3].clone()).await.expect("insert error");
        tree.seal().await;
        let object = Arc::new(FakeObject::new());
        let handle = FakeObjectHandle::new(object.clone());
        tree.compact(&handle).await.expect("compact failed");
        tree.set_layers(
            layers_from_handles(Box::new([handle])).await.expect("layers_from_handles failed"),
        );
        let handle = FakeObjectHandle::new(object.clone());
        let tree = LSMTree::open(emit_left_merge_fn, [handle].into()).await.expect("open failed");

        let layers = tree.layer_set();
        let mut merger = layers.merger();
        let mut iter = merger.seek(Bound::Unbounded).await.expect("seek failed");
        for i in 1..5 {
            let ItemRef { key, value, .. } = iter.get().expect("missing item");
            assert_eq!((key, value), (&TestKey(i..i), &i));
            iter.advance().await.expect("advance failed");
        }
        assert!(iter.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_find() {
        let items = [
            Item::new(TestKey(1..1), 1),
            Item::new(TestKey(2..2), 2),
            Item::new(TestKey(3..3), 3),
            Item::new(TestKey(4..4), 4),
        ];
        let tree = LSMTree::new(emit_left_merge_fn);
        tree.insert(items[0].clone()).await.expect("insert error");
        tree.insert(items[1].clone()).await.expect("insert error");
        tree.seal().await;
        tree.insert(items[2].clone()).await.expect("insert error");
        tree.insert(items[3].clone()).await.expect("insert error");

        let item = tree.find(&items[1].key).await.expect("find failed").expect("not found");
        assert_eq!(item, items[1]);
        assert!(tree.find(&TestKey(100..100)).await.expect("find failed").is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_empty_seal() {
        let tree = LSMTree::new(emit_left_merge_fn);
        tree.seal().await;
        let item = Item::new(TestKey(1..1), 1);
        tree.insert(item.clone()).await.expect("insert error");
        let object = Arc::new(FakeObject::new());
        let handle = FakeObjectHandle::new(object.clone());
        tree.compact(&handle).await.expect("compact failed");
        tree.set_layers(
            layers_from_handles(Box::new([handle])).await.expect("layers_from_handles failed"),
        );
        let found_item = tree.find(&item.key).await.expect("find failed").expect("not found");
        assert_eq!(found_item, item);
        assert!(tree.find(&TestKey(2..2)).await.expect("find failed").is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_filter() {
        let items = [
            Item::new(TestKey(1..1), 1),
            Item::new(TestKey(2..2), 2),
            Item::new(TestKey(3..3), 3),
            Item::new(TestKey(4..4), 4),
        ];
        let tree = LSMTree::new(emit_left_merge_fn);
        tree.insert(items[0].clone()).await.expect("insert error");
        tree.insert(items[1].clone()).await.expect("insert error");
        tree.insert(items[2].clone()).await.expect("insert error");
        tree.insert(items[3].clone()).await.expect("insert error");

        let layers = tree.layer_set();
        let mut merger = layers.merger();

        // Filter out odd keys (which also guarantees we skip the first key which is an edge case).
        let mut iter = (Box::new(merger.seek(Bound::Unbounded).await.expect("seek failed"))
            as Box<dyn LayerIterator<_, _>>)
            .filter(|item: ItemRef<'_, TestKey, u64>| item.key.0.start % 2 == 0)
            .await
            .expect("filter failed");

        assert_eq!(iter.get(), Some(items[1].as_item_ref()));
        iter.advance().await.expect("advance failed");
        assert_eq!(iter.get(), Some(items[3].as_item_ref()));
        iter.advance().await.expect("advance failed");
        assert!(iter.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_insert_order_agnostic() {
        let items = [
            Item::new(TestKey(1..1), 1),
            Item::new(TestKey(2..2), 2),
            Item::new(TestKey(3..3), 3),
            Item::new(TestKey(4..4), 4),
            Item::new(TestKey(5..5), 5),
            Item::new(TestKey(6..6), 6),
        ];
        let a = LSMTree::new(emit_left_merge_fn);
        for item in &items {
            a.insert(item.clone()).await.expect("insert error");
        }
        let b = LSMTree::new(emit_left_merge_fn);
        let mut shuffled = items.clone();
        shuffled.shuffle(&mut thread_rng());
        for item in &shuffled {
            b.insert(item.clone()).await.expect("insert error");
        }
        let layers = a.layer_set();
        let mut merger = layers.merger();
        let mut iter_a = merger.seek(Bound::Unbounded).await.expect("seek failed");
        let layers = b.layer_set();
        let mut merger = layers.merger();
        let mut iter_b = merger.seek(Bound::Unbounded).await.expect("seek failed");

        for item in items {
            assert_eq!(Some(item.as_item_ref()), iter_a.get());
            assert_eq!(Some(item.as_item_ref()), iter_b.get());
            iter_a.advance().await.expect("advance failed");
            iter_b.advance().await.expect("advance failed");
        }
        assert!(iter_a.get().is_none());
        assert!(iter_b.get().is_none());
    }
}

#[cfg(fuzz)]
mod fuzz {
    use {
        crate::{
            lsm_tree::types::{Item, NextKey, OrdLowerBound, OrdUpperBound},
            serialized_types::{
                versioned_type, Version, Versioned, VersionedLatest, LATEST_VERSION,
            },
        },
        arbitrary::Arbitrary,
        fuzz::fuzz,
    };

    #[derive(
        Arbitrary, Clone, Eq, PartialEq, Debug, serde::Serialize, serde::Deserialize, Versioned,
    )]
    struct TestKey(std::ops::Range<u64>);

    versioned_type! { 1.. => TestKey }

    impl Versioned for u64 {}
    versioned_type! { 1.. => u64 }

    impl NextKey for TestKey {}

    impl OrdUpperBound for TestKey {
        fn cmp_upper_bound(&self, other: &TestKey) -> std::cmp::Ordering {
            self.0.end.cmp(&other.0.end)
        }
    }

    impl OrdLowerBound for TestKey {
        fn cmp_lower_bound(&self, other: &Self) -> std::cmp::Ordering {
            self.0.start.cmp(&other.0.start)
        }
    }

    #[derive(Arbitrary)]
    enum FuzzAction {
        Insert(Item<TestKey, u64>),
        ReplaceOrInsert(Item<TestKey, u64>),
        MergeInto(Item<TestKey, u64>, TestKey),
        Find(TestKey),
        Seal,
    }

    #[fuzz]
    fn fuzz_lsm_tree_actions(actions: Vec<FuzzAction>) {
        use {
            super::LSMTree,
            crate::lsm_tree::merge::{MergeLayerIterator, MergeResult},
            futures::executor::block_on,
        };

        fn emit_left_merge_fn(
            _left: &MergeLayerIterator<'_, TestKey, u64>,
            _right: &MergeLayerIterator<'_, TestKey, u64>,
        ) -> MergeResult<TestKey, u64> {
            MergeResult::EmitLeft
        }

        let tree = LSMTree::new(emit_left_merge_fn);
        for action in actions {
            match action {
                FuzzAction::Insert(item) => {
                    let _ = block_on(tree.insert(item));
                }
                FuzzAction::ReplaceOrInsert(item) => {
                    block_on(tree.replace_or_insert(item));
                }
                FuzzAction::Find(key) => {
                    block_on(tree.find(&key)).expect("find failed");
                }
                FuzzAction::MergeInto(item, bound) => block_on(tree.merge_into(item, &bound)),
                FuzzAction::Seal => block_on(tree.seal()),
            };
        }
    }
}
