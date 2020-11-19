// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{ConfigFile, TestData};

// If we don't include at least a {} then the reader library re-fetches instantly
// and spoils the test.
const INSPECT_EMPTY: &str = "[ {} ]";

const CONFIG: &str = r#"
{
    select: {
        // Need a selector or it won't try to get Inspect data and the test won't terminate.
        meaningless: "INSPECT:missing:root:missing",
    },
    act: {
        always: {
            trigger: "3 > 2",
            type: "Snapshot",
            repeat: "Seconds(1)",
            signature: "yes"
        }
    }
}
"#;

pub fn test_with_enable() -> TestData {
    let config = ConfigFile { name: "file.triage".to_string(), contents: CONFIG.to_string() };
    let enable = ConfigFile {
        name: "config.json".to_string(),
        contents: "{enable_filing: true}".to_string(),
    };
    TestData {
        name: "With Enable".to_string(),
        inspect_data: vec![INSPECT_EMPTY.to_string()],
        config_files: vec![config, enable],
        snapshots: vec![vec!["fuchsia-detect-yes".to_string()]],
        bails: false,
    }
}

pub fn test_bad_enable() -> TestData {
    let config = ConfigFile { name: "file.triage".to_string(), contents: CONFIG.to_string() };
    let disable =
        ConfigFile { name: "config.json".to_string(), contents: "{enable_filing: 1}".to_string() };
    TestData {
        name: "Bad Format Enable".to_string(),
        // Detect should never try to fetch Inspect data. If it does, providing
        // data will ensure that the test fails.
        inspect_data: vec![INSPECT_EMPTY.to_string()],
        config_files: vec![config, disable],
        snapshots: vec![],
        bails: true,
    }
}

pub fn test_false_enable() -> TestData {
    let config = ConfigFile { name: "file.triage".to_string(), contents: CONFIG.to_string() };
    let disable = ConfigFile {
        name: "config.json".to_string(),
        contents: "{enable_filing: false}".to_string(),
    };
    TestData {
        name: "Bad Format Enable".to_string(),
        // Detect will fetch data and decide that it would have filed, but will not file.
        inspect_data: vec![INSPECT_EMPTY.to_string()],
        config_files: vec![config, disable],
        snapshots: vec![vec![]],
        bails: false,
    }
}

pub fn test_no_enable() -> TestData {
    let config = ConfigFile { name: "file.triage".to_string(), contents: CONFIG.to_string() };
    let disable = ConfigFile { name: "config.json".to_string(), contents: "{}".to_string() };
    TestData {
        name: "Without Enable".to_string(),
        // Detect will fetch data and decide that it would have filed, but will not file.
        inspect_data: vec![INSPECT_EMPTY.to_string()],
        config_files: vec![config, disable],
        snapshots: vec![vec![]],
        bails: false,
    }
}

pub fn test_without_file() -> TestData {
    let config = ConfigFile { name: "file.triage".to_string(), contents: CONFIG.to_string() };
    TestData {
        name: "Without Program Config File".to_string(),
        // Detect will fetch data and decide that it would have filed, but will not file.
        inspect_data: vec![INSPECT_EMPTY.to_string()],
        config_files: vec![config],
        snapshots: vec![vec![]],
        bails: false,
    }
}
