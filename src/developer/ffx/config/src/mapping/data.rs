// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::get_data_base_path,
    crate::mapping::{postprocess, preprocess, replace},
    lazy_static::lazy_static,
    regex::Regex,
    serde_json::Value,
};

pub(crate) fn data<'a, T: Fn(Value) -> Option<Value> + Sync>(
    next: &'a T,
) -> Box<dyn Fn(Value) -> Option<Value> + Send + Sync + 'a> {
    lazy_static! {
        static ref REGEX: Regex = Regex::new(r"\$(DATA)").unwrap();
    }

    Box::new(move |value| -> Option<Value> {
        match preprocess(&value)
            .as_ref()
            .map(|s| {
                replace(s, &*REGEX, |v| {
                    match get_data_base_path() {
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

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use super::*;
    use crate::mapping::identity::identity;

    fn data_dir(default: &str) -> String {
        match get_data_base_path() {
            Ok(p) => p.to_str().map_or(default.to_string(), |s| s.to_string()),
            Err(_) => default.to_string(),
        }
    }

    #[test]
    fn test_mapper() {
        let value = data_dir("$DATA");
        let test = Value::String("$DATA".to_string());
        assert_eq!(data(&identity)(test), Some(Value::String(value.to_string())));
    }

    #[test]
    fn test_mapper_multiple() {
        let value = data_dir("$DATA");
        let test = Value::String("$DATA/$DATA".to_string());
        assert_eq!(data(&identity)(test), Some(Value::String(format!("{}/{}", value, value))));
    }

    #[test]
    fn test_mapper_returns_pass_through() {
        let test = Value::String("$WHATEVER".to_string());
        assert_eq!(data(&identity)(test), Some(Value::String("$WHATEVER".to_string())));
    }
}
