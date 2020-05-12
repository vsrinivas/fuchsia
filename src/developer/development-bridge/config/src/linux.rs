// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#[cfg(target_os = "linux")]
pub(crate) mod imp {
    use {
        crate::heuristic_config::HeuristicFn,
        crate::heuristic_fns::find_ssh_keys,
        ffx_core::constants::{LOG_DIR, LOG_ENABLED, SSH_PORT, SSH_PRIV, SSH_PUB},
        std::collections::HashMap,
    };

    pub(crate) fn heuristics() -> HashMap<&'static str, HeuristicFn> {
        let mut heuristics = HashMap::<&str, HeuristicFn>::new();
        heuristics.insert(SSH_PUB, find_ssh_keys);
        heuristics.insert(SSH_PRIV, find_ssh_keys);
        heuristics
    }

    pub(crate) fn env_vars() -> HashMap<&'static str, Vec<&'static str>> {
        let mut environment_variables = HashMap::new();
        environment_variables.insert(SSH_PORT, vec!["FUCHSIA_SSH_PORT"]);
        environment_variables.insert(LOG_DIR, vec!["FFX_LOG_DIR", "HOME", "HOMEPATH"]);
        environment_variables.insert(LOG_ENABLED, vec!["FFX_LOG_ENABLED"]);
        environment_variables
    }
}
