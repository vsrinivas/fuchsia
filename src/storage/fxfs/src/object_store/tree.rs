// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        lsm_tree::{
            types::{BoxedLayerIterator, MergeableKey, Value},
            LSMTree, LockedLayer,
        },
        object_handle::WriteBytes,
        object_store::journal,
    },
    anyhow::{Context, Error},
    std::ops::Bound,
};

pub trait MajorCompactable<K, V> {
    /// Returns an iterator that wrapps another iterator that is appropriate for major compactions.
    /// Such an iterator should elide items that don't need to be written if a major compaction is
    /// taking place.
    fn major_iter(iter: BoxedLayerIterator<'_, K, V>) -> BoxedLayerIterator<'_, K, V> {
        iter
    }
}

type Layers<K, V> = Vec<LockedLayer<K, V>>;

/// Picks an appropriate set of layers to flush from the tree and writes them to writer.  After
/// successfully writing a new layer, it returns the layers that should be kept and the old layers
/// that should be purged.
pub async fn flush<'a, K: MergeableKey, V: Value>(
    tree: &'a LSMTree<K, V>,
    writer: impl WriteBytes + Send,
) -> Result<(Layers<K, V>, Layers<K, V>), Error>
where
    LSMTree<K, V>: MajorCompactable<K, V>,
{
    let mut layer_set = tree.immutable_layer_set();
    let mut total_size = 0;
    let mut layer_count = 0;
    let mut split_index = layer_set
        .layers
        .iter()
        .position(|layer| {
            match layer.handle() {
                None => {}
                Some(handle) => {
                    let size = handle.get_size();
                    // Stop adding more layers when the total size of all the immutable layers
                    // so far is less than 3/4 of the layer we are looking at.
                    if total_size > 0 && total_size * 4 < size * 3 {
                        return true;
                    }
                    total_size += size;
                    layer_count += 1;
                }
            }
            false
        })
        .unwrap_or(layer_set.layers.len());

    // If there's only one immutable layer to merge with and it's big, don't merge with it.
    // Instead, we'll create a new layer and eventually that will grow to be big enough to merge
    // with the existing layers.  The threshold here cannot be so big such that merging with a
    // layer that big ends up using so much journal space that we have to immediately flush
    // again as that has the potential to end up in an infinite loop, and so the number chosen
    // here is related to the journal's RECLAIM_SIZE.
    if layer_count == 1
        && total_size > journal::RECLAIM_SIZE
        && layer_set.layers[split_index - 1].handle().is_some()
    {
        split_index -= 1;
    }

    let layers_to_keep = layer_set.layers.split_off(split_index);

    {
        let mut merger = layer_set.merger();
        let mut iter: BoxedLayerIterator<'_, K, V> = Box::new(merger.seek(Bound::Unbounded).await?);
        if layers_to_keep.is_empty() {
            iter = LSMTree::<K, V>::major_iter(iter);
        }
        let block_size = writer.handle().block_size();
        tree.compact_with_iterator(iter, writer, block_size).await.context("ObjectStore::flush")?;
    }

    Ok((layers_to_keep, layer_set.layers))
}
