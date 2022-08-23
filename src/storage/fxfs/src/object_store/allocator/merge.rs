// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        lsm_tree::{
            merge::{
                ItemOp::{Discard, Keep, Replace},
                MergeLayerIterator, MergeResult,
            },
            types::{Item, LayerIteratorFilter},
        },
        object_store::allocator::{AllocatorKey, AllocatorValue, BoxedLayerIterator},
    },
    anyhow::Error,
    std::collections::HashSet,
};

pub fn merge(
    left: &MergeLayerIterator<'_, AllocatorKey, AllocatorValue>,
    right: &MergeLayerIterator<'_, AllocatorKey, AllocatorValue>,
) -> MergeResult<AllocatorKey, AllocatorValue> {
    // Wherever Replace is used below, it must not extend the *end* of the range for whichever item
    // is returned i.e. if replacing the left item, replacement.end <= left.end because otherwise we
    // might not merge records that come after that end point because the merger won't merge records
    // in the same layer

    /*  Case 1: Disjoint
     *    L:    |------------|
     *    R:                      |-----------|
     */
    if left.key().device_range.end < right.key().device_range.start {
        return MergeResult::EmitLeft;
    }

    /*  Case 2: Touching
     *    L:    |------------|
     *    R:                 |-----------|
     */
    if left.key().device_range.end == right.key().device_range.start {
        // We can only merge the range if the values are an exact match.
        if *left.value() == *right.value() {
            return MergeResult::Other {
                emit: None,
                left: Discard,
                right: Replace(Item {
                    key: AllocatorKey {
                        device_range: left.key().device_range.start..right.key().device_range.end,
                    },
                    value: left.value().clone(),
                    sequence: std::cmp::min(left.sequence(), right.sequence()),
                }),
            };
        } else {
            return MergeResult::EmitLeft;
        }
    }
    if left.key().device_range.start == right.key().device_range.start {
        /*  Case 3: Overlap with same start
         *    L:    |------------|
         *    R:    |-----------------|
         */
        if left.key().device_range.end < right.key().device_range.end {
            // The newer value eclipses the older.
            if left.layer_index < right.layer_index {
                return MergeResult::Other {
                    emit: None,
                    left: Keep,
                    right: if left.key().device_range.end == right.key().device_range.end {
                        Discard
                    } else {
                        Replace(Item {
                            key: AllocatorKey {
                                device_range: left.key().device_range.end
                                    ..right.key().device_range.end,
                            },
                            value: right.value().clone(),
                            sequence: right.sequence(),
                        })
                    },
                };
            } else {
                // right is a newer Abs/None than left
                return MergeResult::Other { emit: None, left: Discard, right: Keep };
            }

        /*  Case 4: Overlap with same start
         *    L:    |-----------------|
         *    R:    |------------|
         */
        } else {
            // The newer value eclipses the older.
            if right.layer_index < left.layer_index {
                return MergeResult::Other {
                    emit: None,
                    left: if right.key().device_range.end == left.key().device_range.end {
                        Discard
                    } else {
                        Replace(Item {
                            key: AllocatorKey {
                                device_range: right.key().device_range.end
                                    ..left.key().device_range.end,
                            },
                            value: left.value().clone(),
                            sequence: left.sequence(),
                        })
                    },
                    right: Keep,
                };
            } else {
                // right is a newer Abs/None than left
                return MergeResult::Other { emit: None, left: Keep, right: Discard };
            }
        }
    }
    /*  Case 5: Split off left prefix
     *    L:    |-----...
     *    R:         |-----...
     */
    debug_assert!(left.key().device_range.end >= right.key().device_range.start);
    MergeResult::Other {
        emit: Some(Item {
            key: AllocatorKey {
                device_range: left.key().device_range.start..right.key().device_range.start,
            },
            value: left.value().clone(),
            sequence: left.sequence(),
        }),
        left: Replace(Item {
            key: AllocatorKey {
                device_range: right.key().device_range.start..left.key().device_range.end,
            },
            value: left.value().clone(),
            sequence: left.sequence(),
        }),
        right: Keep,
    }
}

pub async fn filter_tombstones(
    iter: BoxedLayerIterator<'_, AllocatorKey, AllocatorValue>,
) -> Result<BoxedLayerIterator<'_, AllocatorKey, AllocatorValue>, Error> {
    Ok(Box::new(iter.filter(|i| *i.value != AllocatorValue::None).await?))
}

pub async fn filter_marked_for_deletion(
    iter: BoxedLayerIterator<'_, AllocatorKey, AllocatorValue>,
    marked_for_deletion: HashSet<u64>,
) -> Result<BoxedLayerIterator<'_, AllocatorKey, AllocatorValue>, Error> {
    Ok(Box::new(
        iter.filter(move |i| {
            if let AllocatorValue::Abs { owner_object_id, .. } = i.value {
                !marked_for_deletion.contains(owner_object_id)
            } else {
                true
            }
        })
        .await?,
    ))
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            lsm_tree::{
                types::{Item, ItemRef, LayerIterator},
                LSMTree,
            },
            object_handle::INVALID_OBJECT_ID,
            object_store::allocator::{
                merge::{filter_tombstones, merge},
                AllocatorKey, AllocatorValue,
                AllocatorValue::Abs,
            },
        },
        fuchsia_async as fasync,
        std::ops::{Bound, Range},
    };

    // Tests merge logic given (range, delta and object_id) for left, right and expected output.
    async fn test_merge(
        left: (Range<u64>, AllocatorValue),
        right: (Range<u64>, AllocatorValue),
        expected: &[(Range<u64>, AllocatorValue)],
    ) {
        let tree = LSMTree::new(merge);
        tree.insert(Item::new(AllocatorKey { device_range: right.0 }, right.1))
            .await
            .expect("insert error");
        tree.seal().await;
        tree.insert(Item::new(AllocatorKey { device_range: left.0 }, left.1))
            .await
            .expect("insert error");
        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter =
            filter_tombstones(Box::new(merger.seek(Bound::Unbounded).await.expect("seek failed")))
                .await
                .expect("filter failed");
        for e in expected {
            let ItemRef { key, value, .. } = iter.get().expect("get failed");
            assert_eq!((key, value), (&AllocatorKey { device_range: e.0.clone() }, &e.1));
            iter.advance().await.expect("advance failed");
        }
        assert!(iter.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_overlap() {
        test_merge(
            (0..100, Abs { count: 1, owner_object_id: 1 }),
            (200..300, Abs { count: 1, owner_object_id: 1 }),
            &[
                (0..100, Abs { count: 1, owner_object_id: 1 }),
                (200..300, Abs { count: 1, owner_object_id: 1 }),
            ],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_touching() {
        test_merge(
            (0..100, Abs { count: 1, owner_object_id: 1 }),
            (100..200, Abs { count: 1, owner_object_id: 1 }),
            &[(0..200, Abs { count: 1, owner_object_id: 1 })],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_identical() {
        test_merge(
            (0..100, Abs { count: 2, owner_object_id: 1 }),
            (0..100, Abs { count: 1, owner_object_id: 1 }),
            &[(0..100, Abs { count: 2, owner_object_id: 1 })],
        )
        .await;
        test_merge(
            (0..100, AllocatorValue::None),
            (0..100, Abs { count: 1, owner_object_id: 1 }),
            &[],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_left_smaller_than_right_with_same_start() {
        test_merge(
            (0..100, Abs { count: 2, owner_object_id: 1 }),
            (0..200, Abs { count: 1, owner_object_id: 1 }),
            &[
                (0..100, Abs { count: 2, owner_object_id: 1 }),
                (100..200, Abs { count: 1, owner_object_id: 1 }),
            ],
        )
        .await;
        test_merge(
            (0..100, AllocatorValue::None),
            (0..200, Abs { count: 1, owner_object_id: 1 }),
            &[(100..200, Abs { count: 1, owner_object_id: 1 })],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_left_starts_before_right_with_overlap() {
        test_merge(
            (0..200, Abs { count: 2, owner_object_id: 1 }),
            (100..150, Abs { count: 1, owner_object_id: 1 }),
            &[
                (0..100, Abs { count: 2, owner_object_id: 1 }),
                (100..200, Abs { count: 2, owner_object_id: 1 }),
            ],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_different_object_id() {
        // Case 1
        test_merge(
            (0..100, Abs { count: 1, owner_object_id: 1 }),
            (200..300, Abs { count: 1, owner_object_id: 2 }),
            &[
                (0..100, Abs { count: 1, owner_object_id: 1 }),
                (200..300, Abs { count: 1, owner_object_id: 2 }),
            ],
        )
        .await;
        // Case 2
        test_merge(
            (0..100, Abs { count: 1, owner_object_id: 1 }),
            (100..200, Abs { count: 1, owner_object_id: 2 }),
            &[
                (0..100, Abs { count: 1, owner_object_id: 1 }),
                (100..200, Abs { count: 1, owner_object_id: 2 }),
            ],
        )
        .await;
        // Case 3
        test_merge(
            (0..100, Abs { count: 1, owner_object_id: 1 }),
            (0..100, Abs { count: 1, owner_object_id: 2 }),
            &[(0..100, Abs { count: 1, owner_object_id: 1 })],
        )
        .await;
        // Case 4
        test_merge(
            (0..100, Abs { count: 1, owner_object_id: 1 }),
            (0..200, Abs { count: 1, owner_object_id: 2 }),
            &[
                (0..100, Abs { count: 1, owner_object_id: 1 }),
                (100..200, Abs { count: 1, owner_object_id: 2 }),
            ],
        )
        .await;
        // Case 5
        test_merge(
            (0..200, Abs { count: 1, owner_object_id: 1 }),
            (0..100, Abs { count: 1, owner_object_id: 2 }),
            &[(0..200, Abs { count: 1, owner_object_id: 1 })],
        )
        .await;
        // Case 6
        test_merge(
            (0..100, Abs { count: 1, owner_object_id: 1 }),
            (50..150, Abs { count: 1, owner_object_id: 2 }),
            &[
                (0..50, Abs { count: 1, owner_object_id: 1 }),
                (50..100, Abs { count: 1, owner_object_id: 1 }),
                (100..150, Abs { count: 1, owner_object_id: 2 }),
            ],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_tombstones() {
        // We have to make sure we don't prematurely discard records. seal() may be called at
        // any time and the resulting layer tree must remain valid.
        // Here we test absolute allocation counts and reuse of allocated space.
        //
        //  1. Alloc object_id A, write layer file.
        //  2. Dealloc object_id A, Alloc object_id B, write layer file.
        //  3. Dealloc object_id B, Alloc object_id A.
        let key = AllocatorKey { device_range: 0..100 };
        let lower_bound = AllocatorKey::lower_bound_for_merge_into(&key);
        let tree = LSMTree::new(merge);
        tree.merge_into(
            Item::new(key.clone(), AllocatorValue::Abs { count: 1, owner_object_id: 1 }),
            &lower_bound,
        )
        .await;
        tree.seal().await;
        tree.merge_into(
            Item::new(key.clone(), AllocatorValue::Abs { count: 2, owner_object_id: 1 }),
            &lower_bound,
        )
        .await;
        tree.seal().await;
        tree.merge_into(Item::new(key.clone(), AllocatorValue::None), &lower_bound).await;
        tree.merge_into(
            Item::new(key.clone(), AllocatorValue::Abs { count: 1, owner_object_id: 2 }),
            &lower_bound,
        )
        .await;
        tree.seal().await;
        tree.merge_into(Item::new(key.clone(), AllocatorValue::None), &lower_bound).await;
        tree.merge_into(
            Item::new(key.clone(), AllocatorValue::Abs { count: 1, owner_object_id: 1 }),
            &lower_bound,
        )
        .await;
        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Unbounded).await.expect("seek failed");
        let ItemRef { key: k, value, .. } = iter.get().expect("get failed");
        assert_eq!((k, value), (&key, &AllocatorValue::Abs { count: 1, owner_object_id: 1 }));
        iter.advance().await.expect("advance failed");
        assert!(iter.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_preserves_sequences() {
        let tree = LSMTree::new(merge);
        // |1-1-1-1|
        tree.insert(Item {
            key: AllocatorKey { device_range: 0..100 },
            value: AllocatorValue::Abs { count: 1, owner_object_id: INVALID_OBJECT_ID },
            sequence: 1u64,
        })
        .await
        .expect("insert error");
        tree.seal().await;
        // |1|0|1-1|
        tree.insert(Item {
            key: AllocatorKey { device_range: 25..50 },
            value: AllocatorValue::None,
            sequence: 2u64,
        })
        .await
        .expect("insert error");
        // |1|0|1|2|
        tree.insert(Item {
            key: AllocatorKey { device_range: 75..100 },
            value: AllocatorValue::Abs { count: 2, owner_object_id: INVALID_OBJECT_ID },
            sequence: 3u64,
        })
        .await
        .expect("insert error");
        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter =
            filter_tombstones(Box::new(merger.seek(Bound::Unbounded).await.expect("seek failed")))
                .await
                .expect("filter failed");
        assert_eq!(iter.get().unwrap().key, &AllocatorKey { device_range: 0..25 });
        assert_eq!(
            iter.get().unwrap().value,
            &AllocatorValue::Abs { count: 1, owner_object_id: INVALID_OBJECT_ID }
        );
        assert_eq!(iter.get().unwrap().sequence, 1u64);
        iter.advance().await.expect("advance failed");
        assert_eq!(iter.get().unwrap().key, &AllocatorKey { device_range: 50..75 });
        assert_eq!(
            iter.get().unwrap().value,
            &AllocatorValue::Abs { count: 1, owner_object_id: INVALID_OBJECT_ID }
        );
        assert_eq!(iter.get().unwrap().sequence, 1u64);
        iter.advance().await.expect("advance failed");
        assert_eq!(iter.get().unwrap().key, &AllocatorKey { device_range: 75..100 });
        assert_eq!(
            iter.get().unwrap().value,
            &AllocatorValue::Abs { count: 2, owner_object_id: INVALID_OBJECT_ID }
        );
        assert_eq!(iter.get().unwrap().sequence, 3u64);
        iter.advance().await.expect("advance failed");
        assert!(iter.get().is_none());
    }
}
