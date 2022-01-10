// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    scrutiny::{
        model::controller::{DataController, HintDataType},
        model::model::*,
    },
    scrutiny_utils::{fvm::*, usage::*},
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::fs::{self, File},
    std::io::prelude::*,
    std::path::PathBuf,
    std::sync::Arc,
};

#[derive(Deserialize, Serialize)]
pub struct FvmExtractRequest {
    // The input path for the FVM to extract.
    pub input: String,
    // The output directory for the extracted FVM.
    pub output: String,
}

#[derive(Default)]
pub struct FvmExtractController {}

impl DataController for FvmExtractController {
    fn query(&self, _model: Arc<DataModel>, query: Value) -> Result<Value> {
        let request: FvmExtractRequest = serde_json::from_value(query)?;
        let mut fvm_file = File::open(request.input)?;
        let output_path = PathBuf::from(request.output);
        let mut fvm_buffer = Vec::new();
        fvm_file.read_to_end(&mut fvm_buffer)?;
        let mut reader = FvmReader::new(fvm_buffer);
        let fvm_partitions = reader.parse()?;

        fs::create_dir_all(&output_path)?;
        let mut minfs_count = 0;
        let mut blobfs_count = 0;
        for partition in fvm_partitions {
            let partition_name = match partition.partition_type {
                FvmPartitionType::MinFs => {
                    if minfs_count == 0 {
                        format!("{}.blk", partition.partition_type)
                    } else {
                        format!("{}.{}.blk", partition.partition_type, minfs_count)
                    }
                }
                FvmPartitionType::BlobFs => {
                    if blobfs_count == 0 {
                        format!("{}.blk", partition.partition_type)
                    } else {
                        format!("{}.{}.blk", partition.partition_type, blobfs_count)
                    }
                }
            };
            let mut path = output_path.clone();
            path.push(partition_name);
            let mut file = File::create(path)?;
            file.write_all(&partition.buffer)?;
            match partition.partition_type {
                FvmPartitionType::MinFs => minfs_count += 1,
                FvmPartitionType::BlobFs => blobfs_count += 1,
            };
        }

        Ok(json!({"status": "ok"}))
    }

    fn description(&self) -> String {
        "Extracts FVM (.blk) values from an input file to a directory.".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("tool.fvm.extract - Extracts a fvm (.blk) to a directory.")
            .summary("tool.fvm.extract --input foo.blk --output /tmp/foo")
            .description(
                "Extracts a FVM (.blk) into a series of volumes. \
            This tool will simply extract the volumes as large files and will \
            not attempt to interpret the internal file system within the volume.",
            )
            .arg("--input", "Path to the input fvm to extract.")
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
