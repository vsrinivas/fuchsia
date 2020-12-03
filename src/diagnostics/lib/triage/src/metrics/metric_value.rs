// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::Lambda,
    diagnostics_hierarchy::{ArrayContent, Property as DiagnosticProperty},
    serde::Deserialize,
    serde_json::Value as JsonValue,
};

/// The calculated or selected value of a Metric.
///
/// Missing means that the value could not be calculated; its String tells
/// the reason.
#[derive(Deserialize, Debug, Clone)]
pub enum MetricValue {
    // Ensure every variant of MetricValue is tested in metric_value_traits().
    // TODO(cphoenix): Support u64.
    Int(i64),
    Float(f64),
    String(String),
    Bool(bool),
    Vector(Vec<MetricValue>),
    Bytes(Vec<u8>),
    Missing(String),
    Lambda(Box<Lambda>),
}

impl PartialEq for MetricValue {
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (MetricValue::Int(l), MetricValue::Int(r)) => l == r,
            (MetricValue::Float(l), MetricValue::Float(r)) => l == r,
            (MetricValue::Bytes(l), MetricValue::Bytes(r)) => l == r,
            (MetricValue::Int(l), MetricValue::Float(r)) => *l as f64 == *r,
            (MetricValue::Float(l), MetricValue::Int(r)) => *l == *r as f64,
            (MetricValue::String(l), MetricValue::String(r)) => l == r,
            (MetricValue::Bool(l), MetricValue::Bool(r)) => l == r,
            (MetricValue::Vector(l), MetricValue::Vector(r)) => l == r,
            _ => false,
        }
    }
}

impl Eq for MetricValue {}

impl std::fmt::Display for MetricValue {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match &*self {
            MetricValue::Int(n) => write!(f, "Int({})", n),
            MetricValue::Float(n) => write!(f, "Float({})", n),
            MetricValue::Bool(n) => write!(f, "Bool({})", n),
            MetricValue::String(n) => write!(f, "String({})", n),
            MetricValue::Vector(n) => write!(f, "Vector({:?})", n),
            MetricValue::Bytes(n) => write!(f, "Bytes({:?})", n),
            MetricValue::Missing(n) => write!(f, "Missing({})", n),
            MetricValue::Lambda(n) => write!(f, "Fn({:?})", n),
        }
    }
}

impl Into<MetricValue> for f64 {
    fn into(self) -> MetricValue {
        MetricValue::Float(self)
    }
}

impl Into<MetricValue> for i64 {
    fn into(self) -> MetricValue {
        MetricValue::Int(self)
    }
}

impl From<DiagnosticProperty> for MetricValue {
    fn from(property: DiagnosticProperty) -> Self {
        match property {
            DiagnosticProperty::String(_name, value) => Self::String(value),
            DiagnosticProperty::Bytes(_name, value) => Self::Bytes(value),
            DiagnosticProperty::Int(_name, value) => Self::Int(value),
            DiagnosticProperty::Uint(_name, value) => Self::Int(value as i64),
            DiagnosticProperty::Double(_name, value) => Self::Float(value),
            DiagnosticProperty::Bool(_name, value) => Self::Bool(value),
            // TODO(cphoenix): Figure out what to do about histograms.
            DiagnosticProperty::DoubleArray(_name, ArrayContent::Values(values)) => {
                Self::Vector(super::map_vec(&values, |value| Self::Float(*value)))
            }
            DiagnosticProperty::IntArray(_name, ArrayContent::Values(values)) => {
                Self::Vector(super::map_vec(&values, |value| Self::Int(*value)))
            }
            DiagnosticProperty::UintArray(_name, ArrayContent::Values(values)) => {
                Self::Vector(super::map_vec(&values, |value| Self::Int(*value as i64)))
            }
            DiagnosticProperty::DoubleArray(_name, ArrayContent::Buckets(_))
            | DiagnosticProperty::IntArray(_name, ArrayContent::Buckets(_))
            | DiagnosticProperty::UintArray(_name, ArrayContent::Buckets(_)) => {
                Self::Missing("Histograms aren't supported (yet)".to_string())
            }
            DiagnosticProperty::StringList(_name, _list) => {
                Self::Missing("StringLists aren't supported (yet)".to_string())
            }
        }
    }
}

impl From<JsonValue> for MetricValue {
    fn from(value: JsonValue) -> Self {
        match value {
            JsonValue::String(value) => Self::String(value),
            JsonValue::Bool(value) => Self::Bool(value),
            JsonValue::Number(_) => Self::from(&value),
            JsonValue::Array(values) => {
                Self::Vector(values.into_iter().map(|v| Self::from(v)).collect())
            }
            _ => Self::Missing("Unsupported JSON type".to_owned()),
        }
    }
}

impl From<&JsonValue> for MetricValue {
    fn from(value: &JsonValue) -> Self {
        match value {
            JsonValue::String(value) => Self::String(value.clone()),
            JsonValue::Bool(value) => Self::Bool(*value),
            JsonValue::Number(value) => {
                if value.is_i64() {
                    Self::Int(value.as_i64().unwrap())
                } else if value.is_u64() {
                    Self::Int(value.as_u64().unwrap() as i64)
                } else if value.is_f64() {
                    Self::Float(value.as_f64().unwrap())
                } else {
                    Self::Missing("Unable to convert JSON number".to_owned())
                }
            }
            JsonValue::Array(values) => {
                Self::Vector(values.iter().map(|v| Self::from(v)).collect())
            }
            _ => Self::Missing("Unsupported JSON type".to_owned()),
        }
    }
}

#[cfg(test)]
pub(crate) mod test {
    use {
        super::*,
        crate::assert_missing,
        serde_json::{json, Number as JsonNumber},
    };

    #[test]
    fn test_equality() {
        // Equal Value, Equal Type
        assert_eq!(MetricValue::Int(1), MetricValue::Int(1));
        assert_eq!(MetricValue::Float(1.0), MetricValue::Float(1.0));
        assert_eq!(MetricValue::String("A".to_string()), MetricValue::String("A".to_string()));
        assert_eq!(MetricValue::Bool(true), MetricValue::Bool(true));
        assert_eq!(MetricValue::Bool(false), MetricValue::Bool(false));
        assert_eq!(
            MetricValue::Vector(vec![
                MetricValue::Int(1),
                MetricValue::Float(1.0),
                MetricValue::String("A".to_string()),
                MetricValue::Bool(true),
            ]),
            MetricValue::Vector(vec![
                MetricValue::Int(1),
                MetricValue::Float(1.0),
                MetricValue::String("A".to_string()),
                MetricValue::Bool(true),
            ])
        );
        assert_eq!(MetricValue::Bytes(vec![1, 2, 3]), MetricValue::Bytes(vec![1, 2, 3]));

        // Floats and ints interconvert. Test both ways for full code coverage.
        assert_eq!(MetricValue::Int(1), MetricValue::Float(1.0));
        assert_eq!(MetricValue::Float(1.0), MetricValue::Int(1));

        // Numbers, vectors, and byte arrays do not interconvert when compared with Rust ==.
        // Note, though, that the expression "1 == [1]" will evaluate to true.
        assert!(MetricValue::Int(1) != MetricValue::Vector(vec![MetricValue::Int(1)]));
        assert!(MetricValue::Bytes(vec![1]) != MetricValue::Vector(vec![MetricValue::Int(1)]));
        assert!(MetricValue::Int(1) != MetricValue::Bytes(vec![1]));

        // Nested array
        assert_eq!(
            MetricValue::Vector(vec![
                MetricValue::Int(1),
                MetricValue::Float(1.0),
                MetricValue::String("A".to_string()),
                MetricValue::Bool(true),
            ]),
            MetricValue::Vector(vec![
                MetricValue::Int(1),
                MetricValue::Float(1.0),
                MetricValue::String("A".to_string()),
                MetricValue::Bool(true),
            ])
        );

        // Missing should never be equal
        assert!(MetricValue::Missing("err".to_string()) != MetricValue::Missing("err".to_string()));
        // Use assert_missing() macro to test error messages.
        assert_missing!(MetricValue::Missing("err".to_string()), "err");

        // We don't have a contract for Lambda equality. We probably don't need one.
    }

    #[test]
    fn test_inequality() {
        // Different Value, Equal Type
        assert_ne!(MetricValue::Int(1), MetricValue::Int(2));
        assert_ne!(MetricValue::Float(1.0), MetricValue::Float(2.0));
        assert_ne!(MetricValue::String("A".to_string()), MetricValue::String("B".to_string()));
        assert_ne!(MetricValue::Bool(true), MetricValue::Bool(false));
        assert_ne!(
            MetricValue::Vector(vec![
                MetricValue::Int(1),
                MetricValue::Float(1.0),
                MetricValue::String("A".to_string()),
                MetricValue::Bool(true),
            ]),
            MetricValue::Vector(vec![
                MetricValue::Int(2),
                MetricValue::Float(2.0),
                MetricValue::String("B".to_string()),
                MetricValue::Bool(false),
            ])
        );

        // Different Type
        assert_ne!(MetricValue::Int(2), MetricValue::Float(1.0));
        assert_ne!(MetricValue::Int(1), MetricValue::String("A".to_string()));
        assert_ne!(MetricValue::Int(1), MetricValue::Bool(true));
        assert_ne!(MetricValue::Float(1.0), MetricValue::String("A".to_string()));
        assert_ne!(MetricValue::Float(1.0), MetricValue::Bool(true));
        assert_ne!(MetricValue::String("A".to_string()), MetricValue::Bool(true));
    }

    #[test]
    fn test_fmt() {
        assert_eq!(format!("{}", MetricValue::Int(3)), "Int(3)");
        assert_eq!(format!("{}", MetricValue::Float(3.5)), "Float(3.5)");
        assert_eq!(format!("{}", MetricValue::Bool(true)), "Bool(true)");
        assert_eq!(format!("{}", MetricValue::Bool(false)), "Bool(false)");
        assert_eq!(format!("{}", MetricValue::String("cat".to_string())), "String(cat)");
        assert_eq!(
            format!("{}", MetricValue::Vector(vec![MetricValue::Int(1), MetricValue::Float(2.5)])),
            "Vector([Int(1), Float(2.5)])"
        );
        assert_eq!(format!("{}", MetricValue::Bytes(vec![1u8, 2u8])), "Bytes([1, 2])");
        assert_eq!(
            format!("{}", MetricValue::Missing("Where is Waldo?".to_string())),
            "Missing(Where is Waldo?)"
        );
    }

    #[test]
    fn metric_value_from_json() {
        /*
            JSON subtypes:
                Bool(bool),
                Number(Number),
                String(String),
                Array(Vec<Value>),
                Object(Map<String, Value>),
        */
        macro_rules! test_from {
            ($json:path, $metric:path, $value:expr) => {
                test_from_to!($json, $metric, $value, $value);
            };
        }
        macro_rules! test_from_int {
            ($json:path, $metric:path, $value:expr) => {
                test_from_to!($json, $metric, JsonNumber::from($value), $value);
            };
        }
        macro_rules! test_from_float {
            ($json:path, $metric:path, $value:expr) => {
                test_from_to!($json, $metric, JsonNumber::from_f64($value).unwrap(), $value);
            };
        }
        macro_rules! test_from_to {
            ($json:path, $metric:path, $json_value:expr, $metric_value:expr) => {
                let metric_value = $metric($metric_value);
                let json_value = $json($json_value);
                assert_eq!(metric_value, MetricValue::from(json_value));
            };
        }
        test_from!(JsonValue::String, MetricValue::String, "Hi World".to_string());
        test_from_int!(JsonValue::Number, MetricValue::Int, 3);
        test_from_int!(JsonValue::Number, MetricValue::Int, std::i64::MAX);
        test_from_int!(JsonValue::Number, MetricValue::Int, std::i64::MIN);
        test_from_to!(JsonValue::Number, MetricValue::Int, JsonNumber::from(std::u64::MAX), -1);
        test_from_float!(JsonValue::Number, MetricValue::Float, 3.14);
        test_from_float!(JsonValue::Number, MetricValue::Float, std::f64::MAX);
        test_from_float!(JsonValue::Number, MetricValue::Float, std::f64::MIN);
        test_from!(JsonValue::Bool, MetricValue::Bool, true);
        test_from!(JsonValue::Bool, MetricValue::Bool, false);
        let json_vec = vec![json!(1), json!(2), json!(3)];
        let metric_vec = vec![MetricValue::Int(1), MetricValue::Int(2), MetricValue::Int(3)];
        test_from_to!(JsonValue::Array, MetricValue::Vector, json_vec, metric_vec);
        assert_missing!(
            MetricValue::from(JsonValue::Object(serde_json::Map::new())),
            "Unsupported JSON type"
        );
    }

    #[test]
    fn metric_value_from_diagnostic_property() {
        /*
            DiagnosticProperty subtypes:
                String(Key, String),
                Bytes(Key, Vec<u8>),
                Int(Key, i64),
                Uint(Key, u64),
                Double(Key, f64),
                Bool(Key, bool),
                DoubleArray(Key, ArrayContent<f64>),
                IntArray(Key, ArrayContent<i64>),
                UintArray(Key, ArrayContent<u64>),
        */
        macro_rules! test_from {
            ($diagnostic:path, $metric:path, $value:expr) => {
                test_from_to!($diagnostic, $metric, $value, $value);
            };
        }
        macro_rules! test_from_to {
            ($diagnostic:path, $metric:path, $diagnostic_value:expr, $metric_value:expr) => {
                assert_eq!(
                    $metric($metric_value),
                    MetricValue::from($diagnostic("foo".to_string(), $diagnostic_value))
                );
            };
        }
        test_from!(DiagnosticProperty::String, MetricValue::String, "Hi World".to_string());
        test_from!(DiagnosticProperty::Bytes, MetricValue::Bytes, vec![1, 2, 3]);
        test_from!(DiagnosticProperty::Int, MetricValue::Int, 3);
        test_from!(DiagnosticProperty::Int, MetricValue::Int, std::i64::MAX);
        test_from!(DiagnosticProperty::Int, MetricValue::Int, std::i64::MIN);
        test_from!(DiagnosticProperty::Uint, MetricValue::Int, 3);
        test_from_to!(DiagnosticProperty::Uint, MetricValue::Int, std::u64::MAX, -1);
        test_from!(DiagnosticProperty::Double, MetricValue::Float, 3.14);
        test_from!(DiagnosticProperty::Double, MetricValue::Float, std::f64::MAX);
        test_from!(DiagnosticProperty::Double, MetricValue::Float, std::f64::MIN);
        test_from!(DiagnosticProperty::Bool, MetricValue::Bool, true);
        test_from!(DiagnosticProperty::Bool, MetricValue::Bool, false);
        let diagnostic_array = ArrayContent::Values(vec![1.5, 2.5, 3.5]);
        test_from_to!(
            DiagnosticProperty::DoubleArray,
            MetricValue::Vector,
            diagnostic_array,
            vec![MetricValue::Float(1.5), MetricValue::Float(2.5), MetricValue::Float(3.5)]
        );
        let diagnostic_array = ArrayContent::Values(vec![1, 2, 3]);
        test_from_to!(
            DiagnosticProperty::IntArray,
            MetricValue::Vector,
            diagnostic_array,
            vec![MetricValue::Int(1), MetricValue::Int(2), MetricValue::Int(3)]
        );
        let diagnostic_array = ArrayContent::Values(vec![1, 2, 3]);
        test_from_to!(
            DiagnosticProperty::UintArray,
            MetricValue::Vector,
            diagnostic_array,
            vec![MetricValue::Int(1), MetricValue::Int(2), MetricValue::Int(3)]
        );
    }
}
