use super::record::{ExtentKey, ExtentValue, ObjectItem, ObjectKey, ObjectKeyData, ObjectValue};
use crate::lsm_tree::merge::{MergeIterator, MergeResult};

fn merge_extents<'a>(
    object_id: u64,
    left_layer: u16,
    right_layer: u16,
    left_key: &ExtentKey,
    right_key: &ExtentKey,
    left_value: &ExtentValue,
    right_value: &ExtentValue,
) -> MergeResult<ObjectKey, ObjectValue> {
    if left_key.attribute_id != right_key.attribute_id {
        return MergeResult::EmitLeft;
    }
    if left_key.range.end <= right_key.range.start {
        return MergeResult::EmitLeft;
    }
    if right_layer < left_layer {
        // TODO: What if left..start == right..start?
        return MergeResult::EmitPartLeftWithRemainder(
            ObjectItem {
                key: ObjectKey::extent(
                    object_id,
                    ExtentKey::new(
                        left_key.attribute_id,
                        left_key.range.start..right_key.range.start,
                    ),
                ),
                value: ObjectValue::Extent(*left_value),
            },
            ObjectItem {
                key: ObjectKey::extent(
                    object_id,
                    ExtentKey::new(
                        left_key.attribute_id,
                        right_key.range.start..left_key.range.end,
                    ),
                ),
                value: ObjectValue::extent(
                    left_value.device_offset + right_key.range.start - left_key.range.start,
                ),
            },
        );
    }
    if left_key.range.end >= right_key.range.end {
        return MergeResult::DiscardRight;
    }
    return MergeResult::DiscardPartRightWithRemainder(ObjectItem {
        key: ObjectKey::extent(
            object_id,
            ExtentKey::new(left_key.attribute_id, left_key.range.end..right_key.range.end),
        ),
        value: ObjectValue::extent(
            right_value.device_offset + left_key.range.end - right_key.range.start,
        ),
    });
}

#[allow(dead_code)]
pub fn merge(
    left: &MergeIterator<'_, ObjectKey, ObjectValue>,
    right: &MergeIterator<'_, ObjectKey, ObjectValue>,
) -> MergeResult<ObjectKey, ObjectValue> {
    if left.key().object_id == right.key().object_id {
        match (left.key(), right.key(), left.value(), right.value()) {
            (
                ObjectKey { object_id: _, data: ObjectKeyData::Extent(left_extent_key) },
                ObjectKey { object_id: _, data: ObjectKeyData::Extent(right_extent_key) },
                ObjectValue::Extent(left_extent),
                ObjectValue::Extent(right_extent),
            ) => {
                return merge_extents(
                    left.key().object_id,
                    left.layer,
                    right.layer,
                    left_extent_key,
                    right_extent_key,
                    left_extent,
                    right_extent,
                )
            }
            _ => {}
        }
    }
    return MergeResult::EmitLeft;
}
