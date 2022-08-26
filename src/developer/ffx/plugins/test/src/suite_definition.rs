// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    run_test_suite_lib::TestParams,
    std::io::Read,
    test_list::{ExecutionEntry, FuchsiaComponentExecutionEntry, TestList, TestListEntry},
};

#[derive(Default)]
pub struct TestParamsOptions {
    // If set, filter out all tests that cannot be executed by this binary.
    // If not set, such tests result in an error.
    pub ignore_test_without_known_execution: bool,
}

pub fn test_params_from_reader<R: Read>(
    reader: R,
    options: TestParamsOptions,
) -> Result<Vec<TestParams>> {
    let test_list: TestList = serde_json::from_reader(reader).map_err(anyhow::Error::from)?;
    let TestList::Experimental { data } = test_list;
    if options.ignore_test_without_known_execution {
        Ok(data
            .into_iter()
            .filter_map(|entry| maybe_convert_test_list_entry_to_test_params(entry).ok())
            .collect())
    } else {
        data.into_iter()
            .map(maybe_convert_test_list_entry_to_test_params)
            .collect::<Result<Vec<TestParams>, anyhow::Error>>()
    }
}

fn maybe_convert_test_list_entry_to_test_params(entry: TestListEntry) -> Result<TestParams> {
    let TestListEntry { tags, execution, name, .. } = entry;

    match execution {
        Some(ExecutionEntry::FuchsiaComponent(component_execution)) => {
            let FuchsiaComponentExecutionEntry {
                component_url,
                test_args,
                timeout_seconds,
                test_filters,
                also_run_disabled_tests,
                parallel,
                max_severity_logs,
            } = component_execution;

            Ok(TestParams {
                test_url: component_url,
                test_args,
                timeout_seconds,
                test_filters,
                also_run_disabled_tests,
                show_full_moniker: false,
                parallel,
                max_severity_logs,
                tags,
            })
        }
        _ => Err(format_err!(
            "Cannot execute {name}, only \"fuchsia_component\" test execution is supported."
        )),
    }
}

#[cfg(test)]
mod test {
    use super::*;

    const VALID_JSON: &'static str = r#"
    {
        "schema_id": "experimental",
        "data": [
            {
                "name": "test",
                "labels": [],
                "tags": [],
                "execution": {
                    "type": "fuchsia_component",
                    "component_url": "fuchsia.com"
                }
            }
        ]
    }
    "#;

    const CONTAINS_VALID_AND_INVALID: &'static str = r#"
    {
        "schema_id": "experimental",
        "data": [
            {
                "name": "test",
                "labels": [],
                "tags": [],
                "execution": {
                    "type": "fuchsia_component",
                    "component_url": "fuchsia.com"
                }
            },
            {
                "name": "test3",
                "labels": [],
                "tags": []
            }
        ]
    }
    "#;

    #[test]
    fn test_params_from_reader_valid() {
        let reader = VALID_JSON.as_bytes();
        let test_params = test_params_from_reader(
            reader,
            TestParamsOptions { ignore_test_without_known_execution: false },
        )
        .expect("read file");
        assert_eq!(1, test_params.len());
    }

    #[test]
    fn test_params_from_reader_invalid() {
        let reader = CONTAINS_VALID_AND_INVALID.as_bytes();
        let test_params = test_params_from_reader(
            reader,
            TestParamsOptions { ignore_test_without_known_execution: false },
        );
        assert!(test_params.is_err());
    }

    #[test]
    fn test_params_from_reader_invalid_skipped() {
        let reader = CONTAINS_VALID_AND_INVALID.as_bytes();
        let test_params = test_params_from_reader(
            reader,
            TestParamsOptions { ignore_test_without_known_execution: true },
        )
        .expect("should have ignored errors");
        assert_eq!(1, test_params.len());
    }
}
