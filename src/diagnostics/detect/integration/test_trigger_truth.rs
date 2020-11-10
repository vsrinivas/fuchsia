// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{ConfigFile, TestData};

const INSPECT_2: &str = r#"
[
    {
        "data_source": "Inspect",
        "metadata": {
          "errors": null,
          "filename": "namespace/whatever",
          "component_url": "some-component:///#meta/something.cm",
          "timestamp": 1233474285373
        },
        "moniker": "foo/bar",
        "payload": {
          "root": {
            "widgets": 2
          }
        },
        "version": 1
    }

]
"#;

const INSPECT_3: &str = r#"
[
    {
        "data_source": "Inspect",
        "metadata": {
          "errors": null,
          "filename": "namespace/whatever",
          "component_url": "some-component:///#meta/something.cm",
          "timestamp": 1233474285373
        },
        "moniker": "foo/bar",
        "payload": {
          "root": {
            "widgets": 3
          }
        },
        "version": 1
    }

]
"#;

const CONFIG: &str = r#"
{
    select: {
        widgets: "INSPECT:foo/bar:root:widgets",
    },
    act: {
        should_fire: {
            trigger: "widgets > 2",
            type: "Snapshot",
            repeat: "Seconds(1)",
            signature: "widgets-over-2"
        }
    }
}
"#;

pub fn test() -> TestData {
    let config = ConfigFile { name: "file.config".to_string(), contents: CONFIG.to_string() };
    let enable = ConfigFile {
        name: "config.json".to_string(),
        contents: "{enable_filing: true}".to_string(),
    };
    TestData {
        name: "Trigger truth".to_string(),
        inspect_data: vec![INSPECT_3.to_string(), INSPECT_2.to_string(), INSPECT_3.to_string()],
        config_files: vec![config, enable],
        snapshots: vec![
            vec!["fuchsia-detect-widgets-over-2".to_string()],
            vec![],
            vec!["fuchsia-detect-widgets-over-2".to_string()],
        ],
        bails: false,
    }
}
