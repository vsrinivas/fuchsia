// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub(crate) fn identity(value: serde_json::Value) -> Option<serde_json::Value> {
    Some(value)
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use super::*;
    use serde_json::Value;

    #[test]
    fn test_returns_first() {
        let test = Value::String("test1".to_string());
        let result = identity(test);
        assert_eq!(result, Some(Value::String("test1".to_string())));
    }
}
