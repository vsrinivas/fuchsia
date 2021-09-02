// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    scrutiny::{
        model::controller::{ConnectionMode, DataController, HintDataType},
        model::model::*,
    },
    scrutiny_utils::{bootfs::*, usage::*, zbi::*},
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::fs::File,
    std::io::prelude::*,
    std::sync::Arc,
};

#[derive(Deserialize, Serialize)]
pub struct ZbiExtractDevmgrConfigRequest {
    // The input path for the ZBI.
    pub input: String,
}

#[derive(Default)]
pub struct ZbiExtractDevmgrConfigController {}

impl DataController for ZbiExtractDevmgrConfigController {
    fn query(&self, model: Arc<DataModel>, query: Value) -> Result<Value> {
        let request: ZbiExtractDevmgrConfigRequest = serde_json::from_value(query)?;
        let mut zbi_file = File::open(request.input)?;
        let mut zbi_buffer = Vec::new();
        zbi_file.read_to_end(&mut zbi_buffer)?;
        let mut reader = ZbiReader::new(zbi_buffer);
        let zbi_sections = reader.parse()?;

        for section in zbi_sections.iter() {
            if section.section_type == ZbiType::StorageBootfs {
                let mut bootfs_reader = BootfsReader::new(section.buffer.clone());
                let bootfs_data = bootfs_reader.parse().context("failed to parse bootfs")?;
                for (file, data) in bootfs_data.iter() {
                    if file == &model.config().devmgr_config_path() {
                        return Ok(json!(std::str::from_utf8(&data)
                            .context("Failed to convert devmgr config to utf-8")?));
                    }
                }
            }
        }
        Err(anyhow!("Failed to find a devmgr config in the provided ZBI"))
    }

    fn description(&self) -> String {
        "Extracts the devmgr config from a ZBI".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("tool.zbi.extract.devmgr.config - Extracts devmgr config ")
            .summary("tool.zbi.extract.devmgr.config --input foo.zbi")
            .description("Extracts zircon boot images and retrieves the devmgr config.")
            .arg("--input", "Path to the input zbi file")
            .build()
    }

    fn hints(&self) -> Vec<(String, HintDataType)> {
        vec![("--input".to_string(), HintDataType::NoType)]
    }

    /// ZbiExtractDevmgrConfig is only available to the local shell.
    fn connection_mode(&self) -> ConnectionMode {
        ConnectionMode::Local
    }
}
