// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#[cfg(not(target_os = "linux"))]
pub(crate) mod imp {
    use {crate::config::heuristic_config::HeuristicFn, std::collections::HashMap};

    pub(crate) fn heuristics() -> HashMap<&'static str, HeuristicFn> {
        HashMap::new()
    }

    pub(crate) fn env_vars() -> HashMap<&'static str, Vec<&'static str>> {
        HashMap::new()
    }
}
