// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {run_test_suite_lib::TestParams, serde::Deserialize, serde_json::Error, std::io::Read};

/// Read Test Parameters specified as json from a reader.
pub fn test_params_from_reader<R: Read>(reader: R) -> Result<Vec<TestParams>, Error> {
    let test_params: Vec<SuiteDefinition> = serde_json::from_reader(reader)?;
    Ok(test_params.into_iter().map(Into::into).collect())
}

/// Definition of serializable options specified per test suite.
/// Note that this struct defines the json interface for the ffx test command and is
/// therefore part of its API.
#[derive(Deserialize)]
struct SuiteDefinition {
    test_url: String,
    #[serde(default = "Vec::new")]
    test_args: Vec<String>,
    timeout: Option<std::num::NonZeroU32>,
    test_filters: Option<Vec<String>>,
    #[serde(default)]
    also_run_disabled_tests: bool,
    parallel: Option<u16>,
    max_severity_logs: Option<diagnostics_data::Severity>,
}

impl From<SuiteDefinition> for TestParams {
    fn from(other: SuiteDefinition) -> Self {
        let SuiteDefinition {
            test_url,
            timeout,
            test_args,
            test_filters,
            also_run_disabled_tests,
            parallel,
            max_severity_logs,
        } = other;
        TestParams {
            test_url,
            test_args,
            timeout,
            test_filters,
            also_run_disabled_tests,
            parallel,
            max_severity_logs,
        }
    }
}
