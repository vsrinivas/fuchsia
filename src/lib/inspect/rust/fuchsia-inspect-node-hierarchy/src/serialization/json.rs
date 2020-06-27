// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{serialization::HierarchyDeserializer, ArrayContent, Bucket, NodeHierarchy, Property},
    anyhow::{bail, format_err, Error},
    lazy_static::lazy_static,
    paste,
    serde_json::{self, Map, Value},
    std::{fmt::Debug, str::FromStr},
};

/// Allows to serialize a `NodeHierarchy` into a Serde JSON Value.
pub struct JsonNodeHierarchySerializer {}

/// Allows to serialize a `NodeHierarchy` into a Serde JSON Value.
pub struct RawJsonNodeHierarchySerializer {}

/// Implements deserialization of a `NodeHierarchy` from a String.
impl<Key> HierarchyDeserializer<Key> for JsonNodeHierarchySerializer
where
    Key: FromStr + Debug + AsRef<str>,
    Error: From<<Key as FromStr>::Err>,
{
    // The Json Formatter deserializes JSON Strings encoding a single node hierarchy.
    type Object = String;

    // TODO(4601): Should this be parsing the outer schema, which includes the envelope of
    // moniker and metadata? Right now it assumes its receiving only the payload as the string.
    fn deserialize(data_format: String) -> Result<NodeHierarchy<Key>, Error> {
        let root_node: serde_json::Value = serde_json::from_str(&data_format)?;
        deserialize_json(root_node)
    }
}

/// Implements deserialization of a `NodeHierarchy` from a Serde JSON Value.
impl<Key> HierarchyDeserializer<Key> for RawJsonNodeHierarchySerializer
where
    Key: FromStr + Debug + AsRef<str>,
    Error: From<<Key as FromStr>::Err>,
{
    // The Json Formatter deserializes JSON Strings encoding a single node hierarchy.
    type Object = serde_json::Value;

    fn deserialize(data: serde_json::Value) -> Result<NodeHierarchy<Key>, Error> {
        deserialize_json(data)
    }
}

/// Converts a JSON object encoding a NodeHierarchy into that NodeHierarchy. Expects
/// that the top-level serde_value is an object which is convertable into a NodeHierarchy node.:
/// eg:  "{
///          'root': {...}
///       }"
///
// TODO(43030): Remove explicit root nodes from serialized diagnostics data.
fn deserialize_json<Key>(root_node: serde_json::Value) -> Result<NodeHierarchy<Key>, Error>
where
    Key: FromStr + Debug + AsRef<str>,
    Error: From<<Key as FromStr>::Err>,
{
    match root_node {
        serde_json::Value::Object(v) => {
            if v.len() > 1 {
                return Err(format_err!("We expect there to be only one root to the tree."));
            }
            let (name, value) = v.iter().next().unwrap();

            parse_node_object(
                &name,
                value
                    .as_object()
                    .ok_or(format_err!("The first `value` in the tree must be a node."))?,
            )
        }
        _ => return Err(format_err!("The first entry in a NodeHierarchy Json must be a node.")),
    }
}

lazy_static! {
    static ref HISTOGRAM_OBJECT_KEY: String = "buckets".to_string();
    static ref COUNT_KEY: String = "count".to_string();
    static ref FLOOR_KEY: String = "floor".to_string();
    static ref UPPER_BOUND_KEY: String = "upper_bound".to_string();
    static ref REQUIRED_BUCKET_KEYS: Vec<&'static String> =
        vec![&*COUNT_KEY, &*FLOOR_KEY, &*UPPER_BOUND_KEY];
}

macro_rules! check_array_type_fns {
    ($($type:ty),*) => {
        $(
            paste::item! {
                fn [<get_histogram_ $type _key>](
                    object: &serde_json::Map<String, serde_json::Value>, key: &str
                ) -> Result<$type, Error> {
                    object.get(key)
                        .map(|value| sanitize_value(value))
                        .and_then(|value| {
                            if value.[<is_ $type>]() {
                                value.[<as_ $type>]()
                            } else {
                                None
                            }
                        })
                        .ok_or(format_err!("All buckets should have a `{}`", key))
                }

                /// Converts a vector of serde Object values into a vector of Buckets.
                fn [<parse_ $type _buckets>](vec: &Vec<serde_json::Value>)
                    -> Result<Vec<Bucket<$type>>, Error>
                {
                    vec.iter().map(|value| {
                        let object = value.as_object().ok_or(
                            format_err!("Each bucket should be an object"))?;
                        if object.len() != 3 {
                            return Err(format_err!("Each bucket should have 3 entries"))?;
                        }
                        let count = [<get_histogram_ $type _key>](object, &*COUNT_KEY)?;
                        let floor = [<get_histogram_ $type _key>](object, &*FLOOR_KEY)?;
                        let upper = [<get_histogram_ $type _key>](object, &*UPPER_BOUND_KEY)?;
                        Ok(Bucket { floor, count, upper })
                    })
                    .collect::<Result<Vec<_>, Error>>()
                }

                /// Converts a vector of serde Number values into a vector of $type
                /// numerical values.
                fn [<parse_ $type _vec>](vec: &Vec<serde_json::Value>) -> Result<Vec<$type>, Error>
                {
                    vec.iter().map(|serde_num| {
                        if serde_num.[<is_ $type>]() {
                            serde_num.[<as_ $type>]()
                        } else {
                            None
                        }
                    })
                    .map(|maybe_num| maybe_num.ok_or(
                        format_err!("All types in an inspect array must be mapable \
                                    to the same data type.")))
                    .collect::<Result<Vec<_>, Error>>()
                }
            }
        )*
    };
}

// Generates the following methods:
// fn parse_f64_buckets()
// fn parse_f64_vec()
// fn parse_u64_buckets()
// fn parse_u64_vec()
// fn parse_i64_buckets()
// fn parse_i64_vec()
check_array_type_fns!(f64, u64, i64);

fn is_negative_infinity(val: &serde_json::Value) -> bool {
    val.is_string() && (val.as_str().unwrap() == "-Infinity")
}

fn is_infinity(val: &serde_json::Value) -> bool {
    val.is_string() && (val.as_str().unwrap() == "Infinity")
}

fn create_float_min_value() -> serde_json::Value {
    serde_json::to_value(std::f64::MIN).unwrap()
}

fn create_float_max_value() -> serde_json::Value {
    serde_json::to_value(std::f64::MAX).unwrap()
}

/// Takes what is assumed to be one of either a numerical serde_json value OR
/// a custom JSON encoding representing negative or positive infinty, and converts the
/// infinity values into float minimum or float maximum values, leaving the others
/// the same.
///
/// This is done to simplify the logic for verifying the consistency of numerical types
/// in deserialized objects. We shan't be concerned about the transformation of these
/// inifinite values in histograms since they only exist as overflow and underflow boundaries
/// which don't appear in the deserialized representation.
fn sanitize_value(val: &serde_json::Value) -> serde_json::Value {
    if is_infinity(val) {
        return create_float_max_value();
    }

    if is_negative_infinity(val) {
        return create_float_min_value();
    }

    val.clone()
}

/// Pattern matches on the expected structure of a Histogram serialization, and returns
/// the underlying bucket vector if it matches, otherwise returns None.
fn fetch_object_histogram(contents: &Map<String, Value>) -> Option<&Vec<serde_json::Value>> {
    if contents.len() != 1 {
        return None;
    }

    if let Some(serde_json::Value::Array(vec)) = contents.get(&*HISTOGRAM_OBJECT_KEY) {
        if let Some(serde_json::Value::Object(obj)) = vec.first() {
            let obj_is_histogram_bucket = REQUIRED_BUCKET_KEYS.iter().all(|k| obj.contains_key(*k));
            if obj_is_histogram_bucket {
                return Some(vec);
            }
        }
    }
    None
}

/// Parses a JSON representation of an Inspect histogram into its Array form.
fn parse_histogram<Key>(
    name: Key,
    histogram: &Vec<serde_json::Value>,
) -> Result<Property<Key>, Error>
where
    Key: Debug,
{
    if histogram.len() < 3 {
        bail!(
            "Histograms require a minimum of 5 entries to be validly serialized. We can only
uce 5 entries if there exist 3 or more buckets. If this isn't the case, this histogram is
ormed."
        );
    }

    if let Ok(buckets) = parse_f64_buckets(histogram) {
        return Ok(Property::DoubleArray(name, ArrayContent::Buckets(buckets)));
    }

    if let Ok(buckets) = parse_i64_buckets(histogram) {
        return Ok(Property::IntArray(name, ArrayContent::Buckets(buckets)));
    }

    if let Ok(buckets) = parse_u64_buckets(histogram) {
        return Ok(Property::UintArray(name, ArrayContent::Buckets(buckets)));
    }

    return Err(format_err!(
        "Histograms must be one of i64, u64, or f64. Property name: {:?}",
        name
    ));
}

// Parses a JSON array into its numerical Inspect Array.
fn parse_array<Key>(name: Key, vec: &Vec<serde_json::Value>) -> Result<Property<Key>, Error>
where
    Key: Debug,
{
    if let Ok(values) = parse_f64_vec(vec) {
        return Ok(Property::DoubleArray(name, ArrayContent::Values(values)));
    }

    if let Ok(values) = parse_i64_vec(vec) {
        return Ok(Property::IntArray(name, ArrayContent::Values(values)));
    }

    if let Ok(values) = parse_u64_vec(vec) {
        return Ok(Property::UintArray(name, ArrayContent::Values(values)));
    }

    return Err(format_err!("Arrays must be one of i64, u64, or f64. Property name: {:?}", name));
}

/// Parses a serde_json Number into an Inspect number Property.
fn parse_number<Key>(name: Key, num: &serde_json::Number) -> Result<Property<Key>, Error>
where
    Key: Debug,
{
    if num.is_i64() {
        Ok(Property::Int(name, num.as_i64().unwrap()))
    } else if num.is_u64() {
        Ok(Property::Uint(name, num.as_u64().unwrap()))
    } else if num.is_f64() {
        Ok(Property::Double(name, num.as_f64().unwrap()))
    } else {
        return Err(format_err!("Diagnostics numbers must fit within 64 bits."));
    }
}

/// Creates a NodeHierarchy from a serde_json Object map, evaluating each of
/// the entries in the map and parsing them into their relevant Inspect in-memory
/// representation.
fn parse_node_object<Key>(
    node_name: &String,
    contents: &Map<String, Value>,
) -> Result<NodeHierarchy<Key>, Error>
where
    Key: FromStr + Debug + AsRef<str>,
    Error: From<<Key as FromStr>::Err>,
{
    let mut properties: Vec<Property<Key>> = Vec::new();
    let mut children: Vec<NodeHierarchy<Key>> = Vec::new();
    for (name, value) in contents.iter() {
        match value {
            serde_json::Value::Object(obj) => match fetch_object_histogram(obj) {
                Some(histogram_vec) => {
                    properties.push(parse_histogram(Key::from_str(name.as_str())?, histogram_vec)?);
                }
                None => {
                    let child_node = parse_node_object(name, obj)?;
                    children.push(child_node);
                }
            },
            serde_json::Value::Bool(val) => {
                properties.push(Property::Bool(Key::from_str(name.as_str())?, *val));
            }
            serde_json::Value::Number(num) => {
                properties.push(parse_number(Key::from_str(name.as_str())?, num)?);
            }
            serde_json::Value::String(string) => {
                let string_property =
                    Property::String(Key::from_str(name.as_str())?, string.to_string());
                properties.push(string_property);
            }
            serde_json::Value::Array(vec) => {
                properties.push(parse_array(Key::from_str(name.as_str())?, vec)?);
            }
            serde_json::Value::Null => {
                return Err(format_err!("Null isn't an existing part of the diagnostics schema."));
            }
        }
    }

    Ok(NodeHierarchy::new(node_name, properties, children))
}

#[cfg(test)]
mod tests {
    use {super::*, crate::ArrayFormat};

    #[test]
    fn serialize_json() {
        let mut hierarchy = test_hierarchy();
        hierarchy.sort();
        let expected = expected_json();
        let result = serde_json::to_string_pretty(&hierarchy).expect("failed to serialize");
        assert_eq!(result, expected);
    }

    #[test]
    fn deserialize_json() -> Result<(), Error> {
        let json_string = get_single_json_hierarchy();
        let mut parsed_hierarchy = JsonNodeHierarchySerializer::deserialize(json_string)?;
        let mut expected_hierarchy = get_unambigious_deserializable_hierarchy();
        parsed_hierarchy.sort();
        expected_hierarchy.sort();
        assert_eq!(expected_hierarchy, parsed_hierarchy);
        Ok(())
    }

    #[test]
    fn deserialize_json_raw() -> Result<(), Error> {
        let json_string = get_single_json_hierarchy();
        let json_value: serde_json::Value = serde_json::from_str(&json_string)?;
        let mut parsed_hierarchy = RawJsonNodeHierarchySerializer::deserialize(json_value)?;
        let mut expected_hierarchy = get_unambigious_deserializable_hierarchy();
        parsed_hierarchy.sort();
        expected_hierarchy.sort();
        assert_eq!(expected_hierarchy, parsed_hierarchy);
        Ok(())
    }

    #[test]
    fn reversible_deserialize() -> Result<(), Error> {
        let mut original_hierarchy = get_unambigious_deserializable_hierarchy();
        let result =
            serde_json::to_string(&original_hierarchy).expect("failed to format hierarchy");
        let mut parsed_hierarchy = JsonNodeHierarchySerializer::deserialize(result)?;
        parsed_hierarchy.sort();
        original_hierarchy.sort();
        assert_eq!(original_hierarchy, parsed_hierarchy);
        Ok(())
    }

    #[test]
    fn test_exp_histogram() -> Result<(), Error> {
        let mut hierarchy = NodeHierarchy::new(
            "root".to_string(),
            vec![Property::UintArray(
                "histogram".to_string(),
                ArrayContent::new(
                    vec![1000, 1000, 2, 1, 2, 3, 4, 5, 6],
                    ArrayFormat::ExponentialHistogram,
                )
                .unwrap(),
            )],
            vec![],
        );
        let expected_json = serde_json::json!({
            "root": {
                "histogram": {
                    "buckets": [
                        {"count": 1, "floor": 0, "upper_bound": 1000},
                        {"count": 2, "floor": 1000, "upper_bound": 2000},
                        {"count": 3, "floor": 2000, "upper_bound": 3000},
                        {"count": 4, "floor": 3000, "upper_bound": 5000},
                        {"count": 5, "floor": 5000, "upper_bound": 9000},
                        {"count": 6, "floor": 9000, "upper_bound": u64::MAX}
                    ]
                }
            }
        });
        let result_json = serde_json::json!(hierarchy);
        assert_eq!(result_json, expected_json);
        let mut parsed_hierarchy = RawJsonNodeHierarchySerializer::deserialize(result_json)?;
        parsed_hierarchy.sort();
        hierarchy.sort();
        assert_eq!(hierarchy, parsed_hierarchy);
        Ok(())
    }

    // Creates a hierarchy that isn't lossy due to its unambigious values.
    fn get_unambigious_deserializable_hierarchy() -> NodeHierarchy {
        NodeHierarchy::new(
            "root",
            vec![
                Property::UintArray(
                    "array".to_string(),
                    ArrayContent::Values(vec![0, 2, std::u64::MAX]),
                ),
                Property::Bool("bool_true".to_string(), true),
                Property::Bool("bool_false".to_string(), false),
            ],
            vec![
                NodeHierarchy::new(
                    "a",
                    vec![
                        Property::Double("double".to_string(), 2.5),
                        Property::DoubleArray(
                            "histogram".to_string(),
                            ArrayContent::new(
                                vec![0.0, 2.0, 4.0, 1.0, 3.0, 4.0, 7.0],
                                ArrayFormat::ExponentialHistogram,
                            )
                            .unwrap(),
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
                            ArrayContent::new(vec![0, 2, 4, 1, 3], ArrayFormat::LinearHistogram)
                                .unwrap(),
                        ),
                    ],
                    vec![],
                ),
            ],
        )
    }

    fn test_hierarchy() -> NodeHierarchy {
        NodeHierarchy::new(
            "root",
            vec![
                Property::UintArray("array".to_string(), ArrayContent::Values(vec![0, 2, 4])),
                Property::Bool("bool_true".to_string(), true),
                Property::Bool("bool_false".to_string(), false),
            ],
            vec![
                NodeHierarchy::new(
                    "a",
                    vec![
                        Property::Double("double".to_string(), 2.5),
                        Property::DoubleArray(
                            "histogram".to_string(),
                            ArrayContent::new(
                                vec![0.0, 2.0, 4.0, 1.0, 3.0, 4.0],
                                ArrayFormat::ExponentialHistogram,
                            )
                            .unwrap(),
                        ),
                        Property::Bytes("bytes".to_string(), vec![5u8, 0xf1, 0xab]),
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
                            ArrayContent::new(vec![0, 2, 4, 1, 3], ArrayFormat::LinearHistogram)
                                .unwrap(),
                        ),
                    ],
                    vec![],
                ),
            ],
        )
    }

    fn expected_json() -> String {
        r#"{
  "root": {
    "array": [
      0,
      2,
      4
    ],
    "bool_false": false,
    "bool_true": true,
    "a": {
      "bytes": "b64:BfGr",
      "double": 2.5,
      "histogram": {
        "buckets": [
          {
            "count": 1.0,
            "floor": "-Infinity",
            "upper_bound": 0.0
          },
          {
            "count": 3.0,
            "floor": 0.0,
            "upper_bound": 2.0
          },
          {
            "count": 4.0,
            "floor": 2.0,
            "upper_bound": "Infinity"
          }
        ]
      }
    },
    "b": {
      "histogram": {
        "buckets": [
          {
            "count": 4,
            "floor": -9223372036854775808,
            "upper_bound": 0
          },
          {
            "count": 1,
            "floor": 0,
            "upper_bound": 2
          },
          {
            "count": 3,
            "floor": 2,
            "upper_bound": 9223372036854775807
          }
        ]
      },
      "int": -2,
      "string": "some value"
    }
  }
}"#
        .to_string()
    }

    pub fn get_single_json_hierarchy() -> String {
        "{ \"root\": {
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
                                \"upper_bound\": 8.0
                            },
                            {
                                \"count\": 7.0,
                                \"floor\": 8.0,
                                \"upper_bound\": \"Infinity\"
                            }
                        ]
                    }
                },
                \"array\": [
                    0,
                    2,
                    18446744073709551615
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
                },
                \"bool_false\": false,
                \"bool_true\": true
            }}"
        .to_string()
    }
}
