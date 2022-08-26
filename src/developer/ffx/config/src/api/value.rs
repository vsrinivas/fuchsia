// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::api::{
        query::{ConfigQuery, SelectMode},
        ConfigError,
    },
    crate::mapping::{filter, flatten},
    crate::nested::RecursiveMap,
    crate::EnvironmentContext,
    anyhow::anyhow,
    serde_json::{Map, Value},
    std::{
        convert::{From, TryFrom, TryInto},
        path::PathBuf,
    },
};

const ADDITIVE_RETURN_ERR: &str =
    "Additive mode can only be used with an array or Value return type.";
const _ADDITIVE_LEVEL_ERR: &str =
    "Additive mode can only be used if config level is not specified.";

#[derive(Debug)]
pub struct ConfigValue(pub(crate) Option<Value>);

// See RecursiveMap for why the value version is the main implementation.
impl RecursiveMap for ConfigValue {
    type Output = ConfigValue;
    fn recursive_map<T: Fn(&EnvironmentContext, Value) -> Option<Value>>(
        self,
        ctx: &EnvironmentContext,
        mapper: &T,
    ) -> ConfigValue {
        ConfigValue(self.0.recursive_map(ctx, mapper))
    }
}
impl RecursiveMap for &ConfigValue {
    type Output = ConfigValue;
    fn recursive_map<T: Fn(&EnvironmentContext, Value) -> Option<Value>>(
        self,
        ctx: &EnvironmentContext,
        mapper: &T,
    ) -> ConfigValue {
        ConfigValue(self.0.clone()).recursive_map(ctx, mapper)
    }
}

pub trait ValueStrategy {
    fn handle_arrays(ctx: &EnvironmentContext, value: Value) -> Option<Value> {
        flatten(ctx, value)
    }

    fn validate_query(query: &ConfigQuery<'_>) -> std::result::Result<(), ConfigError> {
        match query.select {
            SelectMode::First => Ok(()),
            SelectMode::All => Err(anyhow!(ADDITIVE_RETURN_ERR).into()),
        }
    }
}

impl From<ConfigValue> for Option<Value> {
    fn from(value: ConfigValue) -> Self {
        value.0
    }
}

impl From<Option<Value>> for ConfigValue {
    fn from(value: Option<Value>) -> Self {
        ConfigValue(value)
    }
}

impl ValueStrategy for Value {
    fn handle_arrays(_ctx: &EnvironmentContext, value: Value) -> Option<Value> {
        Some(value)
    }

    fn validate_query(_query: &ConfigQuery<'_>) -> std::result::Result<(), ConfigError> {
        Ok(())
    }
}

impl TryFrom<ConfigValue> for Value {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        value.0.ok_or(anyhow!("no value").into())
    }
}

impl ValueStrategy for Option<Value> {
    fn handle_arrays(_ctx: &EnvironmentContext, value: Value) -> Option<Value> {
        Some(value)
    }

    fn validate_query(_query: &ConfigQuery<'_>) -> std::result::Result<(), ConfigError> {
        Ok(())
    }
}

impl ValueStrategy for String {}

impl TryFrom<ConfigValue> for String {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        value
            .0
            .and_then(|v| v.as_str().map(|s| s.to_string()))
            .ok_or(anyhow!("no configuration String value found").into())
    }
}

impl ValueStrategy for Option<String> {}

impl TryFrom<ConfigValue> for Option<String> {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        Ok(value.0.and_then(|v| v.as_str().map(|s| s.to_string())))
    }
}

impl ValueStrategy for usize {}

impl TryFrom<ConfigValue> for usize {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        value
            .0
            .and_then(|v| {
                v.as_u64().and_then(|v| usize::try_from(v).ok()).or_else(|| {
                    if let Value::String(s) = v {
                        s.parse().ok()
                    } else {
                        None
                    }
                })
            })
            .ok_or(anyhow!("no configuration usize value found").into())
    }
}

impl ValueStrategy for u64 {}

impl TryFrom<ConfigValue> for u64 {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        value
            .0
            .and_then(|v| {
                v.as_u64().or_else(|| if let Value::String(s) = v { s.parse().ok() } else { None })
            })
            .ok_or(anyhow!("no configuration Number value found").into())
    }
}

impl ValueStrategy for Option<u64> {}

impl TryFrom<ConfigValue> for Option<u64> {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        Ok(value.0.and_then(|v| {
            v.as_u64().or_else(|| if let Value::String(s) = v { s.parse().ok() } else { None })
        }))
    }
}

impl ValueStrategy for u16 {}

impl TryFrom<ConfigValue> for u16 {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        value
            .0
            .and_then(|v| {
                v.as_u64().or_else(|| if let Value::String(s) = v { s.parse().ok() } else { None })
            })
            .and_then(|v| u16::try_from(v).ok())
            .ok_or(anyhow!("no configuration Number value found").into())
    }
}

impl ValueStrategy for i64 {}

impl TryFrom<ConfigValue> for i64 {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        value
            .0
            .and_then(|v| {
                v.as_i64().or_else(|| if let Value::String(s) = v { s.parse().ok() } else { None })
            })
            .ok_or(anyhow!("no configuration Number value found").into())
    }
}

impl ValueStrategy for bool {}

impl TryFrom<ConfigValue> for bool {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        value
            .0
            .and_then(|v| {
                v.as_bool().or_else(|| if let Value::String(s) = v { s.parse().ok() } else { None })
            })
            .ok_or(anyhow!("no configuration Boolean value found").into())
    }
}

impl ValueStrategy for PathBuf {}

impl TryFrom<ConfigValue> for PathBuf {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        value
            .0
            .and_then(|v| v.as_str().map(|s| PathBuf::from(s.to_string())))
            .ok_or(anyhow!("no configuration value found").into())
    }
}

impl ValueStrategy for Option<PathBuf> {}

impl TryFrom<ConfigValue> for Option<PathBuf> {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        Ok(value.0.and_then(|v| v.as_str().map(|s| PathBuf::from(s.to_string()))))
    }
}

impl<T: TryFrom<ConfigValue>> ValueStrategy for Vec<T> {
    fn handle_arrays(ctx: &EnvironmentContext, value: Value) -> Option<Value> {
        filter(ctx, value)
    }

    fn validate_query(_query: &ConfigQuery<'_>) -> std::result::Result<(), ConfigError> {
        Ok(())
    }
}

impl<T: TryFrom<ConfigValue>> TryFrom<ConfigValue> for Vec<T> {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        value
            .0
            .and_then(|val| match val.as_array() {
                Some(v) => {
                    let result: Vec<T> = v
                        .iter()
                        .filter_map(|i| ConfigValue(Some(i.clone())).try_into().ok())
                        .collect();
                    if result.len() > 0 {
                        Some(result)
                    } else {
                        None
                    }
                }
                None => ConfigValue(Some(val)).try_into().map(|x| vec![x]).ok(),
            })
            .ok_or(anyhow!("no configuration value found").into())
    }
}

impl ValueStrategy for Map<String, Value> {
    fn handle_arrays(ctx: &EnvironmentContext, value: Value) -> Option<Value> {
        filter(ctx, value)
    }

    fn validate_query(_query: &ConfigQuery<'_>) -> std::result::Result<(), ConfigError> {
        Ok(())
    }
}

impl TryFrom<ConfigValue> for Map<String, Value> {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        value
            .0
            .and_then(|x| x.as_object().cloned())
            .ok_or(anyhow!("no configuration value found").into())
    }
}

impl ValueStrategy for f64 {}

impl TryFrom<ConfigValue> for f64 {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        value
            .0
            .and_then(|v| {
                v.as_f64().or_else(|| if let Value::String(s) = v { s.parse().ok() } else { None })
            })
            .ok_or(anyhow!("no configuration Number value found").into())
    }
}

impl ValueStrategy for Option<f64> {}

impl TryFrom<ConfigValue> for Option<f64> {
    type Error = ConfigError;

    fn try_from(value: ConfigValue) -> std::result::Result<Self, Self::Error> {
        Ok(value.0.and_then(|v| {
            v.as_f64().or_else(|| if let Value::String(s) = v { s.parse().ok() } else { None })
        }))
    }
}

/// Merge's `Value` b into `Value` a.
pub fn merge(a: &mut Value, b: &Value) {
    match (a, b) {
        (&mut Value::Object(ref mut a), &Value::Object(ref b)) => {
            for (k, v) in b.iter() {
                self::merge(a.entry(k.clone()).or_insert(Value::Null), v);
            }
        }
        (a, b) => *a = b.clone(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn test_merge() {
        let mut proto = Value::Null;
        let a = json!({
            "list": [ "first", "second" ],
            "string": "This is a string",
            "object" : {
                "foo" : "foo-prime",
                "bar" : "bar-prime"
            }
        });
        let b = json!({
            "list": [ "third" ],
            "title": "This is a title",
            "otherObject" : {
                "yourHonor" : "I object!"
            }
        });
        merge(&mut proto, &a);
        assert_eq!(proto["list"].as_array().unwrap()[0].as_str().unwrap(), "first");
        assert_eq!(proto["list"].as_array().unwrap()[1].as_str().unwrap(), "second");
        assert_eq!(proto["string"].as_str().unwrap(), "This is a string");
        assert_eq!(proto["object"]["foo"].as_str().unwrap(), "foo-prime");
        assert_eq!(proto["object"]["bar"].as_str().unwrap(), "bar-prime");
        merge(&mut proto, &b);
        assert_eq!(proto["list"].as_array().unwrap()[0].as_str().unwrap(), "third");
        assert_eq!(proto["title"].as_str().unwrap(), "This is a title");
        assert_eq!(proto["string"].as_str().unwrap(), "This is a string");
        assert_eq!(proto["object"]["foo"].as_str().unwrap(), "foo-prime");
        assert_eq!(proto["object"]["bar"].as_str().unwrap(), "bar-prime");
        assert_eq!(proto["otherObject"]["yourHonor"].as_str().unwrap(), "I object!");
    }
}
