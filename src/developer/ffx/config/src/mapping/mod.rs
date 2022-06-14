// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Result},
    regex::{Captures, Regex},
    serde_json::{Map, Value},
    std::path::PathBuf,
};

pub(crate) mod cache;
pub(crate) mod config;
pub(crate) mod data;
pub(crate) mod env_var;
pub(crate) mod file_check;
pub(crate) mod filter;
pub(crate) mod flatten;
pub(crate) mod home;
pub(crate) mod identity;
pub(crate) mod runtime;

// Negative lookbehind (or lookahead for that matter) is not supported in Rust's regex.
// Instead, replace with this string - which hopefully will not be used by anyone in the
// configuration.  Insert joke here about how hope is not a strategy.
const TEMP_REPLACE: &str = "#<#ffx!!replace#>#";

pub(crate) fn preprocess(value: &Value) -> Option<String> {
    value.as_str().map(|s| s.to_string()).map(|s| s.replace("$$", TEMP_REPLACE))
}

pub(crate) fn postprocess(value: String) -> Value {
    Value::String(value.to_string().replace(TEMP_REPLACE, "$"))
}

pub(crate) fn replace_regex<T>(value: &String, regex: &Regex, replacer: T) -> String
where
    T: Fn(&str) -> Result<String>,
{
    regex
        .replace_all(value, |caps: &Captures<'_>| {
            // Skip the first one since that'll be the whole string.
            caps.iter()
                .skip(1)
                .map(|cap| cap.map(|c| replacer(c.as_str())))
                .fold(String::new(), |acc, v| if let Some(Ok(s)) = v { acc + &s } else { acc })
        })
        .into_owned()
}

pub(crate) fn replace<'a, T, P>(
    regex: &'a Regex,
    base_path: P,
    next: &'a T,
) -> Box<dyn Fn(Value) -> Option<Value> + Send + Sync + 'a>
where
    T: Fn(Value) -> Option<Value> + Sync,
    P: Fn() -> Result<PathBuf> + Sync + Send + 'a,
{
    Box::new(move |value| -> Option<Value> {
        match preprocess(&value)
            .as_ref()
            .map(|s| {
                replace_regex(s, regex, |v| {
                    match base_path() {
                        Ok(p) => Ok(p.to_str().map_or(v.to_string(), |s| s.to_string())),
                        Err(_) => Ok(v.to_string()), //just pass through
                    }
                })
            })
            .map(postprocess)
        {
            Some(v) => next(v),
            None => next(value),
        }
    })
}

pub(crate) fn nested_map<T: Fn(Value) -> Option<Value>>(
    cur: Option<Value>,
    mapper: &T,
) -> Option<Value> {
    cur.and_then(|c| {
        if let Value::Object(map) = c {
            let mut result = Map::new();
            for (key, value) in map.iter() {
                let new_value = if value.is_object() {
                    nested_map(map.get(key).cloned(), mapper)
                } else {
                    map.get(key).cloned().and_then(|v| mapper(v))
                };
                if let Some(new_value) = new_value {
                    result.insert(key.to_string(), new_value);
                }
            }
            if result.len() == 0 {
                None
            } else {
                Some(Value::Object(result))
            }
        } else {
            mapper(c)
        }
    })
}

pub(crate) fn nested_get<T: Fn(Value) -> Option<Value>>(
    cur: &Option<Value>,
    key: &str,
    remaining_keys: &[&str],
    mapper: &T,
) -> Option<Value> {
    cur.as_ref().and_then(|c| {
        if remaining_keys.len() == 0 {
            nested_map(c.get(key).cloned(), mapper)
        } else {
            nested_get(&c.get(key).cloned(), remaining_keys[0], &remaining_keys[1..], mapper)
        }
    })
}

pub(crate) fn nested_set(
    cur: &mut Map<String, Value>,
    key: &str,
    remaining_keys: &[&str],
    value: Value,
) -> bool {
    if remaining_keys.len() == 0 {
        // Exit early if the value hasn't changed.
        if let Some(old_value) = cur.get(key) {
            if old_value == &value {
                return false;
            }
        }
        cur.insert(key.to_string(), value);
        true
    } else {
        match cur.get(key) {
            Some(value) => {
                if !value.is_object() {
                    // Any literals will be overridden.
                    cur.insert(key.to_string(), Value::Object(Map::new()));
                }
            }
            None => {
                cur.insert(key.to_string(), Value::Object(Map::new()));
            }
        }
        // Just ensured this would be the case.
        let next_map = cur
            .get_mut(key)
            .expect("unable to get configuration")
            .as_object_mut()
            .expect("Unable to set configuration value as map");
        nested_set(next_map, remaining_keys[0], &remaining_keys[1..], value)
    }
}

pub(crate) fn nested_remove(
    cur: &mut Map<String, Value>,
    key: &str,
    remaining_keys: &[&str],
) -> Result<()> {
    if remaining_keys.len() == 0 {
        cur.remove(&key.to_string()).ok_or(anyhow!("Config key not found")).map(|_| ())
    } else {
        match cur.get(key) {
            Some(value) => {
                if !value.is_object() {
                    bail!("Configuration literal found when expecting a map.")
                }
            }
            None => {
                bail!("Configuration key not found.");
            }
        }
        // Just ensured this would be the case.
        let next_map = cur
            .get_mut(key)
            .expect("unable to get configuration")
            .as_object_mut()
            .expect("Unable to set configuration value as map");
        nested_remove(next_map, remaining_keys[0], &remaining_keys[1..])
    }
}
