pub mod merge;
mod simple_persistent_layer;
pub mod skip_list_layer;
#[cfg(test)]
mod tests;

use {
    crate::object_handle::ObjectHandle,
    anyhow::Error,
    serde::{Deserialize, Serialize},
    simple_persistent_layer::SimplePersistentLayerWriter,
    std::{
        ops::Bound,
        ptr::NonNull,
        sync::{Arc, RwLock},
    },
};

const SKIP_LIST_LAYER_ITEMS: usize = 512;

// Use trait_alias when available.
pub trait Key:
    std::cmp::Ord
    + OrdLowerBound
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
    std::fmt::Debug + Send + Sync + serde::de::DeserializeOwned + serde::Serialize + std::marker::Unpin + 'static
{
}

impl<K> Key for K where
    K: std::cmp::Ord
        + OrdLowerBound
        + std::fmt::Debug
        + Send
        + Sync
        + std::marker::Unpin
        + serde::de::DeserializeOwned
        + serde::Serialize
        + 'static
{
}
impl<V> Value for V where
    V: std::fmt::Debug + Send + Sync + std::marker::Unpin + serde::de::DeserializeOwned + serde::Serialize + 'static
{
}

#[derive(Debug, Serialize)]
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

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Item<K, V> {
    pub key: K,
    pub value: V,
}

impl<K, V> Item<K, V> {
    #[cfg(test)]
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

pub trait OrdLowerBound {
    fn cmp_lower_bound(&self, other: &Self) -> std::cmp::Ordering;
}

// TODO: make this private.
pub trait LayerIterator<K, V>: Send {
    fn seek(&mut self, bound: std::ops::Bound<&K>) -> Result<(), Error>;
    fn advance(&mut self) -> Result<(), Error>;
    fn get(&self) -> Option<ItemRef<'_, K, V>>;
    // TODO: remove this
    fn discard_or_advance(&mut self) -> Result<(), Error>;
}

pub trait LayerIteratorMut<K, V>: LayerIterator<K, V> {
    fn as_iterator_mut(&mut self) -> &mut dyn LayerIterator<K, V>;
    fn as_iterator(&self) -> &dyn LayerIterator<K, V>;
    fn erase(&mut self);
    fn insert_before(&mut self, item: Item<K, V>);
}

type BoxedLayerIterator<'iter, K, V> = Box<dyn LayerIterator<K, V> + 'iter>;

pub trait Layer<K, V>: Send + Sync {
    fn get_iterator(&self) -> BoxedLayerIterator<'_, K, V>;
}

pub trait MutableLayer<K, V>: Layer<K, V> {
    fn as_layer(self: Arc<Self>) -> Arc<dyn Layer<K, V>>;

    fn dump(&self);

    fn insert(&self, item: Item<K, V>);

    fn replace_range(&self, item: Item<K, V>, lower_bound: &K, merge_fn: merge::MergeFn<K, V>);

    fn replace_or_insert(&self, item: Item<K, V>);
}

struct Data<K, V> {
    mutable_layer: Arc<dyn MutableLayer<K, V>>,
    layers: Vec<Arc<dyn Layer<K, V>>>,
}

pub struct LSMTree<K, V> {
    data: RwLock<Data<K, V>>,
    merge_fn: merge::MergeFn<K, V>,
}

pub struct LSMTreeIter<'iter, K, V> {
    _layers: Box<[Arc<dyn Layer<K, V> + 'iter>]>,
    merger: merge::Merger<'iter, K, V>,
}

impl<
        'iter,
        K: std::fmt::Debug + OrdLowerBound + Unpin + 'static,
        V: std::fmt::Debug + Unpin + 'static,
    > LSMTreeIter<'iter, K, V>
{
    fn new(layers: Box<[Arc<dyn Layer<K, V> + 'iter>]>, merge_fn: merge::MergeFn<K, V>) -> Self {
        let iterators: Box<[BoxedLayerIterator<'_, K, V>]> = layers
            .iter()
            .map(|x| {
                let ptr = NonNull::from(x);
                unsafe { &*ptr.as_ptr() }.get_iterator()
            })
            .collect();
        LSMTreeIter { _layers: layers, merger: merge::Merger::new(iterators, merge_fn) }
    }

    pub fn get(&self) -> Option<ItemRef<'_, K, V>> {
        self.merger.get()
    }

    pub fn advance(&mut self) -> Result<(), Error> {
        self.merger.advance()
    }

    pub fn advance_to(&mut self, key: &K) -> Result<(), Error> {
        self.merger.advance_to(key)
    }
}

impl<'tree, K: Key, V: Value> LSMTree<K, V>
where
    for<'de> K: serde::Deserialize<'de>,
    for<'de> V: serde::Deserialize<'de>,
{
    pub fn new(merge_fn: merge::MergeFn<K, V>) -> Self {
        LSMTree {
            data: RwLock::new(Data {
                mutable_layer: Arc::new(skip_list_layer::SkipListLayer::new(SKIP_LIST_LAYER_ITEMS)),
                layers: Vec::new(),
            }),
            merge_fn,
        }
    }

    fn layers_from_handles(
        handles: Box<[impl ObjectHandle + 'static]>,
    ) -> Vec<Arc<dyn Layer<K, V>>> {
        handles
            .into_vec()
            .drain(..)
            .map(|h| {
                Arc::new(simple_persistent_layer::SimplePersistentLayer::new(h, 512))
                    as Arc<dyn Layer<K, V>>
            })
            .collect()
    }

    pub fn open(
        merge_fn: merge::MergeFn<K, V>,
        handles: Box<[impl ObjectHandle + 'static]>,
    ) -> Self {
        LSMTree {
            data: RwLock::new(Data {
                mutable_layer: Arc::new(skip_list_layer::SkipListLayer::new(SKIP_LIST_LAYER_ITEMS)),
                layers: Self::layers_from_handles(handles),
            }),
            merge_fn,
        }
    }

    pub fn set_layers(&self, handles: Box<[impl ObjectHandle + 'static]>) {
        let mut data = self.data.write().unwrap();
        data.layers = Self::layers_from_handles(handles);
    }

    fn add_all_layers<'a>(&'a self, all_layers: &mut Vec<Arc<dyn Layer<K, V>>>) {
        let data = self.data.read().unwrap();
        all_layers.push(data.mutable_layer.clone().as_layer().into());
        for layer in &data.layers {
            all_layers.push(layer.clone().into());
        }
    }

    pub fn dump_mutable_layer(&self) {
        let data = self.data.read().unwrap();
        data.mutable_layer.dump();
    }

    // TODO(csuter): This should maybe run on a different thread. Depends when it's
    // called.
    // Writes the current memory layer to the given object handle and creates a new one.
    pub fn commit<'a>(
        &'a self,
        mut object_handle: impl ObjectHandle + 'static,
    ) -> Result<(), Error> {
        let mut layers = Vec::new();
        self.add_all_layers(&mut layers);
        {
            let mut data = self.data.write().unwrap();
            data.mutable_layer =
                Arc::new(skip_list_layer::SkipListLayer::new(SKIP_LIST_LAYER_ITEMS));
            data.layers = layers.clone();
        }
        // TODO: optimize for the case where the mutable layer is empty.
        {
            let mut writer = SimplePersistentLayerWriter::new(&mut object_handle, 512);
            let iterators = layers.iter().map(|x| x.get_iterator()).collect();
            let mut merger = merge::Merger::new(iterators, self.merge_fn);
            merger.advance()?;
            while let Some(item_ref) = merger.get() {
                writer.write(item_ref)?;
                merger.advance()?;
            }
            writer.close()?;
        }
        {
            let mut data = self.data.write().unwrap();
            data.layers = vec![Arc::new(simple_persistent_layer::SimplePersistentLayer::new(
                object_handle,
                512,
            ))];
        }
        Ok(())
    }

    pub fn iter(&self) -> LSMTreeIter<'tree, K, V> {
        let mut layers = Vec::new();
        self.add_all_layers(&mut layers);
        LSMTreeIter::new(layers.into_boxed_slice(), self.merge_fn)
    }

    pub fn iter_with_layers(
        &self,
        mut layers: Vec<Arc<dyn Layer<K, V>>>,
    ) -> LSMTreeIter<'tree, K, V> {
        self.add_all_layers(&mut layers);
        LSMTreeIter::new(layers.into_boxed_slice(), self.merge_fn)
    }

    pub fn insert(&self, item: Item<K, V>) {
        let mut data = self.data.write().unwrap(); // TODO: probably should be read and clone.
        Arc::get_mut(&mut data.mutable_layer).unwrap().insert(item); // TODO: is get_mut call always safe?
    }

    pub fn replace_or_insert(&self, item: Item<K, V>) {
        let mut data = self.data.write().unwrap();
        Arc::get_mut(&mut data.mutable_layer).unwrap().replace_or_insert(item); // TODO
    }

    pub fn replace_range(&self, item: Item<K, V>, lower_bound: &K) {
        let mut data = self.data.write().unwrap();
        Arc::get_mut(&mut data.mutable_layer).unwrap().replace_range(
            item,
            lower_bound,
            self.merge_fn,
        );
    }

    pub fn range_from(
        &self,
        bound: std::ops::Bound<&K>,
    ) -> Result<LSMTreeIter<'tree, K, V>, Error> {
        let mut layers = Vec::new();
        self.add_all_layers(&mut layers);
        let mut iter = LSMTreeIter::new(layers.into_boxed_slice(), self.merge_fn);
        match bound {
            Bound::Unbounded => iter.merger.advance()?,
            Bound::Included(key) => iter.merger.advance_to(key)?,
            Bound::Excluded(_) => panic!("Excluded bounds not supported!"),
        };
        Ok(iter)
    }

    pub fn find(&self, search_key: &K) -> Result<Option<Item<K, V>>, Error>
    where
        K: Clone,
        V: Clone,
    {
        let mut layers = Vec::new();
        self.add_all_layers(&mut layers);
        let iterators = layers.iter().map(|x| x.get_iterator()).collect();
        let mut merger = merge::Merger::new(iterators, self.merge_fn);
        merger.advance_to(search_key)?;
        Ok(match merger.get() {
            Some(ItemRef { key, value }) => {
                if key == search_key {
                    Some(Item { key: key.clone(), value: value.clone() })
                } else {
                    None
                }
            }
            _ => None,
        })
    }
}
