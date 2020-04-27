// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#[cfg(target_os = "linux")]
pub(crate) mod imp {
    use {
        crate::config::heuristic_config::HeuristicFn,
        crate::constants::{LOG_DIR, LOG_ENABLED, SSH_PRIV, SSH_PUB},
        serde_json::Value,
        std::{collections::HashMap, path::Path},
    };

    pub(crate) fn heuristics() -> HashMap<&'static str, HeuristicFn> {
        let mut heuristics = HashMap::<&str, HeuristicFn>::new();
        heuristics.insert(SSH_PUB, find_ssh_keys);
        heuristics.insert(SSH_PRIV, find_ssh_keys);
        heuristics
    }

    pub(crate) fn env_vars() -> HashMap<&'static str, Vec<&'static str>> {
        let mut environment_variables = HashMap::new();
        environment_variables.insert(LOG_DIR, vec!["FFX_LOG_DIR", "HOME", "HOMEPATH"]);
        environment_variables.insert(LOG_ENABLED, vec!["FFX_LOG_ENABLED"]);
        environment_variables
    }

    fn find_ssh_keys(key: &str) -> Option<Value> {
        let k = if key == SSH_PUB { "authorized_keys" } else { "pkey" };
        match std::env::var("FUCHSIA_DIR") {
            Ok(r) => {
                if Path::new(&r).exists() {
                    return Some(Value::String(String::from(format!("{}/.ssh/{}", r, k))));
                }
            }
            Err(_) => {
                if key != SSH_PUB {
                    return None;
                }
            }
        }
        match std::env::var("HOME") {
            Ok(r) => {
                if Path::new(&r).exists() {
                    Some(Value::String(String::from(format!("{}/.ssh/id_rsa.pub", r))))
                } else {
                    None
                }
            }
            Err(_) => None,
        }
    }
}
