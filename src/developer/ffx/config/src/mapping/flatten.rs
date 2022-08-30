// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::EnvironmentContext;
use serde_json::Value;

/// Pick the first element of Value if it's an array.
pub(crate) fn flatten(_ctx: &EnvironmentContext, value: Value) -> Option<Value> {
    if let Value::Array(values) = value {
        values.into_iter().next()
    } else {
        Some(value)
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use super::*;
    use crate::ConfigMap;

    #[test]
    fn test_returns_first() {
        let ctx = EnvironmentContext::isolated(
            "/tmp".into(),
            Default::default(),
            ConfigMap::default(),
            None,
        );
        let test = Value::Array(vec![
            Value::String("test1".to_string()),
            Value::String("test2".to_string()),
        ]);
        let result = flatten(&ctx, test);
        assert_eq!(result, Some(Value::String("test1".to_string())));
    }

    #[test]
    fn test_returns_value_if_not_string() {
        let ctx = EnvironmentContext::isolated(
            "/tmp".into(),
            Default::default(),
            ConfigMap::default(),
            None,
        );
        let test = Value::Bool(false);
        let result = flatten(&ctx, test);
        assert_eq!(result, Some(Value::Bool(false)));
    }
}
