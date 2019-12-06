// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error, ResultExt},
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    futures::prelude::*,
    serde_derive::{Deserialize, Serialize},
    std::{ffi::CString, fs::File, io::BufReader, str::from_utf8},
};

/// This structure provides info about individual test cases.
/// Example: For test FOO.Bar, this contains info about Bar.
/// Please refer to documentation of `ListTestResult` for details.
#[derive(Serialize, Deserialize, Debug)]
struct IndividualTestInfo {
    pub name: String,
    pub file: String,
    pub line: u64,
}

/// This structure provides info about individual test suites.
/// Example: For test FOO.Bar, this contains info about FOO.
/// Please refer to documentation of `ListTestResult` for details.
#[derive(Serialize, Deserialize, Debug)]
struct TestSuiteResult {
    pub tests: usize,
    pub name: String,
    pub testsuite: Vec<IndividualTestInfo>,
}

/// Sample json will look like
/// ```
/// {
/// "tests": 6,
/// "name": "AllTests",
/// "testsuites": [
///    {
///      "name": "SimpleTest1",
///      "tests": 2,
///      "testsuite": [
///        {
///          "name": "Test1",
///          "file": "../../src/sys/test_adapters/gtest/test_data/simple_tests.cc",
///          "line": 7
///        },
///        {
///          "name": "Test2",
///          "file": "../../src/sys/test_adapters/gtest/test_data/simple_tests.cc",
///          "line": 9
///        }
///      ]
///    },
///  ]
///}
///```
#[derive(Serialize, Deserialize, Debug)]
struct ListTestResult {
    pub tests: usize,
    pub name: String,
    pub testsuites: Vec<TestSuiteResult>,
}

#[derive(Debug)]
pub struct GTestAdapter {
    c_test_path: CString,
    c_test_file_name: CString,
    test_file_name: String,
}

impl GTestAdapter {
    /// Creates a new GTest adapter if `test_path` is valid
    pub fn new(test_path: String) -> Result<GTestAdapter, Error> {
        let test_file_name = test_adapter_lib::extract_test_filename(&test_path)?;

        Ok(GTestAdapter {
            c_test_path: CString::new(&test_path[..])?,
            c_test_file_name: CString::new(&test_file_name[..])?,
            test_file_name: test_file_name,
        })
    }

    /// Launches test process and gets test list out. Returns list of tests names in the format
    /// defined by gtests, i.e FOO.Bar
    pub async fn enumerate_tests(&self) -> Result<Vec<String>, Error> {
        let test_list_file = format!("/tmp/{}_test_list.json", self.test_file_name);

        let (process, logger) = test_adapter_lib::launch_process(
            &self.c_test_path,
            &self.c_test_file_name,
            &[
                &self.c_test_file_name,
                &CString::new("--gtest_list_tests")?,
                &CString::new(format!("--gtest_output=json:{}", test_list_file))?,
            ],
        )?;

        fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
            .await
            .context("Error waiting for test process to exit")?;

        let process_info = process.info().context("Error getting info from process")?;

        if process_info.return_code != 0 {
            let logs = logger.try_concat().await?;
            let output = from_utf8(&logs)?;
            // TODO(anmittal): Add a error logger to API before porting this to runner so that we
            // can display test stdout logs.
            fx_log_err!("Failed getting list of tests:\n{}", output);
            return Err(format_err!("Can't get list of tests. check logs"));
        }

        // Open the file in read-only mode with buffer.
        let open_file_result = File::open(&test_list_file);
        if let Err(e) = open_file_result {
            let logs = logger.try_concat().await?;
            let output = from_utf8(&logs)?;
            fx_log_err!("Failed getting list of tests from {}:\n{}", test_list_file, output);
            return Err(e.into());
        }

        let output_file = open_file_result?;

        let reader = BufReader::new(output_file);

        let test_list: ListTestResult =
            serde_json::from_reader(reader).context("Can't get test from gtest")?;

        let mut tests = Vec::<String>::with_capacity(test_list.tests);

        for suite in &test_list.testsuites {
            for test in &suite.testsuite {
                tests.push(format!("{}.{}", suite.name, test.name))
            }
        }

        return Ok(tests);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn empty_test_file() {
        let adapter =
            GTestAdapter::new("/pkg/bin/no_tests".to_string()).expect("Cannot create adapter");
        let tests = adapter.enumerate_tests().await.expect("Can't enumerate tests");
        assert_eq!(tests.len(), 0, "got {:?}", tests);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn simple_test_file() {
        let adapter =
            GTestAdapter::new("/pkg/bin/simple_tests".to_string()).expect("Cannot create adapter");
        let tests = adapter.enumerate_tests().await.expect("Can't enumerate tests");
        assert_eq!(tests.len(), 6);
        assert_eq!(
            tests,
            vec![
                "SimpleTest1.Test1".to_owned(),
                "SimpleTest1.Test2".to_owned(),
                "SimpleTest2.Test1".to_owned(),
                "SimpleTest2.Test2".to_owned(),
                "SimpleTest2.Test3".to_owned(),
                "SimpleTest3.Test1".to_owned()
            ]
        );
    }

    #[test]
    fn invalid_file() {
        GTestAdapter::new("/pkg/bin/invalid_test_file".to_string()).expect_err("This should fail");
    }
}
