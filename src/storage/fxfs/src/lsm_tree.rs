// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod merge;
mod simple_persistent_layer;
pub mod skip_list_layer;
pub mod types;

use {
    crate::object_handle::ObjectHandle,
    anyhow::Error,
    async_trait::async_trait,
    simple_persistent_layer::SimplePersistentLayerWriter,
    std::{
        ops::Bound,
        sync::{Arc, RwLock},
    },
    types::{
        IntoLayerRefs, Item, ItemRef, Key, Layer, LayerIterator, LayerWriter, MutableLayer,
        OrdLowerBound, Value,
    },
};

const SKIP_LIST_LAYER_ITEMS: usize = 512;
const SIMPLE_PERSISTENT_LAYER_BLOCK_SIZE: u32 = 512;

struct Inner<K, V> {
    mutable_layer: Arc<dyn MutableLayer<K, V>>,
    layers: Vec<Arc<dyn Layer<K, V>>>,
}

/// LSMTree manages a tree of layers to provide a key/value store.  Each layer contains deltas on
/// the preceding layer.  The top layer is an in-memory mutable layer.  Layers can be compacted to
/// form a new combined layer.
pub struct LSMTree<K, V> {
    data: RwLock<Inner<K, V>>,
    merge_fn: merge::MergeFn<K, V>,
}

impl<'tree, K: Key + OrdLowerBound, V: Value> LSMTree<K, V> {
    /// Creates a new empty tree.
    pub fn new(merge_fn: merge::MergeFn<K, V>) -> Self {
        LSMTree {
            data: RwLock::new(Inner {
                mutable_layer: skip_list_layer::SkipListLayer::new(SKIP_LIST_LAYER_ITEMS),
                layers: Vec::new(),
            }),
            merge_fn,
        }
    }

    /// Opens an existing tree from the provided handles to the layer objects.
    pub fn open(
        merge_fn: merge::MergeFn<K, V>,
        handles: Box<[impl ObjectHandle + 'static]>,
    ) -> Self {
        LSMTree {
            data: RwLock::new(Inner {
                mutable_layer: skip_list_layer::SkipListLayer::new(SKIP_LIST_LAYER_ITEMS),
                layers: Self::layers_from_handles(handles),
            }),
            merge_fn,
        }
    }

    /// Replaces the immutable layers.
    pub fn set_layers(&self, handles: Box<[impl ObjectHandle + 'static]>) {
        let mut data = self.data.write().unwrap();
        data.layers = Self::layers_from_handles(handles);
    }

    /// Resets the immutable layers.
    pub fn reset_immutable_layers(&self) {
        self.data.write().unwrap().layers = Vec::new();
    }

    // TODO(csuter): We need to handle the case where the mutable layer is empty.
    /// Seals the current mutable layer and creates a new one.
    pub fn seal(&self) {
        let mut data = self.data.write().unwrap();
        let layer = data.mutable_layer.clone().as_layer();
        data.layers.insert(0, layer);
        data.mutable_layer = skip_list_layer::SkipListLayer::new(SKIP_LIST_LAYER_ITEMS);
    }

    pub fn new_writer(object_handle: &dyn ObjectHandle) -> impl LayerWriter + '_ {
        SimplePersistentLayerWriter::new(object_handle, SIMPLE_PERSISTENT_LAYER_BLOCK_SIZE)
    }

    // TODO(csuter): This should run as a different task.
    // TODO(csuter): We should provide a way for the caller to skip compactions if there's nothing
    // to compact.
    /// Writes the items yielded by the iterator into the supplied object and then switches the tree
    /// to use the new layer.
    pub async fn compact_with_iterator(
        &self,
        mut iterator: impl LayerIterator<K, V>,
        mut object_handle: impl ObjectHandle + 'static,
    ) -> Result<(), Error> {
        {
            let mut writer = SimplePersistentLayerWriter::new(
                &mut object_handle,
                SIMPLE_PERSISTENT_LAYER_BLOCK_SIZE,
            );
            while let Some(item_ref) = iterator.get() {
                log::debug!("compact: writing {:?}", item_ref);
                writer.write(item_ref).await?;
                iterator.advance().await?;
            }
            writer.flush().await?;
        }

        self.set_layers(Box::new([object_handle]));
        Ok(())
    }

    /// Compacts all the immutable layers.
    pub async fn compact(&self, object_handle: impl ObjectHandle + 'static) -> Result<(), Error> {
        let layer_set = self.immutable_layer_set();
        let iter = layer_set.seek(Bound::Unbounded).await?;
        self.compact_with_iterator(iter, object_handle).await
    }

    /// Returns a clone of the current set of layers (including the mutable layer), after which one
    /// can get an iterator, although care should be taken because writes will blocked to the
    /// mutable layer until the iterator is dropped.
    pub fn layer_set(&self) -> LayerSet<K, V> {
        let mut layers = Vec::new();
        {
            let data = self.data.read().unwrap();
            layers.push(data.mutable_layer.clone().as_layer().into());
            for layer in &data.layers {
                layers.push(layer.clone().into());
            }
        }
        LayerSet { layers, merge_fn: self.merge_fn }
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
        let mutable_layer = self.data.read().unwrap().mutable_layer.clone();
        mutable_layer.insert(item).await;
    }

    /// Replaces or inserts an item into the mutable layer.
    pub async fn replace_or_insert(&self, item: Item<K, V>) {
        let mutable_layer = self.data.read().unwrap().mutable_layer.clone();
        mutable_layer.replace_or_insert(item).await;
    }

    /// Merges the given item into the mutable layer.
    pub async fn merge_into(&self, item: Item<K, V>, lower_bound: &K) {
        let mutable_layer = self.data.read().unwrap().mutable_layer.clone();
        mutable_layer.merge_into(item, lower_bound, self.merge_fn).await
    }

    /// Searches for an exact match for the given key.
    pub async fn find(&self, search_key: &K) -> Result<Option<Item<K, V>>, Error>
    where
        K: Clone,
        V: Clone,
    {
        let layer_set = self.layer_set();
        let iter = layer_set.seek(Bound::Included(search_key)).await?;
        Ok(match iter.get() {
            Some(ItemRef { key, value }) if key == search_key => {
                Some(Item { key: key.clone(), value: value.clone() })
            }
            _ => None,
        })
    }

    fn layers_from_handles(
        handles: Box<[impl ObjectHandle + 'static]>,
    ) -> Vec<Arc<dyn Layer<K, V>>> {
        handles
            .into_vec()
            .drain(..)
            .map(|h| {
                simple_persistent_layer::SimplePersistentLayer::new(
                    h,
                    SIMPLE_PERSISTENT_LAYER_BLOCK_SIZE,
                ) as Arc<dyn Layer<K, V>>
            })
            .collect()
    }
}

/// A LayerSet provides a snapshot of the layers at a particular point in time, and allows you to
/// get an iterator.  Iterators borrow the layers so something needs to hold reference count.
pub struct LayerSet<K, V> {
    layers: Vec<Arc<dyn Layer<K, V>>>,
    merge_fn: merge::MergeFn<K, V>,
}

impl<
        K: std::fmt::Debug + OrdLowerBound + Unpin + 'static,
        V: std::fmt::Debug + Unpin + 'static,
    > LayerSet<K, V>
{
    pub fn add_layer(&mut self, layer: Arc<dyn Layer<K, V>>) {
        self.layers.push(layer);
    }

    pub async fn seek<'a>(&'a self, bound: Bound<&K>) -> Result<LSMTreeIter<'a, K, V>, Error> {
        let mut iter = LSMTreeIter(merge::Merger::new(
            &self.layers.as_slice().into_layer_refs(),
            self.merge_fn,
        ));
        iter.0.seek(bound).await?;
        Ok(iter)
    }
}

pub struct LSMTreeIter<'a, K, V>(merge::Merger<'a, K, V>);

impl<'a, K: Key + OrdLowerBound, V: Value> LSMTreeIter<'a, K, V> {
    pub async fn advance_with_hint(&mut self, hint: &K) -> Result<(), Error> {
        self.0.advance_with_hint(hint).await
    }
}

#[async_trait]
impl<'a, K: Key + OrdLowerBound, V: Value> LayerIterator<K, V> for LSMTreeIter<'a, K, V> {
    fn get(&self) -> Option<ItemRef<'_, K, V>> {
        self.0.get()
    }

    async fn advance(&mut self) -> Result<(), Error> {
        self.0.advance().await
    }
}

#[cfg(test)]
mod tests {
    use {
        super::LSMTree,
        crate::{
            lsm_tree::{
                merge::{MergeIterator, MergeResult},
                types::{Item, ItemRef, LayerIterator, OrdLowerBound},
            },
            testing::fake_object::{FakeObject, FakeObjectHandle},
        },
        fuchsia_async as fasync,
        std::{
            ops::Bound,
            sync::{Arc, Mutex},
        },
    };

    #[derive(Clone, Eq, PartialEq, Debug, serde::Serialize, serde::Deserialize)]
    struct TestKey(std::ops::Range<u64>);

    impl Ord for TestKey {
        fn cmp(&self, other: &TestKey) -> std::cmp::Ordering {
            self.0.end.cmp(&other.0.end)
        }
    }

    impl PartialOrd for TestKey {
        fn partial_cmp(&self, other: &TestKey) -> Option<std::cmp::Ordering> {
            Some(self.cmp(other))
        }
    }

    impl OrdLowerBound for TestKey {
        fn cmp_lower_bound(&self, other: &Self) -> std::cmp::Ordering {
            self.0.start.cmp(&other.0.start)
        }
    }

    fn emit_left_merge_fn(
        _left: &MergeIterator<'_, TestKey, u64>,
        _right: &MergeIterator<'_, TestKey, u64>,
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
        let mut iter = layers.seek(Bound::Unbounded).await.expect("seek failed");
        let ItemRef { key, value } = iter.get().expect("missing item");
        assert_eq!((key, value), (&items[0].key, &items[0].value));
        iter.advance().await.expect("advance failed");
        let ItemRef { key, value } = iter.get().expect("missing item");
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
        tree.seal();
        tree.insert(items[2].clone()).await;
        tree.insert(items[3].clone()).await;
        tree.seal();
        let object = Arc::new(Mutex::new(FakeObject::new()));
        let handle = FakeObjectHandle::new(object.clone());
        tree.compact(handle).await.expect("compact failed");
        let handle = FakeObjectHandle::new(object.clone());
        let tree = LSMTree::open(emit_left_merge_fn, [handle].into());

        let layers = tree.layer_set();
        let mut iter = layers.seek(Bound::Unbounded).await.expect("seek failed");
        for i in 1..5 {
            let ItemRef { key, value } = iter.get().expect("missing item");
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
        tree.seal();
        tree.insert(items[2].clone()).await;
        tree.insert(items[3].clone()).await;

        let item = tree.find(&items[1].key).await.expect("find failed").expect("not found");
        assert_eq!(item, items[1]);
        assert!(tree.find(&TestKey(100..100)).await.expect("find failed").is_none());
    }
}
