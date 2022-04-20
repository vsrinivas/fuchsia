// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    serde::{Deserialize, Serialize},
    std::cmp::{Eq, PartialEq},
    std::fmt::Debug,
};

#[derive(Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct TestList {
    pub tests: Vec<TestListEntry>,
}

#[derive(Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct TestListEntry {
    // The name of the test.
    // MUST BE unique within the test list file.
    pub name: String,

    // Arbitrary labels for this test list.
    // No format requirements are imposed on these labels,
    // but for GN this is typically a build label.
    pub labels: Vec<String>,

    // Arbitrary tags for this test suite.
    pub tags: Vec<TestTag>,

    // Instructions for how to execute this test.
    // If missing, this test cannot be executed by ffx test.
    pub execution: Option<ExecutionEntry>,
}

#[derive(Debug, Eq, PartialEq, Serialize, Deserialize, PartialOrd, Ord, Clone)]
pub struct TestTag {
    pub key: String,
    pub value: String,
}

#[derive(Debug, Eq, PartialEq, Serialize, Deserialize)]
#[non_exhaustive]
#[serde(tag = "type")]
pub enum ExecutionEntry {
    #[serde(rename = "fuchsia_component")]
    FuchsiaComponent(FuchsiaComponentExecutionEntry),
}

#[derive(Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct FuchsiaComponentExecutionEntry {
    pub component_url: String,

    #[serde(default = "Vec::new")]
    pub test_args: Vec<String>,

    pub timeout_seconds: Option<std::num::NonZeroU32>,

    pub test_filters: Option<Vec<String>>,

    #[serde(default)]
    pub also_run_disabled_tests: bool,

    pub parallel: Option<u16>,

    pub max_severity_logs: Option<diagnostics_data::Severity>,
}
