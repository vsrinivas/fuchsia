// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    scrutiny::{
        model::controller::{DataController, HintDataType},
        model::model::*,
    },
    scrutiny_utils::{usage::*, zbi::*},
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::fs::File,
    std::io::prelude::*,
    std::sync::Arc,
};

#[derive(Deserialize, Serialize)]
pub struct ZbiExtractCmdlineRequest {
    // The input path for the ZBI.
    pub input: String,
}

#[derive(Default)]
pub struct ZbiExtractCmdlineController {}

impl DataController for ZbiExtractCmdlineController {
    fn query(&self, _model: Arc<DataModel>, query: Value) -> Result<Value> {
        let request: ZbiExtractCmdlineRequest = serde_json::from_value(query)?;
        let mut zbi_file = File::open(request.input)?;
        let mut zbi_buffer = Vec::new();
        zbi_file.read_to_end(&mut zbi_buffer)?;
        let mut reader = ZbiReader::new(zbi_buffer);
        let zbi_sections = reader.parse()?;

        for section in zbi_sections.iter() {
            if section.section_type == ZbiType::Cmdline {
                // The cmdline.blk contains a trailing 0.
                let mut cmdline_buffer = section.buffer.clone();
                cmdline_buffer.truncate(cmdline_buffer.len() - 1);
                return Ok(json!(std::str::from_utf8(&cmdline_buffer)
                    .context("Failed to convert kernel arguments to utf-8")?));
            }
        }
        Err(anyhow!("Failed to find a cmdline section in the provided ZBI"))
    }

    fn description(&self) -> String {
        "Extracts the kernel cmdline from a ZBI".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("tool.zbi.extract.cmdline - Extracts ZBI kernel cmdline")
            .summary("tool.zbi.extract.cmdline --input foo.zbi")
            .description("Extracts zircon boot images and retrieves the kernel cmdline.")
            .arg("--input", "Path to the input zbi file")
            .build()
    }

    fn hints(&self) -> Vec<(String, HintDataType)> {
        vec![("--input".to_string(), HintDataType::NoType)]
    }
}
