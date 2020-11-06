// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{ConfigFile, TestData};

const INSPECT: &str = r#"
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

// Ensure that the "repeat" lines here are consistent with CHECK_PERIOD_SECONDS.
const CONFIG: &str = r#"
{
    select: {
        widgets: "INSPECT:foo/bar:root:widgets",
    },
    act: {
        fire_often: {
            trigger: "widgets > 2",
            type: "Snapshot",
            repeat: "Seconds(1)",
            signature: "frequently"
        },
        fire_rarely: {
            trigger: "widgets > 2",
            type: "Snapshot",
            repeat: "Seconds(6)",
            signature: "rarely"
        }
    }
}
"#;

pub fn test() -> TestData {
    let config = ConfigFile { name: "file.config".to_string(), contents: CONFIG.to_string() };
    TestData {
        name: "Snapshot throttle".to_string(),
        inspect_data: vec![INSPECT.to_string(), INSPECT.to_string(), INSPECT.to_string()],
        config_files: vec![config],
        crashes: vec![
            vec!["fuchsia-detect-frequently".to_string(), "fuchsia-detect-rarely".to_string()],
            vec!["fuchsia-detect-frequently".to_string()],
            vec!["fuchsia-detect-frequently".to_string(), "fuchsia-detect-rarely".to_string()],
        ],
    }
}
