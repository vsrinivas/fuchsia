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
        messy_signature: {
            trigger: "widgets > 2",
            type: "Snapshot",
            repeat: "Seconds(1)",
            signature: "This_is $illy!, BᗷB♥B"
        },
        sentence_signature: {
            trigger: "widgets > 2",
            type: "Snapshot",
            repeat: "Seconds(1)",
            signature: "There was an error"
        }
    }
}
"#;

pub fn test() -> TestData {
    let config = ConfigFile { name: "file.triage".to_string(), contents: CONFIG.to_string() };
    let enable = ConfigFile {
        name: "config.json".to_string(),
        contents: "{enable_filing: true}".to_string(),
    };
    TestData {
        name: "Snapshot sanitizing".to_string(),
        inspect_data: vec![INSPECT.to_string()],
        config_files: vec![config, enable],
        snapshots: vec![vec![
            "fuchsia-detect-this-is--illy---b-b-b".to_string(),
            "fuchsia-detect-there-was-an-error".to_string(),
        ]],
        bails: false,
    }
}
