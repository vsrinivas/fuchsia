// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::api::ReadConfig, serde_json::Value, std::collections::HashMap};

pub(crate) type HeuristicFn = fn(key: &str) -> Option<Value>;

pub(crate) struct Heuristic<'a> {
    heuristics: &'a HashMap<&'static str, HeuristicFn>,
}

impl<'a> Heuristic<'a> {
    pub(crate) fn new(heuristics: &'a HashMap<&'static str, HeuristicFn>) -> Self {
        Self { heuristics }
    }
}

impl ReadConfig for Heuristic<'_> {
    fn get(&self, key: &str) -> Option<Value> {
        self.heuristics.get(key).map(|r| r(key)).flatten()
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;

    fn test_heuristic(key: &str) -> Option<Value> {
        Some(Value::String(key.to_string()))
    }

    #[test]
    fn test_config_heuristics() {
        let (heuristic_key, heuristic_key_2) = ("test", "test_2");
        let mut heuristics = HashMap::<&str, HeuristicFn>::new();
        heuristics.insert(heuristic_key, test_heuristic);
        heuristics.insert(heuristic_key_2, test_heuristic);

        let config = Heuristic::new(&heuristics);

        let missing_key = "whatever";

        assert_eq!(None, config.get(missing_key));
        assert_eq!(Some(Value::String(heuristic_key.to_string())), config.get(heuristic_key));
        assert_eq!(Some(Value::String(heuristic_key_2.to_string())), config.get(heuristic_key_2));
    }
}
