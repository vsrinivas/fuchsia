// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::api::ReadConfig,
    serde_json::Value,
    std::{collections::HashMap, fmt},
};

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
    fn get(&self, key: &str, _mapper: fn(Option<Value>) -> Option<Value>) -> Option<Value> {
        self.heuristics.get(key).map(|r| r(key)).flatten()
    }
}

impl fmt::Display for Heuristic<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "Configuration calculated from the environment when ffx is run.\n")?;
        if self.heuristics.len() == 0 {
            writeln!(f, "none")?;
        } else {
            self.heuristics.iter().try_for_each(|(key, value)| {
                if let Some(v) = value(key) {
                    writeln!(f, "\"{}\" = {}", key, v)
                } else {
                    Ok(())
                }
            })?;
        }
        writeln!(f, "")
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use regex::Regex;
    use std::convert::identity;

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
        assert_eq!(None, config.get(missing_key, identity));
        assert_eq!(
            Some(Value::String(heuristic_key.to_string())),
            config.get(heuristic_key, identity)
        );
        assert_eq!(
            Some(Value::String(heuristic_key_2.to_string())),
            config.get(heuristic_key_2, identity)
        );
    }

    #[test]
    fn test_display() {
        let (heuristic_key, heuristic_key_2) = ("test", "test_2");
        let mut heuristics = HashMap::<&str, HeuristicFn>::new();
        heuristics.insert(heuristic_key, test_heuristic);
        heuristics.insert(heuristic_key_2, test_heuristic);

        let config = Heuristic::new(&heuristics);
        let output = format!("{}", config);
        let h_test_1 = format!("\"{}\" = \"{}\"", heuristic_key, heuristic_key);
        let h_reg_1 = Regex::new(&h_test_1).expect("test regex");
        assert_eq!(1, h_reg_1.find_iter(&output).count(), "{}", output);
        let h_test_2 = format!("\"{}\" = \"{}\"", heuristic_key_2, heuristic_key_2);
        let h_reg_2 = Regex::new(&h_test_2).expect("test regex");
        assert_eq!(1, h_reg_2.find_iter(&output).count());
    }
}
