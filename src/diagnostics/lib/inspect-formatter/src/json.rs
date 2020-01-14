// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{HierarchyDeserializer, HierarchySerializer},
    anyhow::{bail, format_err, Error},
    base64,
    fuchsia_inspect_node_hierarchy::{
        ArrayBucket, ArrayFormat, ArrayValue, NodeHierarchy, Property,
    },
    lazy_static::lazy_static,
    paste,
    serde::ser::{Serialize, SerializeMap, SerializeSeq, Serializer},
    serde_json::{
        self, json,
        ser::{PrettyFormatter, Serializer as JsonSerializer},
        Map, Value,
    },
    std::str,
};

/// Allows to serialize a `NodeHierarchy` into a Serde JSON Value.
pub struct JsonNodeHierarchySerializer {}

/// Allows to serialize a `NodeHierarchy` into a Serde JSON Value.
pub struct RawJsonNodeHierarchySerializer {}

/// Implements serialization of a `NodeHierarchy` into a Serde JSON Value.
impl HierarchySerializer for RawJsonNodeHierarchySerializer {
    type Type = serde_json::Value;

    fn serialize(hierarchy: NodeHierarchy) -> serde_json::Value {
        json!(JsonSerializableNodeHierarchy { hierarchy })
    }
}

/// Implements serialization of a `NodeHierarchy` into a String.
impl HierarchySerializer for JsonNodeHierarchySerializer {
    type Type = Result<String, anyhow::Error>;

    fn serialize(hierarchy: NodeHierarchy) -> Result<String, anyhow::Error> {
        let mut bytes = vec![];
        let mut serializer =
            JsonSerializer::with_formatter(&mut bytes, PrettyFormatter::with_indent(b"    "));
        let value = json!(JsonSerializableNodeHierarchy { hierarchy });
        value.serialize(&mut serializer)?;
        Ok(str::from_utf8(&bytes)?.to_string())
    }
}

/// Implements deserialization of a `NodeHierarchy` from a String.
impl HierarchyDeserializer for JsonNodeHierarchySerializer {
    // The Json Formatter deserializes JSON Strings encoding a single node hierarchy.
    type Object = String;

    fn deserialize(data_format: String) -> Result<NodeHierarchy, Error> {
        let root_node: serde_json::Value = serde_json::from_str(&data_format)?;
        deserialize_json(root_node)
    }
}

/// Implements deserialization of a `NodeHierarchy` from a Serde JSON Value.
impl HierarchyDeserializer for RawJsonNodeHierarchySerializer {
    // The Json Formatter deserializes JSON Strings encoding a single node hierarchy.
    type Object = serde_json::Value;

    fn deserialize(data: serde_json::Value) -> Result<NodeHierarchy, Error> {
        deserialize_json(data)
    }
}

/// A wrapper of a Node hierarchy that allows to serialize it as JSON.
struct JsonSerializableNodeHierarchy {
    /// The hierarchy that will be serialized.
    pub hierarchy: NodeHierarchy,
}

impl Serialize for JsonSerializableNodeHierarchy {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        let mut s = serializer.serialize_map(Some(1))?;
        let name = self.hierarchy.name.clone();
        s.serialize_entry(&name, &SerializableHierarchyFields { hierarchy: &self.hierarchy })?;
        s.end()
    }
}

// The following wrapping structs are used to implement Serialize on them given
// that it's not possible to implement traits for structs in other crates.

pub(in crate) struct SerializableHierarchyFields<'a> {
    pub(in crate) hierarchy: &'a NodeHierarchy,
}

struct SerializableArrayValue<'a, T> {
    array: &'a ArrayValue<T>,
}

struct SerializableArrayBucket<'a, T> {
    bucket: &'a ArrayBucket<T>,
}

impl<'a> Serialize for SerializableHierarchyFields<'a> {
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
                    let wrapped_value = SerializableArrayValue { array };
                    s.serialize_entry(&name, &wrapped_value)?;
                }
                Property::IntArray(name, array) => {
                    let wrapped_value = SerializableArrayValue { array };
                    s.serialize_entry(&name, &wrapped_value)?;
                }
                Property::UintArray(name, array) => {
                    let wrapped_value = SerializableArrayValue { array };
                    s.serialize_entry(&name, &wrapped_value)?;
                }
            };
        }
        for child in self.hierarchy.children.iter() {
            s.serialize_entry(&child.name, &SerializableHierarchyFields { hierarchy: child })?;
        }
        s.end()
    }
}

macro_rules! impl_serialize_for_array_value {
    ($($type:ty,)*) => {
        $(
            impl<'a> Serialize for SerializableArrayValue<'a, $type> {
                fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
                    match self.array.buckets() {
                        Some(buckets) => {
                            let mut s = serializer.serialize_map(Some(1))?;
                            let wrapped_buckets = buckets
                                .iter()
                                .map(|bucket| SerializableArrayBucket { bucket })
                                .collect::<Vec<SerializableArrayBucket<'_, $type>>>();
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
            impl<'a> Serialize for SerializableArrayBucket<'a, $type> {
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

impl<'a> Serialize for SerializableArrayBucket<'a, f64> {
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

/// Converts a JSON object encoding a NodeHierarchy into that NodeHierarchy. Expects
/// that the top-level serde_value is an object which is convertable into a NodeHierarchy node.:
/// eg:  "{
///          'root': {...}
///       }"
///
// TODO(43030): Remove explicit root nodes from serialized diagnostics data.
fn deserialize_json(root_node: serde_json::Value) -> Result<NodeHierarchy, Error> {
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

macro_rules! parse_array_fns {
    ($type:ty) => {
        paste::item! {
            /// Traverse an assumed numerical vector asserting that all serde
            /// values in the array are convertable to the $type numerical value.
            fn [<is_ $type _vec>](vec: &Vec<serde_json::Value>) -> bool
            {
                vec.iter().all(|num| num.[<is_ $type>]())
            }

            /// Traverse every histogram bucket and validate that all of its numerical
            /// values fit within the $type type.
            fn [<is_ $type _histogram>](vec: &Vec<serde_json::Value>) -> Result<bool, Error>
            {
                for bucket in vec {
                    let count_value =
                        sanitize_value(bucket.get(&*COUNT_KEY)
                                       .ok_or(format_err!(
                                           "All histogram buckets need `count` key."))?);

                    let floor_value =
                        sanitize_value(bucket.get(&*FLOOR_KEY)
                                       .ok_or(format_err!(
                                           "All histogram buckets need `floor` key."))?);

                    let upper_value =
                        sanitize_value(bucket.get(&*UPPER_BOUND_KEY)
                                       .ok_or(format_err!(
                                           "All histogram buckets need `upper_bound` key."))?);

                    if (!count_value.[<is_ $type>]()) {
                        return Ok(false);
                    }

                    if (!floor_value.[<is_ $type>]()) {
                        return Ok(false);
                    }

                    if (!upper_value.[<is_ $type>]()) {
                        return Ok(false);
                    }
                }

                Ok(true)
            }

            /// Converts a vector of serde Number values into a vector of $type
            /// numerical values.
            fn [<transform_numerical_ $type _vec>](vec: &Vec<serde_json::Value>)
                                                   -> Result<Vec<$type>, Error>
            {
                vec.iter().map(|serde_num| {
                    serde_num.
                        [<as_ $type>]()
                        .ok_or(format_err!("All types in an inspect array must be
 mapable to the same data type."))
                })
                    .collect::<Result<Vec<$type>, Error>>()
            }

            /// Extracts the first floor value of a histogram, which is equal to the first
            /// floor of a non-underflow bucket.
            fn [<extract_ $type _floor>](histogram: &Vec<serde_json::Value>, index: usize)
                                         -> Result<$type, Error> {
                if index == 0 {
                    return Err(format_err!("Getting a floor from an underflow bucket is meaningless."));
                }
                // The first legitimate floor is the upper bound of the underflow bucket.
                histogram.get(index)
                    .ok_or(format_err!("We've already validated the size of this histogram.."))?
                    .get(&*FLOOR_KEY)
                    .ok_or(format_err!("All histogram buckets need `upper_bound` key."))?
                    .[<as_ $type>]()
                    .ok_or(format_err!("Since this histogram already passed verification for
 this type, the only way this could occur is if the first legitimate floor is a string encoding
 of infinity, which is a verification error in Inspect."))
            }

            /// Computes the step size multiplier for the histogram. The histogram must
            /// have atleast 4 buckets in order to analyze a bucket size relationship between
            /// two non-overflow and non-underflow buckets.
            fn [<extract_ $type _step_multiplier>](histogram: &Vec<serde_json::Value>)
                                         -> Result<$type, Error> {
                if histogram.len() < 4 {
                    return Err(format_err!("Getting a step multiplier with only 1 non-overflow bucket is meaningless."));
                }
                // Get the upper bound of the first and second non-overflow bucket. We cannot use floors
                // and we cannot use relative bucket size because the algorithm used to generate histograms
                // is only consistent in floor growth and step size growth for values which are solutions
                // to the equation
                // floor + (initial_step * step_multiplier) - initial_step / initial_step == step_multiplier
                let first_upper = histogram.get(1)
                    .ok_or(format_err!("We've already validated the size of this histogram.."))?
                    .get(&*UPPER_BOUND_KEY)
                    .ok_or(format_err!("All histogram buckets need `upper_bound` key."))?
                    .[<as_ $type>]()
                    .ok_or(format_err!("Since this histogram already passed verification for
 this type, the only way this could occur is if the first legitimate floor is a string encoding
 of infinity, which is a verification error in Inspect."))?;

                let second_upper = histogram.get(2)
                    .ok_or(format_err!("We've already validated the size of this histogram.."))?
                    .get(&*UPPER_BOUND_KEY)
                    .ok_or(format_err!("All histogram buckets need `upper_bound` key."))?
                    .[<as_ $type>]()
                    .ok_or(format_err!("Since this histogram already passed verification for
 this type, the only way this could occur is if the first legitimate floor is a string encoding
 of infinity, which is a verification error in Inspect."))?;

                // Shift values by 1 to avoid division by 0. We will only ever have to shift
                // up to twice.
                Ok(if (first_upper == 0 as $type) || second_upper == 0 as $type {
                    let shifted_first = first_upper + 1 as $type;
                    let shifted_second = second_upper + 1 as $type;
                    if (shifted_first == 0 as $type) || shifted_second == 0 as $type {
                        shifted_second + 1 as $type / shifted_first + 1 as $type
                    } else {
                        shifted_second / shifted_first
                    }
                } else {
                    second_upper / first_upper
                })
            }


            /// Take a bucket and compute the distance between the upper
            /// bound and the floor. If the provided index is the first or last
            /// bucket in the histogram, this is an error since step sizes for
            /// overflow buckets are meaningless.
            fn [<extract_ $type _step_size>](histogram: &Vec<serde_json::Value>, index: usize)
                                             -> Result<$type, Error> {

                if index == 0 || index == histogram.len()-1 {
                    return Err(format_err!("Cannot extract step sizes from overflow buckets."))
                }

                let non_edge_bucket = histogram.get(index)
                    .ok_or(format_err!("The JSON serializer requires that histograms have
enough data to encode 3 buckets, or one non overflow bucket, so this shouldn't happen."))?;

                let legitimate_upper_bound = non_edge_bucket
                    .get(&*UPPER_BOUND_KEY)
                    .ok_or(format_err!("All histogram buckets need `upper_bound` key."))?
                    .[<as_ $type>]()
                    .ok_or(format_err!("Since this histogram already passed verification for
this type, the only way this could occur is if there exists a string encoding of infinity
present as a upper bound in a non-overflow bucket. This is a verification error in Inspect."))?;

                let legitimate_floor_bound = non_edge_bucket
                    .get(&*FLOOR_KEY)
                    .ok_or(format_err!("All histogram buckets need `floor ` key."))?
                    .[<as_ $type>]()
                    .ok_or(format_err!("Since this histogram already passed verification for
this type, the only way this could occur is if there exists a string encoding of infinity
present as a floor in a non-overflow bucket. This is a verification error in Inspect."))?;

                Ok(legitimate_upper_bound - legitimate_floor_bound)
            }

            /// Iterates over histogram buckets and extracts the count keys into their own
            /// vector, typed as $type.
            fn [<extract_ $type _counts>](histogram: &Vec<serde_json::Value>)
                                          -> Result<Vec<$type>, Error> {
                    histogram.into_iter().map(|serde_val| {
                        serde_val
                            .get(&*COUNT_KEY)
                            .and_then(|val| val.[<as_ $type>]())
                            .ok_or(format_err!("Since this histogram already passed verification
 for this type, the only way this could occur is if there exists a string encoding of infinity
present as a count. This is a verification error in Inspect."))
                    }).collect()
            }

            /// Converts a list of histogram buckets into an Inspect ArrayValue representing
            /// the histogram.
            ///
            /// Assumes that the histogram will be atleast 3 buckets long, since the
            /// library which serializes to JSON requires that the serialized bucket
            /// contain atleast enough entries to create 3 buckets.
            ///
            /// NOTE: If there are less than 4 buckets, this is a lossy deserialization, since
            ///       we do not have two non-edge buckets to check step size growth against.
            ///       We conservatively assume that the histogram is linear.
            fn [<parse_ $type _histogram>](histogram: &Vec<serde_json::Value>)
                                           -> Result<ArrayValue<$type>, Error> {
                // Get the first floor from bucket 1 since bucket 0 is an underflow
                // bucket whose floor is nonsensical.
                let first_floor = [<extract_ $type _floor>](histogram, 1)?;

                let first_step_size = [<extract_ $type _step_size>](histogram, 1)?;

                let counts_vec = [<extract_ $type _counts>](histogram)?;

                // If a histogram has less than 4 buckets, then we cannot deduce if
                // it is linear or exponential, since there aren't two non-overflow
                // buckets to analyze step size growth against. In this case, lossily
                // convert to a linear histogram.
                if histogram.len() < 4 {
                    // [floor, step]
                    let configuring_bits =
                        vec![first_floor, first_step_size];

                    return Ok(ArrayValue::new(
                        [configuring_bits, counts_vec].concat(),
                        ArrayFormat::LinearHistogram
                    ))
                }

                let step_multiplier = [<extract_ $type _step_multiplier>](histogram)?;

                if step_multiplier == 1 as $type {
                    // [floor, step]
                    let configuring_bits =
                        vec![first_floor, first_step_size];

                    return Ok(ArrayValue::new(
                        [configuring_bits, counts_vec].concat(),
                        ArrayFormat::LinearHistogram
                    ));
                }

                // [floor, step, multiplier]
                let configuring_bits =
                    vec![first_floor, first_step_size, step_multiplier];

                Ok(ArrayValue::new(
                    [configuring_bits, counts_vec].concat(),
                    ArrayFormat::ExponentialHistogram
                ))
            }
        }
    };
}

// Generates the following methods:
// fn is_f64_array()
// fn is_f64_histogram()
// fn transform_numerical_f64_vec()
// fn extract_f64_step_size()
// fn extract_f64_counts()
// fn parse_f64_histogram()
parse_array_fns!(f64);

// fn Generates the following methods:
// fn is_u64_array()
// fn is_u64_histogram()
// fn transform_numerical_u64_vec()
// fn extract_u64_step_size()
// fn extract_u64_counts()
// fn parse_u64_histogram()
parse_array_fns!(u64);

// fn Generates the following methods:
// fn is_i64_array()
// fn is_i64_histogram()
// fn transform_numerical_i64_vec()
// fn extract_i64_step_size()
// fn extract_i64_counts()
// fn parse_i64_histogram()
parse_array_fns!(i64);

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

/// Parses a JSON representation of an Inspect histogram into its ArrayValue form.
fn parse_histogram(name: &String, histogram: &Vec<serde_json::Value>) -> Result<Property, Error> {
    if histogram.len() < 3 {
        bail!(
            "Histograms require a minimum of 5 entries to be validly serialized. We can only
uce 5 entries if there exist 3 or more buckets. If this isn't the case, this histogram is
ormed."
        );
    }

    if is_f64_histogram(histogram)? {
        return Ok(Property::DoubleArray(name.to_string(), parse_f64_histogram(histogram)?));
    }

    if is_i64_histogram(histogram)? {
        return Ok(Property::IntArray(name.to_string(), parse_i64_histogram(histogram)?));
    }

    if is_u64_histogram(histogram)? {
        return Ok(Property::UintArray(name.to_string(), parse_u64_histogram(histogram)?));
    }

    return Err(format_err!(
        "Histograms must be one of i64, u64, or f64. Property name: {:?}",
        name
    ));
}

/// Parses a JSON array into its numerical Inspect ArrayValue.
fn parse_array(name: &String, vec: &Vec<serde_json::Value>) -> Result<Property, Error> {
    let array_format = ArrayFormat::Default;

    if is_f64_vec(vec) {
        return Ok(Property::DoubleArray(
            name.to_string(),
            ArrayValue::new(transform_numerical_f64_vec(vec)?, array_format),
        ));
    }

    if is_i64_vec(vec) {
        return Ok(Property::IntArray(
            name.to_string(),
            ArrayValue::new(transform_numerical_i64_vec(vec)?, array_format),
        ));
    }

    if is_u64_vec(vec) {
        return Ok(Property::UintArray(
            name.to_string(),
            ArrayValue::new(transform_numerical_u64_vec(vec)?, array_format),
        ));
    }

    return Err(format_err!("Arrays must be one of i64, u64, or f64. Property name: {:?}", name));
}

/// Parses a serde_json Number into an Inspect number Property.
fn parse_number(name: &String, num: &serde_json::Number) -> Result<Property, Error> {
    if num.is_i64() {
        Ok(Property::Int(name.to_string(), num.as_i64().unwrap()))
    } else if num.is_u64() {
        Ok(Property::Uint(name.to_string(), num.as_u64().unwrap()))
    } else if num.is_f64() {
        Ok(Property::Double(name.to_string(), num.as_f64().unwrap()))
    } else {
        return Err(format_err!("Diagnostics numbers must fit within 64 bits."));
    }
}

/// Creates a NodeHierarchy from a serde_json Object map, evaluating each of
/// the entries in the map and parsing them into their relevant Inspect in-memory
/// representation.
fn parse_node_object(
    node_name: &String,
    contents: &Map<String, Value>,
) -> Result<NodeHierarchy, Error> {
    let mut properties: Vec<Property> = Vec::new();
    let mut children: Vec<NodeHierarchy> = Vec::new();
    for (name, value) in contents.iter() {
        match value {
            serde_json::Value::Object(obj) => match fetch_object_histogram(obj) {
                Some(histogram_vec) => {
                    properties.push(parse_histogram(name, histogram_vec)?);
                }
                None => {
                    let child_node = parse_node_object(name, obj)?;
                    children.push(child_node);
                }
            },
            serde_json::Value::Bool(_) => {
                // TODO(37140): Deserialize booleans when supported.
                return Err(format_err!("Booleans are not part of the diagnostics schema."));
            }
            serde_json::Value::Number(num) => {
                properties.push(parse_number(name, num)?);
            }
            serde_json::Value::String(string) => {
                let string_property = Property::String(name.to_string(), string.to_string());
                properties.push(string_property);
            }
            serde_json::Value::Array(vec) => {
                properties.push(parse_array(name, vec)?);
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
    use {
        super::*,
        fuchsia_inspect_node_hierarchy::{ArrayFormat, ArrayValue, Property},
    };

    #[test]
    fn serialize_json() {
        let hierarchy = test_hierarchy();
        let expected = expected_json();
        let result =
            JsonNodeHierarchySerializer::serialize(hierarchy).expect("failed to serialize");
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
        let result = JsonNodeHierarchySerializer::serialize(original_hierarchy.clone())
            .expect("failed to format hierarchy");
        let mut parsed_hierarchy = JsonNodeHierarchySerializer::deserialize(result)?;
        parsed_hierarchy.sort();
        original_hierarchy.sort();
        assert_eq!(original_hierarchy, parsed_hierarchy);
        Ok(())
    }

    // Creates a hierarchy that isn't lossy due to its unambigious values.
    fn get_unambigious_deserializable_hierarchy() -> NodeHierarchy {
        NodeHierarchy::new(
            "root",
            vec![Property::UintArray(
                "array".to_string(),
                ArrayValue::new(vec![0, 2, std::u64::MAX], ArrayFormat::Default),
            )],
            vec![
                NodeHierarchy::new(
                    "a",
                    vec![
                        Property::Double("double".to_string(), 2.5),
                        Property::DoubleArray(
                            "histogram".to_string(),
                            ArrayValue::new(
                                vec![0.0, 2.0, 4.0, 1.0, 3.0, 4.0, 7.0],
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
        )
    }

    fn test_hierarchy() -> NodeHierarchy {
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
                            ArrayValue::new(vec![0, 2, 4, 1, 3], ArrayFormat::LinearHistogram),
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
        "array": [
            0,
            2,
            4
        ],
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
                }
            }}"
        .to_string()
    }
}
