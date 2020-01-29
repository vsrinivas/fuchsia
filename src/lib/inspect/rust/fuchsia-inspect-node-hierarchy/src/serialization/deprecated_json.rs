// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::serialization::{json::SerializableHierarchyFields, utils::format_parts, *},
    anyhow::Error,
    serde::ser::{Serialize, SerializeMap, SerializeStruct, Serializer},
    serde_json::{
        json,
        ser::{PrettyFormatter, Serializer as JsonSerializer},
    },
    std::str::from_utf8,
};

/// A JSON formatter for an inspect node hierarchy.
/// NOTE: This is deprecated. It's currently used by the Archivist and iquery but we are migrating
/// away from it as we migrate to use the Unified Reader server. New clients should use
/// `JsonHierarchySerializer`.
// TODO(fxb/43112): remove
pub struct DeprecatedJsonFormatter {}

impl DeprecatedHierarchyFormatter for DeprecatedJsonFormatter {
    fn format(hierarchy: HierarchyData) -> Result<String, Error> {
        let mut bytes = Vec::new();
        let mut serializer =
            JsonSerializer::with_formatter(&mut bytes, PrettyFormatter::with_indent(b"    "));
        json!(SerializableHierarchyData { hierarchy_data: hierarchy })
            .serialize(&mut serializer)?;
        Ok(from_utf8(&bytes)?.to_string())
    }

    fn format_multiple(hierarchies: Vec<HierarchyData>) -> Result<String, Error> {
        let values = hierarchies
            .into_iter()
            .map(|hierarchy_data| SerializableHierarchyData { hierarchy_data })
            .collect::<Vec<SerializableHierarchyData>>();
        let mut bytes = Vec::new();
        let mut serializer =
            JsonSerializer::with_formatter(&mut bytes, PrettyFormatter::with_indent(b"    "));
        json!(values).serialize(&mut serializer)?;
        Ok(from_utf8(&bytes)?.to_string())
    }
}

pub struct SerializableHierarchyData {
    hierarchy_data: HierarchyData,
}

struct WrappedSerializableNodeHierarchy<'a> {
    hierarchy: &'a NodeHierarchy,
}

impl<'a> Serialize for WrappedSerializableNodeHierarchy<'a> {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        let mut s = serializer.serialize_map(Some(1))?;
        let name = self.hierarchy.name.clone();
        s.serialize_entry(&name, &SerializableHierarchyFields { hierarchy: &self.hierarchy })?;
        s.end()
    }
}

impl Serialize for SerializableHierarchyData {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        let mut s = serializer.serialize_struct("NodeHierarchy", 2)?;
        let path = format_parts(&self.hierarchy_data.file_path, &self.hierarchy_data.fields);
        s.serialize_field("path", &path)?;
        let hierarchy =
            WrappedSerializableNodeHierarchy { hierarchy: &self.hierarchy_data.hierarchy };
        s.serialize_field("contents", &hierarchy)?;
        s.end()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{ArrayFormat, ArrayValue, Property},
    };

    #[test]
    fn format_json() {
        let hierarchy =
            NodeHierarchy::new("root", vec![Property::Double("double".to_string(), 2.5)], vec![]);
        let data = HierarchyData {
            hierarchy,
            file_path: "/some/path/out/diagnostics/root.inspect".to_string(),
            fields: vec![],
        };
        let result = DeprecatedJsonFormatter::format(data).expect("failed to format hierarchy");
        let expected = "{
    \"contents\": {
        \"root\": {
            \"double\": 2.5
        }
    },
    \"path\": \"/some/path/out/diagnostics/root.inspect\"
}";
        assert_eq!(result, expected);
    }

    #[test]
    fn format_json_multiple() -> Result<(), Error> {
        let (a, b) = get_hierarchies();
        let datas = vec![
            HierarchyData {
                hierarchy: a,
                file_path: "/some/path/out/diagnostics/root.inspect".to_string(),
                fields: vec![],
            },
            HierarchyData {
                hierarchy: b,
                file_path: "/other/path/out/diagnostics".to_string(),
                fields: vec!["root".to_string(), "x".to_string(), "y".to_string()],
            },
        ];
        let result =
            DeprecatedJsonFormatter::format_multiple(datas).expect("failed to format hierarchies");
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
        \"path\": \"/some/path/out/diagnostics/root.inspect\"
    },
    {
        \"contents\": {
            \"y\": {
                \"bytes\": \"b64:BfGr\"
            }
        },
        \"path\": \"/other/path/out/diagnostics#x/y\"
    }
]"
        .to_string()
    }
}
