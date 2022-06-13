// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    scrutiny::{
        model::controller::{DataController, HintDataType},
        model::model::*,
    },
    scrutiny_utils::{blobfs::*, usage::*},
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::fs::{self, File},
    std::io::{prelude::*, BufReader},
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
        let blobfs_file = File::open(&blobfs_path)
            .map_err(|err| anyhow!("Failed to open blbofs archive {:?}: {}", blobfs_path, err))?;
        let mut reader =
            BlobFsReaderBuilder::new().archive(BufReader::new(blobfs_file))?.build()?;

        let output_path = PathBuf::from(request.output);
        fs::create_dir_all(&output_path)?;
        for blob_path in reader.clone().blob_paths() {
            let path = output_path.join(blob_path);
            let mut file = File::create(path)?;
            file.write_all(reader.read_blob(blob_path)?.as_slice())?;
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
}
