// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod merge;
mod simple_persistent_layer;
pub mod skip_list_layer;
pub mod types;

use {
    crate::{
        object_handle::{ReadObjectHandle, WriteBytes, WriteObjectHandle, Writer},
        trace_duration,
    },
    anyhow::Error,
    async_utils::event::Event,
    simple_persistent_layer::SimplePersistentLayerWriter,
    std::{
        fmt,
        ops::Bound,
        sync::{Arc, RwLock},
    },
    types::{
        IntoLayerRefs, Item, ItemRef, Key, Layer, LayerIterator, LayerWriter, MergeableKey,
        MutableLayer, NextKey, OrdLowerBound, Value,
    },
};

const SKIP_LIST_LAYER_ITEMS: usize = 512;

pub async fn layers_from_handles<K: Key, V: Value>(
    handles: Box<[impl ReadObjectHandle + 'static]>,
) -> Result<Vec<Arc<dyn Layer<K, V>>>, Error> {
    let mut layers = Vec::new();
    for handle in Vec::from(handles) {
        let block_size = handle.block_size();
        layers.push(
            simple_persistent_layer::SimplePersistentLayer::open(handle, block_size).await?
                as Arc<dyn Layer<K, V>>,
        );
    }
    Ok(layers)
}

struct Inner<K, V> {
    // The Event allows us to wait for any impending mutations to complete.  See the seal method
    // below.
    mutable_layer: (Event, Arc<dyn MutableLayer<K, V>>),
    layers: Vec<Arc<dyn Layer<K, V>>>,
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

    // TODO(csuter): We need to handle the case where the mutable layer is empty.
    /// Seals the current mutable layer and creates a new one.
    pub async fn seal(&self) {
        {
            let mut data = self.data.write().unwrap();
            let (event, layer) = std::mem::replace(
                &mut data.mutable_layer,
                (Event::new(), skip_list_layer::SkipListLayer::new(SKIP_LIST_LAYER_ITEMS)),
            );
            data.layers.insert(0, layer.as_layer());
            // Before we return, we must wait for any mutations to the old mutable layer to complete
            // and that's done by waiting for the event to be dropped and ensuring that the event is
            // cloned whenever we plan to mutate the layer.
            event.wait_or_dropped()
        }
        .await
        .unwrap_err(); // wait_or_dropped returns Result<(), Dropped>
    }

    pub fn new_writer<'a>(object_handle: &'a dyn WriteObjectHandle) -> impl LayerWriter + 'a {
        SimplePersistentLayerWriter::new(Writer::new(object_handle), object_handle.block_size())
    }

    // TODO(csuter): We should provide a way for the caller to skip compactions if there's nothing
    // to compact.
    /// Writes the items yielded by the iterator into the supplied object.
    pub async fn compact_with_iterator(
        &self,
        mut iterator: impl LayerIterator<K, V>,
        writer: impl WriteBytes + Send,
        block_size: u32,
    ) -> Result<(), Error> {
        trace_duration!("LSMTree::compact_with_iterator");
        let mut writer = SimplePersistentLayerWriter::new(writer, block_size);
        while let Some(item_ref) = iterator.get() {
            log::debug!("compact: writing {:?}", item_ref);
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

    /// Inserts an item into the mutable layer. Behaviour is undefined if the item already exists.
    pub async fn insert(&self, item: Item<K, V>) {
        let (_event, mutable_layer) = self.data.read().unwrap().mutable_layer.clone();
        mutable_layer.insert(item).await;
    }

    /// Replaces or inserts an item into the mutable layer.
    pub async fn replace_or_insert(&self, item: Item<K, V>) {
        let (_event, mutable_layer) = self.data.read().unwrap().mutable_layer.clone();
        mutable_layer.replace_or_insert(item).await;
    }

    /// Merges the given item into the mutable layer.
    pub async fn merge_into(&self, item: Item<K, V>, lower_bound: &K) {
        let (_event, mutable_layer) = self.data.read().unwrap().mutable_layer.clone();
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
                types::{Item, ItemRef, LayerIterator, NextKey, OrdLowerBound, OrdUpperBound},
            },
            testing::fake_object::{FakeObject, FakeObjectHandle},
        },
        fuchsia_async as fasync,
        std::{ops::Bound, sync::Arc},
    };

    #[derive(Clone, Eq, PartialEq, Debug, serde::Serialize, serde::Deserialize)]
    struct TestKey(std::ops::Range<u64>);

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
        tree.insert(items[0].clone()).await;
        tree.insert(items[1].clone()).await;
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
        tree.insert(items[0].clone()).await;
        tree.insert(items[1].clone()).await;
        tree.seal().await;
        tree.insert(items[2].clone()).await;
        tree.insert(items[3].clone()).await;
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
        tree.insert(items[0].clone()).await;
        tree.insert(items[1].clone()).await;
        tree.seal().await;
        tree.insert(items[2].clone()).await;
        tree.insert(items[3].clone()).await;

        let item = tree.find(&items[1].key).await.expect("find failed").expect("not found");
        assert_eq!(item, items[1]);
        assert!(tree.find(&TestKey(100..100)).await.expect("find failed").is_none());
    }
}
