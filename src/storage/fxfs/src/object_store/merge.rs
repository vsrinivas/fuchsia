// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::record::{
        AttributeKey, ExtentKey, ExtentValue, ObjectItem, ObjectKey, ObjectKeyData, ObjectValue,
    },
    crate::lsm_tree::merge::{
        ItemOp::{Discard, Keep, Replace},
        MergeLayerIterator, MergeResult,
    },
};

fn merge_extents<'a>(
    object_id: u64,
    attribute_id: u64,
    left_layer: u16,
    right_layer: u16,
    left_key: &ExtentKey,
    right_key: &ExtentKey,
    left_value: &ExtentValue,
    right_value: &ExtentValue,
) -> MergeResult<ObjectKey, ObjectValue> {
    // For now, we don't support/expect two extents with the same key in one layer.
    debug_assert!(right_layer != left_layer);

    // TODO(jfsulliv): once we are at the base layer, we should be deleting records mapping to
    // deleted extents. Otherwise they'll stick around forever.
    if left_key.range.end <= right_key.range.start {
        // Extents don't overlap.
        return MergeResult::EmitLeft;
    }

    // The start of the left extent is <= the start of the right extent, due to merge key ordering.
    //
    // One of the extents has to win. The way we break this tie is by picking the extent from the
    // newest layer (i.e. the layer with the lowest index).
    //
    // Generally, we'll be doing the following:
    //
    //  Old  |----------|
    //  New          |----------|
    //
    // Turns into
    //
    //  Emit  |------|
    //  Old          |--|
    //  New          |----------|
    if right_layer < left_layer {
        // Right layer is newer.
        debug_assert!(left_key.range.start < right_key.range.start);
        return MergeResult::Other {
            emit: Some(ObjectItem::new(
                ObjectKey::extent(
                    object_id,
                    attribute_id,
                    left_key.range.start..right_key.range.start,
                ),
                ObjectValue::Extent(*left_value),
            )),
            left: Replace(ObjectItem::new(
                ObjectKey::extent(
                    object_id,
                    attribute_id,
                    right_key.range.start..left_key.range.end,
                ),
                ObjectValue::Extent(
                    left_value.offset_by(right_key.range.start - left_key.range.start),
                ),
            )),
            right: Keep,
        };
    }
    // Left layer is newer.
    if left_key.range.end >= right_key.range.end {
        // The left key entirely contains the right key.
        return MergeResult::Other { emit: None, left: Keep, right: Discard };
    }
    MergeResult::Other {
        emit: None,
        left: Keep,
        right: Replace(ObjectItem::new(
            ObjectKey::extent(object_id, attribute_id, left_key.range.end..right_key.range.end),
            ObjectValue::Extent(right_value.offset_by(left_key.range.end - right_key.range.start)),
        )),
    }
}

// Assumes that the two extents to be merged are on adjacent layers (i.e. layers N, N+1).
fn merge_deleted_extents<'a>(
    object_id: u64,
    attribute_id: u64,
    left_key: &ExtentKey,
    right_key: &ExtentKey,
) -> MergeResult<ObjectKey, ObjectValue> {
    if left_key.range.end < right_key.range.start {
        // The extents are not adjacent or overlapping.
        return MergeResult::EmitLeft;
    }
    // Both of these are deleted extents which are either adjacent or overlapping, which means
    // we can coalece the records.
    if left_key.range.end >= right_key.range.end {
        // The left deletion eclipses the right, so just keep the left.
        return MergeResult::Other { emit: None, left: Keep, right: Discard };
    }
    MergeResult::Other {
        emit: None,
        left: Discard,
        right: Replace(ObjectItem::new(
            ObjectKey::extent(object_id, attribute_id, left_key.range.start..right_key.range.end),
            ObjectValue::deleted_extent(),
        )),
    }
}

/// Merge function for items in the object store.
///
/// The most interesting behaviour in this merge function is how extents are handled. Since extents
/// can overlap and replace one another, the merge function generally builds up the most
/// recent view of the extents in the tree, so that the output of a full merge contains no
/// overlapping extents. You can imagine looking down at the extents from the top-most layer.
///
/// A brief example:
///
/// Layer 0   |a-a-a-a|     |b-b-b|
/// Layer 1   |c-c-c-c-c|
/// Layer 2                     |d-d-d-d|
///
/// Merged    |a-a-a-a|c|   |b-b-b|d-d-d|
///
/// Adjacent or overlapping extent deletions in two adjacent layers can be merged into single
/// records (since they do not have a physical offset, so there's no need to keep the physical
/// extents contiguous). We can't merge deletions from non-adjacent layers, since that would
/// cause issues in situations like this:
///
/// Layer 0         |X-X-X|
/// Layer 1   |a-a-a-a-a-a|
/// Layer 2   |X-X-X|
///
/// Merging the two deletions in layers 0 and 2 would either result in the middle extent being
/// fully occluded or not at all (depending on whether we replaced on the left or right layer).
///
/// TODO(jfsulliv): At the base layer, we should prune deleted extents completely.
pub fn merge(
    left: &MergeLayerIterator<'_, ObjectKey, ObjectValue>,
    right: &MergeLayerIterator<'_, ObjectKey, ObjectValue>,
) -> MergeResult<ObjectKey, ObjectValue> {
    if left.key().object_id != right.key().object_id {
        return MergeResult::EmitLeft;
    }
    match (left.key(), right.key(), left.value(), right.value()) {
        (
            ObjectKey {
                object_id,
                data: ObjectKeyData::Attribute(left_attr_id, AttributeKey::Extent(left_extent_key)),
            },
            ObjectKey {
                object_id: _,
                data:
                    ObjectKeyData::Attribute(right_attr_id, AttributeKey::Extent(right_extent_key)),
            },
            ObjectValue::Extent(left_extent),
            ObjectValue::Extent(right_extent),
        ) if left_attr_id == right_attr_id => {
            if let (None, None) = (left_extent.device_offset, right_extent.device_offset) {
                if (left.layer_index as i32 - right.layer_index as i32).abs() == 1 {
                    // Two deletions in adjacent layers can be merged.
                    return merge_deleted_extents(
                        *object_id,
                        *left_attr_id,
                        left_extent_key,
                        right_extent_key,
                    );
                }
            }
            return merge_extents(
                *object_id,
                *left_attr_id,
                left.layer_index,
                right.layer_index,
                left_extent_key,
                right_extent_key,
                left_extent,
                right_extent,
            );
        }
        (ObjectKey { data: ObjectKeyData::Tombstone, .. }, _, _, _) => {
            MergeResult::Other { emit: None, left: Keep, right: Discard }
        }
        (left_key, right_key, _, _) if left_key == right_key => {
            MergeResult::Other { emit: None, left: Keep, right: Discard }
        }
        _ => MergeResult::EmitLeft,
    }
}

#[cfg(test)]
mod tests {
    use {
        super::merge,
        crate::{
            lsm_tree::{
                types::{Item, LayerIterator},
                LSMTree,
            },
            object_store::record::{ObjectItem, ObjectKey, ObjectValue, Timestamp},
        },
        anyhow::Error,
        fuchsia_async as fasync,
        std::ops::Bound,
    };

    async fn test_merge(layer0: &[ObjectItem], layer1: &[ObjectItem], expected: &[ObjectItem]) {
        let tree = LSMTree::new(merge);
        for item in layer1 {
            tree.insert(item.clone()).await;
        }
        tree.seal().await;
        for item in layer0 {
            tree.insert(item.clone()).await;
        }
        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Unbounded).await.expect("seek failed");
        for e in expected {
            assert_eq!(iter.get().expect("get failed"), e.as_item_ref());
            iter.advance().await.expect("advance failed");
        }
        assert!(iter.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_extents_non_overlapping() -> Result<(), Error> {
        let object_id = 0;
        let attr_id = 0;
        let tree = LSMTree::<ObjectKey, ObjectValue>::new(merge);

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..512),
            ObjectValue::extent(0),
        ))
        .await;
        tree.seal().await;

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 512..1024),
            ObjectValue::extent(16384),
        ))
        .await;

        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(std::ops::Bound::Unbounded).await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 0..512));
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 512..1024));
        iter.advance().await?;
        assert!(iter.get().is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_extents_rewrite_right() -> Result<(), Error> {
        let object_id = 0;
        let attr_id = 0;
        let tree = LSMTree::<ObjectKey, ObjectValue>::new(merge);

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..1024),
            ObjectValue::extent(0),
        ))
        .await;
        tree.seal().await;

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 512..1024),
            ObjectValue::extent(16384),
        ))
        .await;

        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(std::ops::Bound::Unbounded).await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 0..512));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::extent(0));
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 512..1024));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::extent(16384));
        iter.advance().await?;
        assert!(iter.get().is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_extents_rewrite_left() -> Result<(), Error> {
        let object_id = 0;
        let attr_id = 0;
        let tree = LSMTree::<ObjectKey, ObjectValue>::new(merge);

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..1024),
            ObjectValue::extent(0),
        ))
        .await;
        tree.seal().await;

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..512),
            ObjectValue::extent(16384),
        ))
        .await;

        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(std::ops::Bound::Unbounded).await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 0..512));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::extent(16384));
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 512..1024));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::extent(512));
        iter.advance().await?;
        assert!(iter.get().is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_extents_rewrite_middle() -> Result<(), Error> {
        let object_id = 0;
        let attr_id = 0;
        let tree = LSMTree::<ObjectKey, ObjectValue>::new(merge);

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..2048),
            ObjectValue::extent(0),
        ))
        .await;
        tree.seal().await;

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 1024..1536),
            ObjectValue::extent(16384),
        ))
        .await;

        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(std::ops::Bound::Unbounded).await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 0..1024));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::extent(0));
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 1024..1536));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::extent(16384));
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 1536..2048));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::extent(1536));
        iter.advance().await?;
        assert!(iter.get().is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_extents_rewrite_eclipses() -> Result<(), Error> {
        let object_id = 0;
        let attr_id = 0;
        let tree = LSMTree::<ObjectKey, ObjectValue>::new(merge);

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 1024..1536),
            ObjectValue::extent(0),
        ))
        .await;
        tree.seal().await;

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..2048),
            ObjectValue::extent(16384),
        ))
        .await;

        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(std::ops::Bound::Unbounded).await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 0..2048));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::extent(16384));
        iter.advance().await?;
        assert!(iter.get().is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_extents_delete_left() -> Result<(), Error> {
        let object_id = 0;
        let attr_id = 0;
        let tree = LSMTree::<ObjectKey, ObjectValue>::new(merge);

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..1024),
            ObjectValue::extent(0),
        ))
        .await;
        tree.seal().await;

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..512),
            ObjectValue::deleted_extent(),
        ))
        .await;

        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(std::ops::Bound::Unbounded).await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 0..512));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::deleted_extent());
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 512..1024));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::extent(512));
        iter.advance().await?;
        assert!(iter.get().is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_extents_delete_right() -> Result<(), Error> {
        let object_id = 0;
        let attr_id = 0;
        let tree = LSMTree::<ObjectKey, ObjectValue>::new(merge);

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..1024),
            ObjectValue::extent(0),
        ))
        .await;
        tree.seal().await;

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 512..1024),
            ObjectValue::deleted_extent(),
        ))
        .await;

        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(std::ops::Bound::Unbounded).await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 0..512));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::extent(0));
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 512..1024));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::deleted_extent());
        iter.advance().await?;
        assert!(iter.get().is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_extents_delete_middle() -> Result<(), Error> {
        let object_id = 0;
        let attr_id = 0;
        let tree = LSMTree::<ObjectKey, ObjectValue>::new(merge);

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..2048),
            ObjectValue::extent(0),
        ))
        .await;
        tree.seal().await;

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 1024..1536),
            ObjectValue::deleted_extent(),
        ))
        .await;

        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(std::ops::Bound::Unbounded).await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 0..1024));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::extent(0));
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 1024..1536));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::deleted_extent());
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 1536..2048));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::extent(1536));
        iter.advance().await?;
        assert!(iter.get().is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_extents_delete_eclipses() -> Result<(), Error> {
        let object_id = 0;
        let attr_id = 0;
        let tree = LSMTree::<ObjectKey, ObjectValue>::new(merge);

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 1024..1536),
            ObjectValue::extent(0),
        ))
        .await;
        tree.seal().await;

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..2048),
            ObjectValue::deleted_extent(),
        ))
        .await;

        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(std::ops::Bound::Unbounded).await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 0..2048));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::deleted_extent());
        iter.advance().await?;
        assert!(iter.get().is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_deleted_extents_new_layer_joins_two_deletions() -> Result<(), Error> {
        // Old layer:  [----]    [----]
        // New layer:       [----]
        // Merged:     [--------------]
        let object_id = 0;
        let attr_id = 0;
        let tree = LSMTree::<ObjectKey, ObjectValue>::new(merge);

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..512),
            ObjectValue::deleted_extent(),
        ))
        .await;
        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 1024..1536),
            ObjectValue::deleted_extent(),
        ))
        .await;
        tree.seal().await;

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 512..1024),
            ObjectValue::deleted_extent(),
        ))
        .await;
        tree.seal().await;

        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(std::ops::Bound::Unbounded).await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 0..1536));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::deleted_extent());
        iter.advance().await?;
        assert!(iter.get().is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_deleted_extents_new_layer_joined_by_old_deletion() -> Result<(), Error> {
        // Old layer:       [----]
        // New layer:  [----]    [----]
        // Merged:     [--------------]
        let object_id = 0;
        let attr_id = 0;
        let tree = LSMTree::<ObjectKey, ObjectValue>::new(merge);

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 512..1024),
            ObjectValue::deleted_extent(),
        ))
        .await;
        tree.seal().await;

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..512),
            ObjectValue::deleted_extent(),
        ))
        .await;
        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 1024..1536),
            ObjectValue::deleted_extent(),
        ))
        .await;
        tree.seal().await;

        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(std::ops::Bound::Unbounded).await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 0..1536));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::deleted_extent());
        iter.advance().await?;
        assert!(iter.get().is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_deleted_extents_overlapping_newest_on_right() -> Result<(), Error> {
        let object_id = 0;
        let attr_id = 0;
        let tree = LSMTree::<ObjectKey, ObjectValue>::new(merge);

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..1024),
            ObjectValue::deleted_extent(),
        ))
        .await;
        tree.seal().await;

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 512..1536),
            ObjectValue::deleted_extent(),
        ))
        .await;

        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(std::ops::Bound::Unbounded).await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 0..1536));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::deleted_extent());
        iter.advance().await?;
        assert!(iter.get().is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_deleted_extents_overlapping_newest_on_left() -> Result<(), Error> {
        let object_id = 0;
        let attr_id = 0;
        let tree = LSMTree::<ObjectKey, ObjectValue>::new(merge);

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 512..1536),
            ObjectValue::deleted_extent(),
        ))
        .await;
        tree.seal().await;
        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..1024),
            ObjectValue::deleted_extent(),
        ))
        .await;

        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(std::ops::Bound::Unbounded).await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 0..1536));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::deleted_extent());
        iter.advance().await?;
        assert!(iter.get().is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_deleted_extents_new_layer_contained_in_old() -> Result<(), Error> {
        // Old layer:  [--------------]
        // New layer:       [----]
        // Merged:     [--------------]
        let object_id = 0;
        let attr_id = 0;
        let tree = LSMTree::<ObjectKey, ObjectValue>::new(merge);

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..1536),
            ObjectValue::deleted_extent(),
        ))
        .await;
        tree.seal().await;

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 512..1024),
            ObjectValue::deleted_extent(),
        ))
        .await;
        tree.seal().await;

        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(std::ops::Bound::Unbounded).await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 0..1536));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::deleted_extent());
        iter.advance().await?;
        assert!(iter.get().is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_deleted_extents_new_layer_eclipses_old() -> Result<(), Error> {
        // Old layer:       [----]
        // New layer:  [--------------]
        // Merged:     [--------------]
        let object_id = 0;
        let attr_id = 0;
        let tree = LSMTree::<ObjectKey, ObjectValue>::new(merge);

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 512..1024),
            ObjectValue::deleted_extent(),
        ))
        .await;
        tree.seal().await;

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..1536),
            ObjectValue::deleted_extent(),
        ))
        .await;
        tree.seal().await;

        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(std::ops::Bound::Unbounded).await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 0..1536));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::deleted_extent());
        iter.advance().await?;
        assert!(iter.get().is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_deleted_extents_does_not_coalesce_if_not_adjacent_layers(
    ) -> Result<(), Error> {
        // Layer 0:  [XXXXX]
        // Layer 1:  [--------------]
        // Layer 2:        [XXXXXXXX]
        //  Merged:  [XXXXX|--------]
        let object_id = 0;
        let attr_id = 0;
        let tree = LSMTree::<ObjectKey, ObjectValue>::new(merge);

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 512..1024),
            ObjectValue::deleted_extent(),
        ))
        .await;
        tree.seal().await;

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..1024),
            ObjectValue::extent(0),
        ))
        .await;
        tree.seal().await;

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..512),
            ObjectValue::deleted_extent(),
        ))
        .await;

        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(std::ops::Bound::Unbounded).await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 0..512));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::deleted_extent());
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 512..1024));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::extent(512));
        iter.advance().await?;
        assert!(iter.get().is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_deleted_extents_does_not_coalesce_if_not_adjacent_deletions(
    ) -> Result<(), Error> {
        // Layer 0:  [XXXXX|--------]
        // Layer 1:           [XXXXX]
        //  Merged:  [XXXXX|--------]
        let object_id = 0;
        let attr_id = 0;
        let tree = LSMTree::<ObjectKey, ObjectValue>::new(merge);

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 1024..1536),
            ObjectValue::extent(0),
        ))
        .await;
        tree.seal().await;

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..512),
            ObjectValue::deleted_extent(),
        ))
        .await;
        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 512..1536),
            ObjectValue::extent(0),
        ))
        .await;

        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(std::ops::Bound::Unbounded).await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 0..512));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::deleted_extent());
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 512..1536));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::extent(0));
        iter.advance().await?;
        assert!(iter.get().is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_deleted_extent_into_overwrites_extents() -> Result<(), Error> {
        let object_id = 0;
        let attr_id = 0;
        let tree = LSMTree::<ObjectKey, ObjectValue>::new(merge);

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..1024),
            ObjectValue::extent(0),
        ))
        .await;
        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 1024..2048),
            ObjectValue::extent(16384),
        ))
        .await;
        let key = ObjectKey::extent(object_id, attr_id, 512..1536);
        tree.merge_into(
            Item::new(key.clone(), ObjectValue::deleted_extent()),
            &key.key_for_merge_into(),
        )
        .await;

        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(std::ops::Bound::Unbounded).await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 0..512));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::extent(0));
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 512..1536));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::deleted_extent());
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 1536..2048));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::extent(16896));
        iter.advance().await?;
        assert!(iter.get().is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_deleted_extent_into_merges_with_other_deletions() -> Result<(), Error> {
        let object_id = 0;
        let attr_id = 0;
        let tree = LSMTree::<ObjectKey, ObjectValue>::new(merge);

        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 0..1024),
            ObjectValue::deleted_extent(),
        ))
        .await;
        tree.insert(Item::new(
            ObjectKey::extent(object_id, attr_id, 1024..2048),
            ObjectValue::deleted_extent(),
        ))
        .await;

        let key = ObjectKey::extent(object_id, attr_id, 512..1536);
        tree.merge_into(
            Item::new(key.clone(), ObjectValue::deleted_extent()),
            &key.key_for_merge_into(),
        )
        .await;

        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(std::ops::Bound::Unbounded).await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(object_id, attr_id, 0..2048));
        assert_eq!(iter.get().unwrap().value, &ObjectValue::deleted_extent());
        iter.advance().await?;
        assert!(iter.get().is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_size_records() {
        let left = &[Item::new(ObjectKey::attribute(1, 0), ObjectValue::attribute(5))];
        let right = &[Item::new(ObjectKey::attribute(1, 0), ObjectValue::attribute(10))];
        test_merge(left, right, left).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_different_attributes_not_merged() {
        let left = Item::new(ObjectKey::attribute(1, 0), ObjectValue::attribute(5));
        let right = Item::new(ObjectKey::attribute(1, 1), ObjectValue::attribute(10));
        test_merge(&[left.clone()], &[right.clone()], &[left, right]).await;

        let left = Item::new(ObjectKey::extent(1, 0, 0..100), ObjectValue::extent(0));
        let right = Item::new(ObjectKey::extent(1, 1, 0..100), ObjectValue::extent(1));
        test_merge(&[left.clone()], &[right.clone()], &[left, right]).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_tombstone_discards_all_other_records() {
        let tombstone = Item::new(ObjectKey::tombstone(1), ObjectValue::None);
        let other_object = Item::new(
            ObjectKey::object(2),
            ObjectValue::file(1, 0, Timestamp::default(), Timestamp::default()),
        );
        test_merge(
            &[tombstone.clone()],
            &[
                Item::new(
                    ObjectKey::object(1),
                    ObjectValue::file(1, 100, Timestamp::default(), Timestamp::default()),
                ),
                Item::new(ObjectKey::attribute(1, 0), ObjectValue::attribute(100)),
                Item::new(ObjectKey::extent(1, 0, 0..100), ObjectValue::extent(5000)),
                other_object.clone(),
            ],
            &[tombstone, other_object],
        )
        .await;
    }
}
