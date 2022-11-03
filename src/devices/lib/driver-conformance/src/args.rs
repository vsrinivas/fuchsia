// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    regex::Regex,
    std::{fmt, str::FromStr},
};

/// Custom type to store a list of test URLs.
#[derive(FromArgs, Default, Clone, PartialEq, Debug)]
pub struct TestList {
    #[argh(positional)]
    pub list: Vec<String>,
}

impl FromStr for TestList {
    type Err = anyhow::Error;

    fn from_str(value: &str) -> Result<TestList, Self::Err> {
        let params: Vec<_> = value.split(",").map(|test| String::from(test)).collect();
        let re = Regex::new(r"fuchsia-pkg://.+/.+#meta/.+").unwrap();
        for param in params.iter() {
            if !re.is_match(&param.as_str()) {
                return Err(anyhow::anyhow!(
                    "'{}' does not appear to be a fuchsia-pkg URL.",
                    param
                ));
            }
        }
        Ok(TestList { list: params })
    }
}

impl fmt::Display for TestList {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        for test in self.list.iter() {
            writeln!(f, "{}", test)?;
        }
        Ok(())
    }
}

/// Download or run driver conformance tests.
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "conformance",
    example = "To run all tests for a given driver:

$ ffx driver conformance test --driver fuchsia-boot:///#driver/my-driver.so

To run all tests for a given device:

$ ffx driver conformance test --device pci-00:05.0-fidl/my-device

To run all tests of a given type(s) for a given driver:

$ ffx driver conformance test --driver fuchsia-boot:///#driver/my-driver.so --types functional,performance

To run an arbitrary test(s) against a given driver:

$ ffx driver conformance test --driver fuchsia-boot:///#driver/my-driver.so --tests fuchsia-pkg://fuchsia.com/my-test#meta/my-test.cm"
)]
pub struct ConformanceCommand {
    #[argh(subcommand)]
    pub subcommand: ConformanceSubCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum ConformanceSubCommand {
    Test(TestCommand),
}

/// Runs driver conformance tests.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "test")]
pub struct TestCommand {
    /// device topological path. e.g. pci-00:05.0-fidl/my-device
    #[argh(option)]
    pub device: Option<String>,

    /// driver libname. e.g. fuchsia-boot:///#driver/my-driver.so
    #[argh(option)]
    pub driver: Option<String>,

    /// path to FHCP metadata file.
    #[argh(option)]
    pub metadata_path: Option<String>,

    /// WIP. comma-separated list of test types. e.g. performance,functional
    #[argh(option)]
    pub types: Option<String>,

    /// WIP. comma-separated list of test categories. e.g. imaging::camera,usb
    #[argh(option)]
    pub categories: Option<String>,

    /// comma-separated list of test components. e.g. fuchsia-pkg://fuchsia.dev/sometest#meta/sometest.cm,fuchsia-pkg://fuchsia.dev/test2#meta/othertest.cm
    #[argh(option)]
    pub tests: Option<TestList>,

    /// WIP. local directory storing the resources required for offline testing.
    #[argh(option)]
    pub cache: Option<String>,
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_test_list_from_str_ok() {
        let single_input = "fuchsia-pkg://fuchsia.com/fake-test#meta/fake-test.cm";
        let single = TestList::from_str(single_input).unwrap();
        assert_eq!(single.list.len(), 1);
        assert!(
            single.list.contains(&single_input.to_string()),
            "Did not find an item: {}",
            single_input
        );

        let multiple_input = vec![
            "fuchsia-pkg://fuchsia.com/fake-test#meta/fake-test.cm",
            "fuchsia-pkg://fuchsia.com/flake-test#meta/flake-test.cm",
            "fuchsia-pkg://fuchsia.com/bake-test#meta/bake-test.cm",
        ];
        let multiple_csv =
            format!("{},{},{}", multiple_input[0], multiple_input[1], multiple_input[2]);
        let multiple = TestList::from_str(multiple_csv.as_str()).unwrap();
        assert_eq!(multiple.list.len(), 3);
        for e in multiple_input.iter() {
            assert!(
                multiple.list.contains(&e.to_string()),
                "Did not find an item: {}",
                e.to_string()
            );
        }
    }

    #[test]
    fn test_test_list_from_str_err() {
        let very_wrong = "abc";
        match TestList::from_str(very_wrong) {
            Ok(_) => assert!(false, "This call should not pass."),
            Err(e) => assert_eq!(e.to_string(), "'abc' does not appear to be a fuchsia-pkg URL."),
        }

        // Slight mismatch: meta -> mata
        let slightly_wrong = "fuchsia-pkg://some.domain/foobar#mata/foobar.cm";
        match TestList::from_str(slightly_wrong) {
            Ok(_) => assert!(false, "This call should not pass."),
            Err(e) => assert_eq!(e.to_string(), "'fuchsia-pkg://some.domain/foobar#mata/foobar.cm' does not appear to be a fuchsia-pkg URL."),
        }

        let multiple_csv = format!(
            "{},{},{}",
            "fuchsia-pkg://fuchsia.com/fake-test#meta/fake-test.cm", "def", "ghi"
        );
        match TestList::from_str(multiple_csv.as_str()) {
            Ok(_) => assert!(false, "This call should not pass."),
            Err(e) => assert_eq!(e.to_string(), "'def' does not appear to be a fuchsia-pkg URL."),
        }
    }
}
