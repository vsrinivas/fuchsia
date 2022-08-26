// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Functions for interacting with nested json objects in a recursive way.

use crate::EnvironmentContext;
use anyhow::{Context, Result};
use serde_json::{map::Entry, Map, Value};
use std::iter::FromIterator;

/// A trait that adds a recursive mapping function to a nested json value tree.
///
/// Note that implementations of this should be on value types and not references,
/// with perhaps an additional impl for a reference that clones. This is because most
/// of the time most values won't change, so it makes sense to copy the whole thing up
/// front and only have to build a new item if necessary.
///
/// This also makes it so you can efficiently chain [`RecursiveMap::recursive_map`]
/// calls together, since they will continue to use the same Value items until one
/// is overwritten.
pub(crate) trait RecursiveMap {
    /// The type of output that the filtering will return. That type must implement
    /// this trait as well.
    type Output: RecursiveMap;

    /// Filters values recursively through the function provided.
    fn recursive_map<T: Fn(&EnvironmentContext, Value) -> Option<Value>>(
        self,
        ctx: &EnvironmentContext,
        mapper: &T,
    ) -> Self::Output;
}

impl RecursiveMap for Value {
    type Output = Option<Value>;
    fn recursive_map<T: Fn(&EnvironmentContext, Value) -> Option<Value>>(
        self,
        ctx: &EnvironmentContext,
        mapper: &T,
    ) -> Option<Value> {
        match self {
            Value::Object(map) => {
                let mut result = Map::new();
                for (key, value) in map.into_iter() {
                    let new_value = if value.is_object() {
                        value.clone().recursive_map(ctx, mapper)
                    } else {
                        mapper(ctx, value.clone())
                    };
                    if let Some(new_value) = new_value.clone() {
                        result.insert(key.clone(), new_value);
                    }
                }
                if result.len() == 0 {
                    None
                } else {
                    mapper(ctx, Value::Object(result))
                }
            }
            Value::Array(arr) => {
                let result = Vec::from_iter(
                    arr.into_iter().filter_map(|value| value.recursive_map(ctx, mapper)),
                );
                if result.len() == 0 {
                    None
                } else {
                    mapper(ctx, Value::Array(result))
                }
            }
            other => mapper(ctx, other),
        }
    }
}

impl RecursiveMap for &Value {
    type Output = Option<Value>;
    fn recursive_map<T: Fn(&EnvironmentContext, Value) -> Option<Value>>(
        self,
        ctx: &EnvironmentContext,
        mapper: &T,
    ) -> Self::Output {
        self.clone().recursive_map(ctx, mapper)
    }
}
impl RecursiveMap for Option<&Value> {
    type Output = Option<Value>;
    fn recursive_map<T: Fn(&EnvironmentContext, Value) -> Option<Value>>(
        self,
        ctx: &EnvironmentContext,
        mapper: &T,
    ) -> Self::Output {
        self.and_then(|value| value.clone().recursive_map(ctx, mapper))
    }
}
impl RecursiveMap for Option<Value> {
    type Output = Option<Value>;
    fn recursive_map<T: Fn(&EnvironmentContext, Value) -> Option<Value>>(
        self,
        ctx: &EnvironmentContext,
        mapper: &T,
    ) -> Self::Output {
        self.and_then(|value| value.recursive_map(ctx, mapper))
    }
}

/// Search the given nested json value for the given `key`, and then recurse through
/// the rest of the tree for the `remaining_keys`. Returns the value at that position if found.
pub(crate) fn nested_get<'a>(
    cur: Option<&'a Map<String, Value>>,
    key: &str,
    remaining_keys: &[&str],
) -> Option<&'a Value> {
    cur.and_then(|cur| {
        if remaining_keys.len() == 0 {
            cur.get(key)
        } else {
            nested_get(
                cur.get(key).and_then(Value::as_object),
                remaining_keys[0],
                &remaining_keys[1..],
            )
        }
    })
}

/// Find `key` in `cur`, then recurisively search for the `remaining_keys` through the nested
/// object for the position to insert, creating Object entries as it goes if necessary (including
/// overwriting leaf values in the way). Sets the value if not already set to the same value,
/// and returns true if it did (or false if it already existed as the same value).
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
        if let Entry::Occupied(mut occupied) = cur.entry(key) {
            let val = occupied.get_mut();
            if let Value::Object(ref mut next_map) = val {
                nested_set(next_map, remaining_keys[0], &remaining_keys[1..], value)
            } else {
                let mut next_map = Map::new();
                nested_set(&mut next_map, remaining_keys[0], &remaining_keys[1..], value);
                *val = Value::Object(next_map);
                // since we're creating the rest of the tree, we know we either succeeded and inserted or failed and crashed.
                true
            }
        } else {
            let mut next_map = Map::new();
            nested_set(&mut next_map, remaining_keys[0], &remaining_keys[1..], value);
            cur.insert(key.to_string(), Value::Object(next_map));
            // since we're creating the rest of the tree, we know we either succeeded and inserted or failed and crashed.
            true
        }
    }
}

/// Searches the nested object to find the given `key`, then recursively through the `remaining_keys`,
/// and removes it if found. If the key or any of its parent keys don't exist, it will return an error.
pub(crate) fn nested_remove(
    cur: &mut Map<String, Value>,
    key: &str,
    remaining_keys: &[&str],
) -> Result<()> {
    if remaining_keys.len() == 0 {
        cur.remove(&key.to_string()).context("Config key not found").map(|_| ())
    } else {
        // Just ensured this would be the case.
        let next_map = cur
            .get_mut(key)
            .context("Configuration key not found.")?
            .as_object_mut()
            .context("Configuration literal found when expecting a map.")?;
        nested_remove(next_map, remaining_keys[0], &remaining_keys[1..])?;
        if next_map.len() == 0 {
            cur.remove(key).context("Current key not found trying to recursively remove")?;
        }
        Ok(())
    }
}
