// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{utils::format_parts, *},
    base64,
    failure::Error,
    fuchsia_inspect::reader::{ArrayBucket, ArrayValue, NodeHierarchy, Property},
    maplit::hashmap,
    serde::ser::{Serialize, SerializeMap, SerializeSeq, SerializeStruct, Serializer},
    serde_json::{
        json,
        ser::{PrettyFormatter, Serializer as JsonSerializer},
    },
    std::str::from_utf8,
};

/// A JSON formatter for an inspect node hierarchy.
pub struct JsonFormatter {}

impl HierarchyFormatter for JsonFormatter {
    fn format(hierarchy: HierarchyData) -> Result<String, Error> {
        let mut bytes = Vec::new();
        let mut serializer =
            JsonSerializer::with_formatter(&mut bytes, PrettyFormatter::with_indent(b"    "));
        json!(SerializableHierarchy { hierarchy_data: hierarchy }).serialize(&mut serializer)?;
        Ok(from_utf8(&bytes)?.to_string())
    }

    fn format_multiple(hierarchies: Vec<HierarchyData>) -> Result<String, Error> {
        let values = hierarchies
            .into_iter()
            .map(|hierarchy_data| SerializableHierarchy { hierarchy_data })
            .collect::<Vec<SerializableHierarchy>>();
        let mut bytes = Vec::new();
        let mut serializer =
            JsonSerializer::with_formatter(&mut bytes, PrettyFormatter::with_indent(b"    "));
        json!(values).serialize(&mut serializer)?;
        Ok(from_utf8(&bytes)?.to_string())
    }
}

// The following wrapping structs are used to implement Serialize on them given
// that it's not possible to implement traits for structs in other crates.
struct SerializableHierarchy {
    hierarchy_data: HierarchyData,
}

struct WrappedNodeHierarchy<'a> {
    hierarchy: &'a NodeHierarchy,
}

struct WrappedArrayValue<'a, T> {
    array: &'a ArrayValue<T>,
}

struct WrappedArrayBucket<'a, T> {
    bucket: &'a ArrayBucket<T>,
}

impl Serialize for SerializableHierarchy {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        let mut s = serializer.serialize_struct("NodeHierarchy", 2)?;
        let path = format_parts(&self.hierarchy_data.file_path, &self.hierarchy_data.fields);
        s.serialize_field("path", &path)?;
        let name = self.hierarchy_data.hierarchy.name.clone();
        let contents =
            hashmap!(name => WrappedNodeHierarchy { hierarchy: &self.hierarchy_data.hierarchy });
        s.serialize_field("contents", &contents)?;
        s.end()
    }
}

impl<'a> Serialize for WrappedNodeHierarchy<'a> {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        let items = self.hierarchy.properties.len() + self.hierarchy.children.len();
        let mut s = serializer.serialize_map(Some(items))?;
        for property in self.hierarchy.properties.iter() {
            let _ = match property {
                Property::String(name, value) => s.serialize_entry(&name, &value)?,
                Property::Int(name, value) => s.serialize_entry(&name, &value)?,
                Property::Uint(name, value) => s.serialize_entry(&name, &value)?,
                Property::Double(name, value) => s.serialize_entry(&name, &value)?,
                Property::Bytes(name, array) => {
                    s.serialize_entry(&name, &format!("b64:{}", base64::encode(&array)))?
                }
                Property::DoubleArray(name, array) => {
                    let wrapped_value = WrappedArrayValue { array };
                    s.serialize_entry(&name, &wrapped_value)?;
                }
                Property::IntArray(name, array) => {
                    let wrapped_value = WrappedArrayValue { array };
                    s.serialize_entry(&name, &wrapped_value)?;
                }
                Property::UintArray(name, array) => {
                    let wrapped_value = WrappedArrayValue { array };
                    s.serialize_entry(&name, &wrapped_value)?;
                }
            };
        }
        for child in self.hierarchy.children.iter() {
            s.serialize_entry(&child.name, &WrappedNodeHierarchy { hierarchy: child })?;
        }
        s.end()
    }
}

macro_rules! impl_serialize_for_array_value {
    ($($type:ty,)*) => {
        $(
            impl<'a> Serialize for WrappedArrayValue<'a, $type> {
                fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
                    match self.array.buckets() {
                        Some(buckets) => {
                            let mut s = serializer.serialize_map(Some(1))?;
                            let wrapped_buckets = buckets
                                .iter()
                                .map(|bucket| WrappedArrayBucket { bucket })
                                .collect::<Vec<WrappedArrayBucket<'_, $type>>>();
                            s.serialize_entry("buckets", &wrapped_buckets)?;
                            s.end()
                        }
                        None => {
                            let mut s = serializer.serialize_seq(Some(self.array.values.len()))?;
                            for value in self.array.values.iter() {
                                s.serialize_element(&value)?;
                            }
                            s.end()
                        }
                    }
                }
            }
        )*
    }
}

macro_rules! impl_serialize_for_array_bucket {
    ($($type:ty,)*) => {
        $(
            impl<'a> Serialize for WrappedArrayBucket<'a, $type> {
                fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
                    let mut s = serializer.serialize_map(Some(3))?;
                    s.serialize_entry("floor", &self.bucket.floor)?;
                    s.serialize_entry("upper_bound", &self.bucket.upper)?;
                    s.serialize_entry("count", &self.bucket.count)?;
                    s.end()
                }
            }
        )*
    }
}

impl_serialize_for_array_value![i64, u64, f64,];
impl_serialize_for_array_bucket![i64, u64,];

impl<'a> Serialize for WrappedArrayBucket<'a, f64> {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        let mut s = serializer.serialize_map(Some(3))?;
        let parts = [
            ("floor", self.bucket.floor),
            ("upper_bound", self.bucket.upper),
            ("count", self.bucket.count),
        ];
        for (entry_key, value) in parts.iter() {
            if *value == std::f64::MAX || *value == std::f64::INFINITY {
                s.serialize_entry(entry_key, "Infinity")?;
            } else if *value == std::f64::MIN || *value == std::f64::NEG_INFINITY {
                s.serialize_entry(entry_key, "-Infinity")?;
            } else {
                s.serialize_entry(entry_key, value)?;
            }
        }
        s.end()
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_inspect::reader::ArrayFormat};

    #[test]
    fn format_json() {
        let hierarchy =
            NodeHierarchy::new("root", vec![Property::Double("double".to_string(), 2.5)], vec![]);
        let data = HierarchyData {
            hierarchy,
            file_path: "/some/path/out/objects/root.inspect".to_string(),
            fields: vec![],
        };
        let result = JsonFormatter::format(data).expect("failed to format hierarchy");
        let expected = "{
    \"contents\": {
        \"root\": {
            \"double\": 2.5
        }
    },
    \"path\": \"/some/path/out/objects/root.inspect\"
}";
        assert_eq!(result, expected);
    }

    #[test]
    fn format_json_multiple() -> Result<(), Error> {
        let (a, b) = get_hierarchies();
        let datas = vec![
            HierarchyData {
                hierarchy: a,
                file_path: "/some/path/out/objects/root.inspect".to_string(),
                fields: vec![],
            },
            HierarchyData {
                hierarchy: b,
                file_path: "/other/path/out/objects".to_string(),
                fields: vec!["root".to_string(), "x".to_string(), "y".to_string()],
            },
        ];
        let result = JsonFormatter::format_multiple(datas).expect("failed to format hierarchies");
        assert_eq!(get_expected_multi_json(), result);
        Ok(())
    }

    fn get_hierarchies() -> (NodeHierarchy, NodeHierarchy) {
        (
            NodeHierarchy::new(
                "root",
                vec![Property::UintArray(
                    "array".to_string(),
                    ArrayValue::new(vec![0, 2, 4], ArrayFormat::Default),
                )],
                vec![
                    NodeHierarchy::new(
                        "a",
                        vec![
                            Property::Double("double".to_string(), 2.5),
                            Property::DoubleArray(
                                "histogram".to_string(),
                                ArrayValue::new(
                                    vec![0.0, 2.0, 4.0, 1.0, 3.0, 4.0],
                                    ArrayFormat::ExponentialHistogram,
                                ),
                            ),
                        ],
                        vec![],
                    ),
                    NodeHierarchy::new(
                        "b",
                        vec![
                            Property::Int("int".to_string(), -2),
                            Property::String("string".to_string(), "some value".to_string()),
                            Property::IntArray(
                                "histogram".to_string(),
                                ArrayValue::new(vec![0, 2, 4, 1, 3], ArrayFormat::LinearHistogram),
                            ),
                        ],
                        vec![],
                    ),
                ],
            ),
            NodeHierarchy::new(
                "y",
                vec![Property::Bytes("bytes".to_string(), vec![5u8, 0xf1, 0xab])],
                vec![],
            ),
        )
    }

    fn get_expected_multi_json() -> String {
        "[
    {
        \"contents\": {
            \"root\": {
                \"a\": {
                    \"double\": 2.5,
                    \"histogram\": {
                        \"buckets\": [
                            {
                                \"count\": 1.0,
                                \"floor\": \"-Infinity\",
                                \"upper_bound\": 0.0
                            },
                            {
                                \"count\": 3.0,
                                \"floor\": 0.0,
                                \"upper_bound\": 2.0
                            },
                            {
                                \"count\": 4.0,
                                \"floor\": 2.0,
                                \"upper_bound\": \"Infinity\"
                            }
                        ]
                    }
                },
                \"array\": [
                    0,
                    2,
                    4
                ],
                \"b\": {
                    \"histogram\": {
                        \"buckets\": [
                            {
                                \"count\": 4,
                                \"floor\": -9223372036854775808,
                                \"upper_bound\": 0
                            },
                            {
                                \"count\": 1,
                                \"floor\": 0,
                                \"upper_bound\": 2
                            },
                            {
                                \"count\": 3,
                                \"floor\": 2,
                                \"upper_bound\": 9223372036854775807
                            }
                        ]
                    },
                    \"int\": -2,
                    \"string\": \"some value\"
                }
            }
        },
        \"path\": \"/some/path/out/objects/root.inspect\"
    },
    {
        \"contents\": {
            \"y\": {
                \"bytes\": \"b64:BfGr\"
            }
        },
        \"path\": \"/other/path/out/objects#x/y\"
    }
]"
        .to_string()
    }
}
