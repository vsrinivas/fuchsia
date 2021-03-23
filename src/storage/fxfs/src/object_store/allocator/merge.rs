// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    lsm_tree::{
        merge::{
            ItemOp::{Discard, Keep, Replace},
            MergeIterator, MergeResult,
        },
        types::Item,
    },
    object_store::allocator::{AllocatorKey, AllocatorValue},
};

pub fn merge(
    left: &MergeIterator<'_, AllocatorKey, AllocatorValue>,
    right: &MergeIterator<'_, AllocatorKey, AllocatorValue>,
) -> MergeResult<AllocatorKey, AllocatorValue> {
    // Wherever Replace is used below, it must not extend the *end* of the range for whichever item
    // is returned i.e. if replacing the left item, replacement.end <= left.end because otherwise we
    // might not merge records that come after that end point because the merger won't merge records
    // in the same layer.

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
            right: Replace(Item::new(
                AllocatorKey {
                    device_range: left.key().device_range.start..right.key().device_range.end,
                },
                AllocatorValue { delta: left.value().delta },
            )),
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
                    Replace(Item::new(
                        left.key().clone(),
                        AllocatorValue { delta: left.value().delta + right.value().delta },
                    ))
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
                    Replace(Item::new(
                        left.key().clone(),
                        AllocatorValue { delta: left.value().delta + right.value().delta },
                    ))
                },
                right: Replace(Item::new(
                    AllocatorKey {
                        device_range: left.key().device_range.end..right.key().device_range.end,
                    },
                    AllocatorValue { delta: right.value().delta },
                )),
            };
        }
        /*  Case 5:
         *    L:    |-------------------|
         *    R:    |------------|
         */
        return MergeResult::Other {
            emit: None,
            left: Replace(Item::new(
                AllocatorKey {
                    device_range: right.key().device_range.end..left.key().device_range.end,
                },
                AllocatorValue { delta: left.value().delta },
            )),
            right: if left.value().delta + right.value().delta == 0 {
                Discard
            } else {
                Replace(Item::new(
                    right.key().clone(),
                    AllocatorValue { delta: left.value().delta + right.value().delta },
                ))
            },
        };
    }
    /*  Case 6:
     *    L:    |-----...
     *    R:         |-----...
     */
    MergeResult::Other {
        emit: Some(Item::new(
            AllocatorKey {
                device_range: left.key().device_range.start..right.key().device_range.start,
            },
            AllocatorValue { delta: left.value().delta },
        )),
        left: if right.key().device_range.start == left.key().device_range.end {
            Discard
        } else {
            Replace(Item::new(
                AllocatorKey {
                    device_range: right.key().device_range.start..left.key().device_range.end,
                },
                left.value().clone(),
            ))
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
        tree.seal();
        tree.insert(Item::new(
            AllocatorKey { device_range: left.0 },
            AllocatorValue { delta: left.1 },
        ))
        .await;
        let layer_set = tree.layer_set();
        let mut iter = layer_set.seek(Bound::Unbounded).await.expect("seek failed");
        for e in expected {
            let ItemRef { key, value } = iter.get().expect("get failed");
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
}
