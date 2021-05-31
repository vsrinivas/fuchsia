// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    lsm_tree::{
        merge::{
            ItemOp::{Discard, Keep, Replace},
            MergeLayerIterator, MergeResult,
        },
        types::Item,
    },
    object_store::allocator::{AllocatorKey, AllocatorValue},
};

pub fn merge(
    left: &MergeLayerIterator<'_, AllocatorKey, AllocatorValue>,
    right: &MergeLayerIterator<'_, AllocatorKey, AllocatorValue>,
) -> MergeResult<AllocatorKey, AllocatorValue> {
    // Wherever Replace is used below, it must not extend the *end* of the range for whichever item
    // is returned i.e. if replacing the left item, replacement.end <= left.end because otherwise we
    // might not merge records that come after that end point because the merger won't merge records
    // in the same layer

    /*  Case 1:
     *    L:    |------------|
     *    R:                      |-----------|
     */
    if left.key().device_range.end < right.key().device_range.start {
        return MergeResult::EmitLeft;
    }
    /*  Case 2:
     *    L:    |------------|
     *    R:                 |-----------|
     */
    if left.key().device_range.end == right.key().device_range.start
        && left.value().delta == right.value().delta
    {
        // Merge the two items together
        return MergeResult::Other {
            emit: None,
            left: Discard,
            right: Replace(Item {
                key: AllocatorKey {
                    device_range: left.key().device_range.start..right.key().device_range.end,
                },
                value: AllocatorValue { delta: left.value().delta },
                sequence: std::cmp::min(left.sequence(), right.sequence()),
            }),
        };
    }
    if left.key().device_range.start == right.key().device_range.start {
        /*  Case 3:
         *    L:    |------------|
         *    R:    |------------|
         */
        if left.key().device_range.end == right.key().device_range.end {
            return MergeResult::Other {
                emit: None,
                left: if left.value().delta + right.value().delta == 0 {
                    Discard
                } else {
                    Replace(Item {
                        key: left.key().clone(),
                        value: AllocatorValue { delta: left.value().delta + right.value().delta },
                        sequence: std::cmp::min(left.sequence(), right.sequence()),
                    })
                },
                right: Discard,
            };
        }
        /*  Case 4:
         *    L:    |------------|
         *    R:    |-----------------|
         */
        if left.key().device_range.end < right.key().device_range.end {
            return MergeResult::Other {
                emit: None,
                left: if left.value().delta + right.value().delta == 0 {
                    Discard
                } else {
                    Replace(Item {
                        key: left.key().clone(),
                        value: AllocatorValue { delta: left.value().delta + right.value().delta },
                        sequence: std::cmp::min(left.sequence(), right.sequence()),
                    })
                },
                right: Replace(Item {
                    key: AllocatorKey {
                        device_range: left.key().device_range.end..right.key().device_range.end,
                    },
                    value: AllocatorValue { delta: right.value().delta },
                    sequence: std::cmp::min(left.sequence(), right.sequence()),
                }),
            };
        }
        /*  Case 5:
         *    L:    |-------------------|
         *    R:    |------------|
         */
        return MergeResult::Other {
            emit: None,
            left: Replace(Item {
                key: AllocatorKey {
                    device_range: right.key().device_range.end..left.key().device_range.end,
                },
                value: AllocatorValue { delta: left.value().delta },
                sequence: std::cmp::min(left.sequence(), right.sequence()),
            }),
            right: if left.value().delta + right.value().delta == 0 {
                Discard
            } else {
                Replace(Item {
                    key: right.key().clone(),
                    value: AllocatorValue { delta: left.value().delta + right.value().delta },
                    sequence: std::cmp::min(left.sequence(), right.sequence()),
                })
            },
        };
    }
    /*  Case 6:
     *    L:    |-----...
     *    R:         |-----...
     */
    MergeResult::Other {
        emit: Some(Item {
            key: AllocatorKey {
                device_range: left.key().device_range.start..right.key().device_range.start,
            },
            value: AllocatorValue { delta: left.value().delta },
            sequence: std::cmp::min(left.sequence(), right.sequence()),
        }),
        left: if right.key().device_range.start == left.key().device_range.end {
            Discard
        } else {
            Replace(Item {
                key: AllocatorKey {
                    device_range: right.key().device_range.start..left.key().device_range.end,
                },
                value: left.value().clone(),
                sequence: std::cmp::min(left.sequence(), right.sequence()),
            })
        },
        right: Keep,
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            lsm_tree::{
                types::{Item, ItemRef, LayerIterator},
                LSMTree,
            },
            object_store::allocator::{merge::merge, AllocatorKey, AllocatorValue},
        },
        fuchsia_async as fasync,
        std::ops::{Bound, Range},
    };

    async fn test_merge(
        left: (Range<u64>, i64),
        right: (Range<u64>, i64),
        expected: &[(Range<u64>, i64)],
    ) {
        let tree = LSMTree::new(merge);
        tree.insert(Item::new(
            AllocatorKey { device_range: right.0 },
            AllocatorValue { delta: right.1 },
        ))
        .await;
        tree.seal().await;
        tree.insert(Item::new(
            AllocatorKey { device_range: left.0 },
            AllocatorValue { delta: left.1 },
        ))
        .await;
        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Unbounded).await.expect("seek failed");
        for e in expected {
            let ItemRef { key, value, .. } = iter.get().expect("get failed");
            assert_eq!(
                (key, value),
                (
                    &AllocatorKey { device_range: e.0.clone() },
                    &AllocatorValue { delta: e.1.clone() }
                )
            );
            iter.advance().await.expect("advance failed");
        }
        assert!(iter.get().is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_overlap() {
        test_merge((0..100, 1), (200..300, 1), &[(0..100, 1), (200..300, 1)]).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_touching() {
        test_merge((0..100, 1), (100..200, 1), &[(0..200, 1)]).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_identical() {
        test_merge((0..100, 1), (0..100, 1), &[(0..100, 2)]).await;
        test_merge((0..100, 1), (0..100, -1), &[]).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_left_smaller_than_right_with_same_start() {
        test_merge((0..100, 1), (0..200, 1), &[(0..100, 2), (100..200, 1)]).await;
        test_merge((0..100, 1), (0..200, -1), &[(100..200, -1)]).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_right_smaller_than_left_with_same_start() {
        test_merge((0..200, 1), (0..100, 1), &[(0..100, 2), (100..200, 1)]).await;
        test_merge((0..200, 1), (0..100, -1), &[(100..200, 1)]).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_left_starts_before_right_with_overlap() {
        test_merge((0..200, 1), (100..150, 1), &[(0..100, 1), (100..150, 2), (150..200, 1)]).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_preserves_sequences() {
        let tree = LSMTree::new(merge);
        tree.insert(Item {
            key: AllocatorKey { device_range: 0..100 },
            value: AllocatorValue { delta: 1 },
            sequence: 1u64,
        })
        .await;
        tree.seal().await;
        tree.insert(Item {
            key: AllocatorKey { device_range: 25..50 },
            value: AllocatorValue { delta: -1 },
            sequence: 2u64,
        })
        .await;
        tree.insert(Item {
            key: AllocatorKey { device_range: 75..100 },
            value: AllocatorValue { delta: 1 },
            sequence: 3u64,
        })
        .await;
        let layer_set = tree.layer_set();
        let mut merger = layer_set.merger();
        let mut iter = merger.seek(Bound::Unbounded).await.expect("seek failed");
        assert_eq!(iter.get().unwrap().key, &AllocatorKey { device_range: 0..25 });
        assert_eq!(iter.get().unwrap().value, &AllocatorValue { delta: 1 });
        assert_eq!(iter.get().unwrap().sequence, 1u64);
        iter.advance().await.expect("advance failed");
        assert_eq!(iter.get().unwrap().key, &AllocatorKey { device_range: 50..75 });
        assert_eq!(iter.get().unwrap().value, &AllocatorValue { delta: 1 });
        assert_eq!(iter.get().unwrap().sequence, 1u64);
        iter.advance().await.expect("advance failed");
        assert_eq!(iter.get().unwrap().key, &AllocatorKey { device_range: 75..100 });
        assert_eq!(iter.get().unwrap().value, &AllocatorValue { delta: 2 });
        assert_eq!(iter.get().unwrap().sequence, 1u64);
        iter.advance().await.expect("advance failed");
        assert!(iter.get().is_none());
    }
}
