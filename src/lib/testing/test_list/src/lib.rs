// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    serde::{Deserialize, Serialize},
    std::cmp::{Eq, PartialEq},
    std::fmt::Debug,
};

#[derive(Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(tag = "schema_id")]
pub enum TestList {
    #[serde(rename = "experimental")]
    Experimental { data: Vec<TestListEntry> },
}

#[derive(Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct TestListEntry {
    /// The name of the test.
    ///
    /// MUST BE unique within the test list file.
    pub name: String,

    /// Arbitrary labels for this test list.
    ///
    /// No format requirements are imposed on these labels,
    /// but for GN this is typically a build label.
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

impl TestTag {
    pub fn new(key: impl Into<String>, value: impl Into<String>) -> Self {
        Self { key: key.into(), value: value.into() }
    }
}

#[derive(Debug, Eq, PartialEq, Serialize, Deserialize)]
#[non_exhaustive]
#[serde(tag = "type")]
pub enum ExecutionEntry {
    #[serde(rename = "fuchsia_component")]
    /// The test is executed as a Fuchsia component on a target device.
    FuchsiaComponent(FuchsiaComponentExecutionEntry),
}

#[derive(Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct FuchsiaComponentExecutionEntry {
    /// The URL of the component to execute.
    pub component_url: String,

    /// Command line arguments passed to the test for execution.
    #[serde(default = "Vec::new")]
    pub test_args: Vec<String>,

    /// The number of seconds to wait before the test is killed and marked "timed out".
    pub timeout_seconds: Option<std::num::NonZeroU32>,

    /// Filters for which test cases in the suite to execute.
    pub test_filters: Option<Vec<String>>,

    /// If true, test cases in the suite marked "disabled" will be run anyway.
    #[serde(default)]
    pub also_run_disabled_tests: bool,

    /// The number of test cases to run in parallel.
    ///
    /// This value is a hint to the test runner, which is free to
    /// override the preference. If unset, a value is chosen by the
    /// test runner.
    pub parallel: Option<u16>,

    /// The maximum severity of logs the test is allowed to write.
    ///
    /// This may be used to catch log spam from components by ensuring
    /// that all logging during test execution is equal to or below
    /// this level.
    pub max_severity_logs: Option<diagnostics_data::Severity>,
}
