// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {fuchsia_inspect as inspect, fuchsia_inspect_derive::Inspect};

#[derive(Inspect, Default, Debug)]
pub struct SamplerExecutorStats {
    pub total_project_samplers_configured: inspect::UintProperty,
    pub healthily_exited_samplers: inspect::UintProperty,
    pub errorfully_exited_samplers: inspect::UintProperty,
    pub reboot_exited_samplers: inspect::UintProperty,
    pub inspect_node: fuchsia_inspect::Node,
}

impl SamplerExecutorStats {
    pub fn new() -> Self {
        Self::default()
    }
}

#[derive(Inspect, Default, Debug)]
pub struct ProjectSamplerStats {
    // Total number of unique project samplers for this project.
    pub project_sampler_count: inspect::UintProperty,
    // Total number of configured metrics across all
    // project samplers for this project..
    pub metrics_configured: inspect::UintProperty,
    // Total number of cobalt logs sent on the behalf of this project.
    pub cobalt_logs_sent: inspect::UintProperty,
    inspect_node: fuchsia_inspect::Node,
}

impl ProjectSamplerStats {
    pub fn new() -> Self {
        Self::default()
    }
}
