// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    scrutiny::{
        model::controller::{ConnectionMode, DataController, HintDataType},
        model::model::*,
    },
    scrutiny_utils::{blobfs::*, usage::*},
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::fs::{self, File},
    std::io::prelude::*,
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
        let mut blobfs_file = File::open(request.input)?;
        let output_path = PathBuf::from(request.output);
        let mut blobfs_buffer = Vec::new();
        blobfs_file.read_to_end(&mut blobfs_buffer)?;
        let mut reader = BlobFsReader::new(blobfs_buffer);
        let blobs = reader.parse()?;

        fs::create_dir_all(&output_path)?;
        for blob in blobs {
            let mut path = output_path.clone();
            path.push(blob.merkle.clone());
            let mut file = File::create(path)?;
            file.write_all(&blob.buffer)?;
        }
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
