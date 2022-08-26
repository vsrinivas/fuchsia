// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::EnvironmentContext;
use serde_json::Value;

/// Maps values that are empty (ie. empty arrays) to None.
pub(crate) fn filter(_ctx: &EnvironmentContext, value: Value) -> Option<Value> {
    if let Value::Array(values) = &value {
        if values.len() == 0 {
            return None;
        }
    }
    Some(value)
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use super::*;
    use crate::ConfigMap;
    use serde_json::json;

    #[test]
    fn test_returns_all() {
        let ctx = EnvironmentContext::isolated("/tmp".into(), ConfigMap::default(), None);
        let test = json!(["test1", "test2"]);
        let result = filter(&ctx, test);
        assert_eq!(result, Some(json!(["test1", "test2"])));
    }

    #[test]
    fn test_returns_value_if_not_array() {
        let ctx = EnvironmentContext::isolated("/tmp".into(), ConfigMap::default(), None);
        let test = Value::Bool(false);
        let result = filter(&ctx, test);
        assert_eq!(result, Some(Value::Bool(false)));
    }
}
