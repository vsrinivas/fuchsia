use crate::lsm_tree::{
    merge::{MergeIterator, MergeResult},
    Item, ItemRef,
};
use crate::object_store::allocator::{
    AllocatorKey, AllocatorValue,
    AllocatorValue::{Delete, Insert},
};

fn split_left(
    item: ItemRef<'_, AllocatorKey, AllocatorValue>,
    position: u64,
) -> MergeResult<AllocatorKey, AllocatorValue> {
    let mut key1 = item.key.clone();
    key1.device_range.end = position;
    let mut key2 = item.key.clone();
    key2.device_range.start = position;
    return MergeResult::EmitPartLeftWithRemainder(
        Item { key: key1, value: (*item.value).clone() },
        Item { key: key2, value: (*item.value).clone() },
    );
}

pub fn merge(
    left: &MergeIterator<'_, AllocatorKey, AllocatorValue>,
    right: &MergeIterator<'_, AllocatorKey, AllocatorValue>,
) -> MergeResult<AllocatorKey, AllocatorValue> {
    // println!("merging {:?} {:?}", left.key(), right.key());
    // No overlap?
    if left.key().device_range.end <= right.key().device_range.start {
        return MergeResult::EmitLeft;
    }
    // Start not equal?
    if left.key().device_range.start < right.key().device_range.start {
        return split_left(left.item(), right.key().device_range.start);
    }
    // Different extents?
    if left.key().object_id != right.key().object_id
        || left.key().attribute_id != right.key().attribute_id
        || left.key().file_offset != right.key().file_offset
    {
        // The keys don't overlap, but we still might need to split the left key
        // at right.device_range.end.
        if left.key().device_range.end > right.key().device_range.end {
            return split_left(left.item(), right.key().device_range.end);
        } else {
            return MergeResult::EmitLeft;
        }
    }
    // At this point the records are mergeable. The only difference in the keys
    // will be in device_range.end. left.device_range.end <= right.device_range.end.
    if left.layer < right.layer {
        // Left wins.
        match (left.value(), right.value()) {
            (Delete, Delete) => {
                // This is impossible. You can't delete an extent twice.
                panic!("Two deletes!"); // TODO
            }
            (Insert, Delete) | (Insert, Insert) => {
                // This is just re-using an extent that we previously deleted. There might be a
                // remainder for the right record.
                if left.key().device_range.end < right.key().device_range.end {
                    let mut key = right.key().clone();
                    key.device_range.start = left.key().device_range.end;
                    MergeResult::DiscardPartRightWithRemainder(Item {
                        key,
                        value: right.value().clone(),
                    })
                } else {
                    MergeResult::DiscardRight
                }
            }
            (Delete, Insert) => {
                if left.key().device_range.end < right.key().device_range.end {
                    let mut key = right.key().clone();
                    key.device_range.start = left.key().device_range.end;
                    MergeResult::DiscardLeftAndPartRightWithRemainder(Item {
                        key,
                        value: right.value().clone(),
                    })
                } else {
                    MergeResult::DiscardBoth
                }
            }
        }
    } else {
        // It can't be equal because if it were, layer would be the only difference and left.layer
        // would *have* to be < right.layer.
        assert!(left.key().device_range.end < right.key().device_range.end);
        let mut key = right.key().clone();
        key.device_range.start = left.key().device_range.end;
        // Right wins.
        match (left.value(), right.value()) {
            (Delete, Delete) => {
                // This is impossible. You can't delete an extent twice.
                panic!("Two deletes!"); // TODO
            }
            (Insert, Delete) => MergeResult::DiscardLeftAndPartRightWithRemainder(Item {
                key,
                value: right.value().clone(),
            }),
            (Delete, Insert) | (Insert, Insert) => MergeResult::DiscardLeft,
        }
    }
}
