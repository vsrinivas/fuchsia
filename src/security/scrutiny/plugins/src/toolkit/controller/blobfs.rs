// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    scrutiny::{
        model::controller::{DataController, HintDataType},
        model::model::*,
    },
    scrutiny_utils::{blobfs::*, fs::tempdir, io::TryClonableBufReaderFile, usage::*},
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::fs::{self, File},
    std::io::BufReader,
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
    fn query(&self, model: Arc<DataModel>, query: Value) -> Result<Value> {
        let tmp_root_dir_path = model.config().tmp_dir_path();
        let tmp_dir = tempdir(tmp_root_dir_path.as_ref())
            .context("Failed to create temporary directory for blobfs extract controller")?;

        let request: BlobFsExtractRequest = serde_json::from_value(query)?;
        let blobfs_path = PathBuf::from(request.input);
        let blobfs_file = File::open(&blobfs_path)
            .map_err(|err| anyhow!("Failed to open blbofs archive {:?}: {}", blobfs_path, err))?;
        let reader: TryClonableBufReaderFile = BufReader::new(blobfs_file).into();
        let mut reader =
            BlobFsReaderBuilder::new().archive(reader)?.tmp_dir(Arc::new(tmp_dir))?.build()?;

        let output_path = PathBuf::from(request.output);
        fs::create_dir_all(&output_path)?;

        // Clone paths out of `reader` to avoid simultaneous immutable borrow from
        // `reader.blob_paths()` and mutable borrow from `reader.read_blob()`.
        let blob_paths: Vec<PathBuf> = reader.blob_paths().map(PathBuf::clone).collect();
        for blob_path in blob_paths.into_iter() {
            let path = output_path.join(&blob_path);
            let mut file = File::create(path)?;
            let mut blob = reader.open(&blob_path)?;
            std::io::copy(&mut blob, &mut file)?;
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
