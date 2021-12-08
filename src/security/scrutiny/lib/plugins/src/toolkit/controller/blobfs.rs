// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    scrutiny::{
        model::controller::{ConnectionMode, DataController, HintDataType},
        model::model::*,
    },
    scrutiny_utils::{blobfs_export::blobfs_export, usage::*},
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::path::PathBuf,
    std::sync::Arc,
};

#[derive(Deserialize, Serialize)]
pub struct BlobFsExtractRequest {
    // The input path for the BlobFs to extract.
    pub input: String,
    // The output directory for the extracted BlobFs.
    pub output: String,
}

#[derive(Default)]
pub struct BlobFsExtractController {}

impl DataController for BlobFsExtractController {
    fn query(&self, _model: Arc<DataModel>, query: Value) -> Result<Value> {
        let request: BlobFsExtractRequest = serde_json::from_value(query)?;
        let blobfs_path = PathBuf::from(request.input);
        let output_path = PathBuf::from(request.output);
        blobfs_export(&blobfs_path.to_str().unwrap(), &output_path.to_str().unwrap())?;
        Ok(json!({"status": "ok"}))
    }

    fn description(&self) -> String {
        "Extracts BlobFs partition from an input file to a directory.".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("tool.blobfs.extract - Extracts a blobfs partition to a directory.")
            .summary("tool.blobfs.extract --input blobfs_partition --output /tmp/foo")
            .description(
                "Extracts a BlobFs partition into a series of files. \
                Each file will simply be named its merkle hash to prevent collisions.",
            )
            .arg("--input", "Path to the input blobfs partition to extract.")
            .arg("--output", "Path to the output directory")
            .build()
    }

    fn hints(&self) -> Vec<(String, HintDataType)> {
        vec![
            ("--input".to_string(), HintDataType::NoType),
            ("--output".to_string(), HintDataType::NoType),
        ]
    }

    /// BlobFsExtract is only available to the local shell as it directly
    /// modifies files on disk.
    fn connection_mode(&self) -> ConnectionMode {
        ConnectionMode::Local
    }
}
