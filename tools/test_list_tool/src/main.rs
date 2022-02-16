// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! tlg is the Test List Generator.

use {
    anyhow::Error,
    serde::{Deserialize, Serialize},
    serde_json,
    std::{
        cmp::{Eq, PartialEq},
        fmt::Debug,
        fs,
        io::Read,
        path::PathBuf,
    },
    structopt::StructOpt,
    test_list::{TestList, TestListEntry, TestTag},
};

mod opts;

#[derive(Debug, Eq, PartialEq, Serialize, Deserialize)]
struct TestsJsonEntry {
    test: TestEntry,
}

#[derive(Debug, Eq, PartialEq, Serialize, Deserialize)]
struct TestEntry {
    name: String,
    label: String,
    cpu: String,
    os: String,
}

fn test_list_from_tests_json(tests_json: Vec<TestsJsonEntry>) -> TestList {
    let mut test_list = TestList { tests: vec![] };
    for entry in tests_json {
        let t = entry.test;
        test_list.tests.push(TestListEntry {
            name: t.name,
            labels: vec![t.label],
            tags: vec![
                TestTag { key: "cpu".to_string(), value: t.cpu },
                TestTag { key: "os".to_string(), value: t.os },
            ],
        });
    }
    test_list
}

fn main() -> Result<(), Error> {
    run_tlg()
}

fn read_tests_json(file: &PathBuf) -> Result<Vec<TestsJsonEntry>, Error> {
    let mut buffer = String::new();
    fs::File::open(&file)?.read_to_string(&mut buffer)?;
    let t: Vec<TestsJsonEntry> = serde_json::from_str(&buffer)?;
    Ok(t)
}

fn run_tlg() -> Result<(), Error> {
    let opt = opts::Opt::from_args();
    opt.validate()?;
    let tests_json = read_tests_json(&opt.input)?;
    let test_list = test_list_from_tests_json(tests_json);
    let test_list_json = serde_json::to_string(&test_list)?;
    fs::write(opt.output, test_list_json)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {super::*, tempfile::tempdir};

    #[test]
    fn test_test_list_from_tests_json() {
        let tests_json = vec![TestsJsonEntry {
            test: TestEntry {
                name: "test-name".to_string(),
                label: "test-label".to_string(),
                cpu: "x64".to_string(),
                os: "linux".to_string(),
            },
        }];
        let tests_list = test_list_from_tests_json(tests_json);
        assert_eq!(
            tests_list,
            TestList {
                tests: vec![TestListEntry {
                    name: "test-name".to_string(),
                    labels: vec!["test-label".to_string()],
                    tags: vec![
                        TestTag { key: "cpu".to_string(), value: "x64".to_string() },
                        TestTag { key: "os".to_string(), value: "linux".to_string() },
                    ],
                },],
            }
        )
    }

    #[test]
    fn test_read_tests_json() {
        let data = r#"
            [
                {
                    "test": {
                        "cpu": "x64",
                        "label": "//build/components/tests:echo-integration-test_test_echo-client-test(//build/toolchain/fuchsia:x64)",
                        "log_settings": {
                            "max_severity": "WARN"
                        },
                        "name": "fuchsia-pkg://fuchsia.com/echo-integration-test#meta/echo-client-test.cm",
                        "os": "fuchsia",
                        "package_label": "//build/components/tests:echo-integration-test(//build/toolchain/fuchsia:x64)",
                        "package_manifests": [
                            "obj/build/components/tests/echo-integration-test/package_manifest.json"
                        ],
                        "package_url": "fuchsia-pkg://fuchsia.com/echo-integration-test#meta/echo-client-test.cm"
                    }
                }
            ]"#;
        let dir = tempdir().expect("failed to get tempdir");
        let tests_json_path = dir.path().join("tests.json");
        fs::write(&tests_json_path, data).expect("failed to write tests.json to tempfile");
        let tests_json = read_tests_json(&tests_json_path).expect("read_tests_json() failed");
        assert_eq!(
            tests_json,
            vec![
                TestsJsonEntry{
                    test: TestEntry{
                        name: "fuchsia-pkg://fuchsia.com/echo-integration-test#meta/echo-client-test.cm".to_string(),
                        label: "//build/components/tests:echo-integration-test_test_echo-client-test(//build/toolchain/fuchsia:x64)".to_string(),
                        cpu: "x64".to_string(),
                        os: "fuchsia".to_string(),
                    },
                }
            ],
        );
    }
}
