// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{ArrayContent, Bucket, NodeHierarchy, Property},
    lazy_static::lazy_static,
    paste,
    serde::{
        de::{self, MapAccess, SeqAccess, Visitor},
        Deserialize, Deserializer,
    },
    std::{cmp::Eq, collections::HashMap, fmt, hash::Hash, marker::PhantomData, str::FromStr},
};

lazy_static! {
    static ref HISTOGRAM_OBJECT_KEY: String = "buckets".to_string();
}

struct RootVisitor<Key> {
    // Key is unused.
    marker: PhantomData<Key>,
}

impl<'de, Key> Visitor<'de> for RootVisitor<Key>
where
    Key: FromStr + Clone + Hash + Eq + AsRef<str>,
{
    type Value = NodeHierarchy<Key>;

    fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("there should be a single root")
    }

    fn visit_map<V>(self, mut map: V) -> Result<NodeHierarchy<Key>, V::Error>
    where
        V: MapAccess<'de>,
    {
        let result = match map.next_entry::<String, FieldValue<Key>>()? {
            Some((map_key, value)) => {
                let key = Key::from_str(&map_key)
                    .map_err(|_| de::Error::custom("failed to parse key"))?;
                value.into_node(&key)
            }
            None => return Err(de::Error::invalid_length(0, &"expected a root node")),
        };

        let mut found = 1;
        while map.next_key::<String>()?.is_some() {
            found += 1;
        }

        if found > 1 {
            return Err(de::Error::invalid_length(found, &"expected a single root"));
        }

        result.ok_or(de::Error::custom("expected node for root"))
    }
}

impl<'de, Key> Deserialize<'de> for NodeHierarchy<Key>
where
    Key: FromStr + Clone + Hash + Eq + AsRef<str>,
{
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        deserializer.deserialize_map(RootVisitor { marker: PhantomData })
    }
}

trait IntoProperty<Key> {
    fn into_property(self, key: &Key) -> Option<Property<Key>>;
}

/// The value of an inspect tree field (node or property).
enum FieldValue<Key> {
    String(String),
    Bytes(Vec<u8>),
    Int(i64),
    Uint(u64),
    Double(f64),
    Bool(bool),
    Array(Vec<NumericValue>),
    Histogram(Vec<Bucket<NumericValue>>),
    Node(HashMap<Key, FieldValue<Key>>),
}

impl<Key: Clone> IntoProperty<Key> for FieldValue<Key> {
    fn into_property(self, key: &Key) -> Option<Property<Key>> {
        match self {
            Self::String(value) => Some(Property::String(key.clone(), value)),
            Self::Bytes(value) => Some(Property::Bytes(key.clone(), value)),
            Self::Int(value) => Some(Property::Int(key.clone(), value)),
            Self::Uint(value) => Some(Property::Uint(key.clone(), value)),
            Self::Double(value) => Some(Property::Double(key.clone(), value)),
            Self::Bool(value) => Some(Property::Bool(key.clone(), value)),
            Self::Array(values) => values.into_property(key),
            Self::Histogram(buckets) => buckets.into_property(key),
            Self::Node(_) => None,
        }
    }
}

impl<Key: AsRef<str> + Clone> FieldValue<Key> {
    fn is_property(&self) -> bool {
        !matches!(self, Self::Node(_))
    }

    fn into_node(self, key: &Key) -> Option<NodeHierarchy<Key>> {
        match self {
            Self::Node(map) => {
                let mut properties = vec![];
                let mut children = vec![];
                for (map_key, value) in map {
                    if value.is_property() {
                        properties.push(value.into_property(&map_key).unwrap());
                    } else {
                        children.push(value.into_node(&map_key).unwrap());
                    }
                }
                Some(NodeHierarchy::new(key.as_ref(), properties, children))
            }
            _ => None,
        }
    }
}

impl<'de, Key> Deserialize<'de> for FieldValue<Key>
where
    Key: FromStr + Hash + Eq,
{
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        deserializer.deserialize_any(FieldVisitor { marker: PhantomData })
    }
}

struct FieldVisitor<Key> {
    marker: PhantomData<Key>,
}

impl<'de, Key> Visitor<'de> for FieldVisitor<Key>
where
    Key: FromStr + Hash + Eq,
{
    type Value = FieldValue<Key>;

    fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("failed to field")
    }

    fn visit_map<V>(self, mut map: V) -> Result<Self::Value, V::Error>
    where
        V: MapAccess<'de>,
    {
        let mut entries = vec![];
        while let Some(entry) = map.next_entry::<String, FieldValue<Key>>()? {
            entries.push(entry);
        }
        // Histograms are of the type: { buckets: [ { } ]}, therefore here we expect to have an
        // object with a single entry, whose key is buckets and the value is a Histogram.
        if entries.len() == 1
            && entries[0].0 == *HISTOGRAM_OBJECT_KEY
            && matches!(entries[0].1, FieldValue::Histogram(_))
        {
            let histogram_value = entries.pop().unwrap().1;
            return Ok(histogram_value);
        }

        let node = entries
            .into_iter()
            .map(|(key, value)| Key::from_str(&key).map(|key| (key, value)))
            .collect::<Result<HashMap<Key, FieldValue<Key>>, _>>()
            .map_err(|_| de::Error::custom("failed to parse key"))?;
        Ok(FieldValue::Node(node))
    }

    fn visit_i64<E>(self, value: i64) -> Result<Self::Value, E> {
        Ok(FieldValue::Int(value))
    }

    fn visit_u64<E>(self, value: u64) -> Result<Self::Value, E> {
        Ok(FieldValue::Uint(value))
    }

    fn visit_f64<E>(self, value: f64) -> Result<Self::Value, E> {
        Ok(FieldValue::Double(value))
    }

    fn visit_bool<E>(self, value: bool) -> Result<Self::Value, E> {
        Ok(FieldValue::Bool(value))
    }

    fn visit_str<E>(self, value: &str) -> Result<Self::Value, E>
    where
        E: de::Error,
    {
        if value.starts_with("b64:") {
            let bytes64 = value.replace("b64:", "");
            let bytes = base64::decode(&bytes64)
                .map_err(|_| de::Error::custom("failed to decode bytes"))?;
            return Ok(FieldValue::Bytes(bytes));
        }
        Ok(FieldValue::String(value.to_string()))
    }

    fn visit_seq<S>(self, mut seq: S) -> Result<Self::Value, S::Error>
    where
        S: SeqAccess<'de>,
    {
        let mut result = vec![];
        while let Some(elem) = seq.next_element::<SeqItem>()? {
            result.push(elem);
        }
        // There can be two types of sequences: the histogram (containing buckets) and the regular
        // arrays (containing numeric values). There cannot be a sequence containing a mix of them.
        let mut array = vec![];
        let mut histogram = vec![];
        for item in result {
            match item {
                SeqItem::Value(x) => array.push(x),
                SeqItem::Bucket(x) => histogram.push(x),
            }
        }
        if histogram.len() > 0 && array.is_empty() {
            return Ok(FieldValue::Histogram(histogram));
        }
        if array.len() > 0 && histogram.is_empty() {
            return Ok(FieldValue::Array(array));
        }
        Err(de::Error::custom("unexpected sequence containing mixed values"))
    }
}

#[derive(Deserialize)]
#[serde(untagged)]
enum SeqItem {
    Value(NumericValue),
    Bucket(Bucket<NumericValue>),
}

enum NumericValue {
    Positive(u64),
    Negative(i64),
    Double(f64),
}

impl NumericValue {
    #[inline]
    fn as_i64(&self) -> Option<i64> {
        match self {
            Self::Positive(x) if *x <= i64::max_value() as u64 => Some(*x as i64),
            Self::Negative(x) => Some(*x),
            _ => None,
        }
    }

    #[inline]
    fn as_u64(&self) -> Option<u64> {
        match self {
            Self::Positive(x) => Some(*x),
            _ => None,
        }
    }

    #[inline]
    fn as_f64(&self) -> Option<f64> {
        match self {
            Self::Double(x) => Some(*x),
            _ => None,
        }
    }
}

impl<'de> Deserialize<'de> for NumericValue {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        deserializer.deserialize_any(NumericValueVisitor)
    }
}

impl<Key: Clone> IntoProperty<Key> for Vec<NumericValue> {
    fn into_property(self, key: &Key) -> Option<Property<Key>> {
        if let Some(values) = parse_f64_vec(&self) {
            return Some(Property::DoubleArray(key.clone(), ArrayContent::Values(values)));
        }
        if let Some(values) = parse_i64_vec(&self) {
            return Some(Property::IntArray(key.clone(), ArrayContent::Values(values)));
        }
        if let Some(values) = parse_u64_vec(&self) {
            return Some(Property::UintArray(key.clone(), ArrayContent::Values(values)));
        }
        None
    }
}

impl<Key: Clone> IntoProperty<Key> for Vec<Bucket<NumericValue>> {
    fn into_property(self, key: &Key) -> Option<Property<Key>> {
        if let Some(buckets) = parse_f64_buckets(&self) {
            return Some(Property::DoubleArray(key.clone(), ArrayContent::Buckets(buckets)));
        }
        if let Some(buckets) = parse_i64_buckets(&self) {
            return Some(Property::IntArray(key.clone(), ArrayContent::Buckets(buckets)));
        }
        if let Some(buckets) = parse_u64_buckets(&self) {
            return Some(Property::UintArray(key.clone(), ArrayContent::Buckets(buckets)));
        }
        None
    }
}

macro_rules! parse_numeric_vec_impls {
    ($($type:ty),*) => {
        $(
            paste::paste! {
                fn [<parse_ $type _vec>](vec: &Vec<NumericValue>) -> Option<Vec<$type>> {
                    vec.iter().map(|value| value.[<as_ $type>]()).collect::<Option<Vec<_>>>()
                }

                fn [<parse_ $type _buckets>](buckets: &Vec<Bucket<NumericValue>>)
                    -> Option<Vec<Bucket<$type>>>
                {
                    buckets.iter().map(|bucket| {
                        let floor = match bucket.floor.[<as_ $type>]() {
                            Some(f) => f,
                            _ => return None,
                        };
                        let upper = match bucket.upper.[<as_ $type>]() {
                            Some(u) => u,
                            _ => return None,
                        };
                        let count = match bucket.count.[<as_ $type>]() {
                            Some(c) => c,
                            _ => return None,
                        };
                        Some(Bucket { floor, upper, count })
                    })
                    .collect::<Option<Vec<_>>>()
                }
            }
        )*
    };
}

// Generates the following functions:
// fn parse_f64_vec()
// fn parse_u64_vec()
// fn parse_i64_vec()
// fn parse_f64_buckets()
// fn parse_u64_buckets()
// fn parse_i64_buckets()
parse_numeric_vec_impls!(f64, u64, i64);

struct NumericValueVisitor;

impl<'de> Visitor<'de> for NumericValueVisitor {
    type Value = NumericValue;

    fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("failed to deserialize bucket array")
    }

    fn visit_i64<E>(self, value: i64) -> Result<Self::Value, E> {
        if value < 0 {
            return Ok(NumericValue::Negative(value));
        }
        Ok(NumericValue::Positive(value as u64))
    }

    fn visit_u64<E>(self, value: u64) -> Result<Self::Value, E> {
        Ok(NumericValue::Positive(value))
    }

    fn visit_f64<E>(self, value: f64) -> Result<Self::Value, E> {
        Ok(NumericValue::Double(value))
    }

    fn visit_str<E>(self, value: &str) -> Result<Self::Value, E>
    where
        E: de::Error,
    {
        if value == "-Infinity" {
            return Ok(NumericValue::Double(std::f64::MIN));
        }
        if value == "Infinity" {
            return Ok(NumericValue::Double(std::f64::MAX));
        }
        Err(de::Error::invalid_value(de::Unexpected::Str(value), &"Infinity/-Infinity or a number"))
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::ArrayFormat};

    #[test]
    fn deserialize_json() {
        let json_string = get_single_json_hierarchy();
        let mut parsed_hierarchy: NodeHierarchy =
            serde_json::from_str(&json_string).expect("deserialized");
        let mut expected_hierarchy = get_unambigious_deserializable_hierarchy();
        parsed_hierarchy.sort();
        expected_hierarchy.sort();
        assert_eq!(expected_hierarchy, parsed_hierarchy);
    }

    #[test]
    fn reversible_deserialize() {
        let mut original_hierarchy = get_unambigious_deserializable_hierarchy();
        let result =
            serde_json::to_string(&original_hierarchy).expect("failed to format hierarchy");
        let mut parsed_hierarchy: NodeHierarchy =
            serde_json::from_str(&result).expect("deserialized");
        parsed_hierarchy.sort();
        original_hierarchy.sort();
        assert_eq!(original_hierarchy, parsed_hierarchy);
    }

    #[test]
    fn test_exp_histogram() {
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
        let mut parsed_hierarchy: NodeHierarchy =
            serde_json::from_value(result_json).expect("deserialized");
        parsed_hierarchy.sort();
        hierarchy.sort();
        assert_eq!(hierarchy, parsed_hierarchy);
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
