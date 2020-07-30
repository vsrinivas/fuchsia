// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#[cfg(target_os = "linux")]
pub(crate) mod imp {
    use {
        crate::constants::{SSH_PRIV, SSH_PUB},
        crate::heuristic_config::HeuristicFn,
        crate::heuristic_fns::find_ssh_keys,
        std::collections::HashMap,
    };

    pub(crate) fn heuristics() -> HashMap<&'static str, HeuristicFn> {
        let mut heuristics = HashMap::<&str, HeuristicFn>::new();
        heuristics.insert(SSH_PUB, find_ssh_keys);
        heuristics.insert(SSH_PRIV, find_ssh_keys);
        heuristics
    }
}
