// In-memory structures of records in the LSM tree.

use crate::lsm_tree::OrdLowerBound;
use crate::lsm_tree::{Item, ItemRef};
use serde::{Deserialize, Serialize};
use std::cmp::{max, min};
use std::ops::Range;

#[derive(Debug, Eq, PartialEq, Clone, Serialize, Deserialize)]
pub struct ExtentKey {
    pub attribute_id: u64,
    pub range: std::ops::Range<u64>,
}

impl ExtentKey {
    pub fn new(attribute_id: u64, range: std::ops::Range<u64>) -> Self {
        ExtentKey { attribute_id, range }
    }

    pub fn overlap(&self, other: &ExtentKey) -> Option<Range<u64>> {
        if self.attribute_id != other.attribute_id
            || self.range.end <= other.range.start
            || self.range.start >= other.range.end
        {
            None
        } else {
            Some(max(self.range.start, other.range.start)..min(self.range.end, other.range.end))
        }
    }

    pub fn lower_bound(&self) -> Self {
        ExtentKey { attribute_id: self.attribute_id, range: 0..self.range.start + 1 }
    }
}

// The normal comparison uses the end of the range before the start of the range. This makes
// searching for records easier because it's easy to find K.. (where K is the key you are searching
// for), but it's harder (with BTreeMap at least) to find the key immediately preceding K that then
// extends to the end i.e. K-1..  OrdLowerBound orders by the start of an extent.
impl Ord for ExtentKey {
    fn cmp(&self, other: &ExtentKey) -> std::cmp::Ordering {
        // The comparison uses the end of the range so that we can more easily do queries.
        // TODO(csuter): Expand on this.
        self.attribute_id
            .cmp(&other.attribute_id)
            .then(self.range.end.cmp(&other.range.end))
            .then(self.range.start.cmp(&other.range.start))
    }
}

impl PartialOrd for ExtentKey {
    fn partial_cmp(&self, other: &ExtentKey) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl OrdLowerBound for ExtentKey {
    // Orders by the start of the range rather than the end.
    fn cmp_lower_bound(&self, other: &ExtentKey) -> std::cmp::Ordering {
        return self
            .attribute_id
            .cmp(&other.attribute_id)
            .then(self.range.start.cmp(&other.range.start));
    }
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
pub struct ExtentValue {
    pub device_offset: u64,
}

#[derive(Clone, Debug, Eq, PartialEq, PartialOrd, Ord, Serialize, Deserialize)]
pub enum ObjectKeyData {
    Object,
    Attribute { attribute_id: u64 },
    Extent(ExtentKey),
    Child { name: String }, // Should this be a string or array of bytes?
}

#[derive(Debug, Eq, PartialEq, PartialOrd, Ord, Serialize, Deserialize, Clone)]
pub struct ObjectKey {
    pub object_id: u64,
    pub data: ObjectKeyData,
}

impl ObjectKey {
    pub fn object(object_id: u64) -> ObjectKey {
        ObjectKey { object_id: object_id, data: ObjectKeyData::Object }
    }

    pub fn attribute(object_id: u64, attribute_id: u64) -> ObjectKey {
        ObjectKey { object_id, data: ObjectKeyData::Attribute { attribute_id } }
    }

    pub fn extent(object_id: u64, extent_key: ExtentKey) -> ObjectKey {
        ObjectKey { object_id, data: ObjectKeyData::Extent(extent_key) }
    }

    pub fn child(object_id: u64, name: &str) -> ObjectKey {
        ObjectKey { object_id, data: ObjectKeyData::Child { name: name.to_owned() } }
    }

    // Returns the lower bound for a key.
    pub fn lower_bound(&self) -> ObjectKey {
        if let ObjectKey { object_id, data: ObjectKeyData::Extent(e) } = self {
            ObjectKey::extent(*object_id, e.lower_bound())
        } else {
            self.clone()
        }
    }
}

impl OrdLowerBound for ObjectKey {
    fn cmp_lower_bound(&self, other: &ObjectKey) -> std::cmp::Ordering {
        self.object_id.cmp(&other.object_id).then_with(|| match (&self.data, &other.data) {
            (ObjectKeyData::Extent(ref left_extent), ObjectKeyData::Extent(ref right_extent)) => {
                left_extent.cmp_lower_bound(right_extent)
            }
            _ => self.data.cmp(&other.data),
        })
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum ObjectType {
    File,
    Directory,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum ObjectValue {
    Object { object_type: ObjectType },
    Attribute { size: u64 },
    Extent(ExtentValue),
    Child { object_id: u64 },
}

impl ObjectValue {
    pub fn object(object_type: ObjectType) -> ObjectValue {
        ObjectValue::Object { object_type }
    }
    pub fn attribute(size: u64) -> ObjectValue {
        ObjectValue::Attribute { size }
    }
    pub fn extent(device_offset: u64) -> ObjectValue {
        ObjectValue::Extent(ExtentValue { device_offset })
    }

    pub fn child(object_id: u64) -> ObjectValue {
        ObjectValue::Child { object_id }
    }
}

pub type ObjectItem = Item<ObjectKey, ObjectValue>;

pub fn decode_extent(
    item: ItemRef<'_, ObjectKey, ObjectValue>,
) -> Option<(u64, &ExtentKey, &ExtentValue)> {
    match item {
        ItemRef {
            key: ObjectKey { object_id, data: ObjectKeyData::Extent(ref extent_key) },
            value: ObjectValue::Extent(ref extent_value),
        } => Some((*object_id, extent_key, extent_value)),
        _ => None,
    }
}

// TODO: need validation after deserialization.
