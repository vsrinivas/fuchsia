// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    log::info,
    scrutiny::{
        model::controller::{ConnectionMode, DataController, HintDataType},
        model::model::*,
    },
    scrutiny_utils::{blobfs::*, bootfs::*, fvm::*, usage::*, zbi::*},
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::collections::HashMap,
    std::fs::{self, File},
    std::io::prelude::*,
    std::path::PathBuf,
    std::sync::Arc,
};

#[derive(Deserialize, Serialize)]
pub struct ZbiExtractRequest {
    // The input path for the ZBI.
    pub input: String,
    // The output directory for the extracted ZBI.
    pub output: String,
}

#[derive(Default)]
pub struct ZbiExtractController {}

impl DataController for ZbiExtractController {
    fn query(&self, _model: Arc<DataModel>, query: Value) -> Result<Value> {
        let request: ZbiExtractRequest = serde_json::from_value(query)?;
        let mut zbi_file = File::open(request.input)?;
        let output_path = PathBuf::from(request.output);
        let mut zbi_buffer = Vec::new();
        zbi_file.read_to_end(&mut zbi_buffer)?;
        let mut reader = ZbiReader::new(zbi_buffer);
        let zbi_sections = reader.parse()?;

        fs::create_dir_all(&output_path)?;
        let mut sections_dir = output_path.clone();
        sections_dir.push("sections");
        fs::create_dir_all(&sections_dir)?;
        let mut section_count = HashMap::new();
        for section in zbi_sections.iter() {
            let section_str = format!("{:?}", section.section_type).to_lowercase();
            let section_name = if let Some(count) = section_count.get_mut(&section.section_type) {
                *count += 1;
                format!("{}.{}.blk", section_str, count)
            } else {
                section_count.insert(section.section_type, 0);
                format!("{}.blk", section_str)
            };
            let mut path = sections_dir.clone();
            path.push(section_name);
            let mut file = File::create(path)?;
            file.write_all(&section.buffer)?;

            // Expand bootfs into its own folder as well.
            if section.section_type == ZbiType::StorageBootfs {
                let mut bootfs_dir = output_path.clone();
                bootfs_dir.push("bootfs");
                fs::create_dir_all(bootfs_dir.clone())?;
                let mut bootfs_reader = BootfsReader::new(section.buffer.clone());
                let bootfs_files = bootfs_reader.parse()?;
                for (file_name, data) in bootfs_files.iter() {
                    let mut bootfs_file_path = bootfs_dir.clone();
                    bootfs_file_path.push(file_name);
                    if let Some(parent_dir) = bootfs_file_path.as_path().parent() {
                        fs::create_dir_all(parent_dir)?;
                    }
                    let mut bootfs_file = File::create(bootfs_file_path)?;
                    bootfs_file.write_all(&data)?;
                }
            } else if section.section_type == ZbiType::StorageRamdisk {
                info!("Attempting to load FvmPartitions");
                let mut fvm_reader = FvmReader::new(section.buffer.clone());
                if let Ok(fvm_partitions) = fvm_reader.parse() {
                    info!("Extracting {} Partitions in StorageRamdisk", fvm_partitions.len());
                    let mut fvm_dir = output_path.clone();
                    fvm_dir.push("fvm");
                    fs::create_dir_all(fvm_dir.clone())?;

                    let mut partition_count = HashMap::<FvmPartitionType, u64>::new();
                    for partition in fvm_partitions.iter() {
                        let file_name = if let Some(count) =
                            partition_count.get_mut(&partition.partition_type)
                        {
                            *count += 1;
                            format!("{}.{}.blk", partition.partition_type, count)
                        } else {
                            section_count.insert(section.section_type, 0);
                            format!("{}.blk", partition.partition_type)
                        };
                        let mut fvm_partition_path = fvm_dir.clone();
                        fvm_partition_path.push(file_name);
                        let mut fvm_file = File::create(fvm_partition_path)?;
                        fvm_file.write_all(&partition.buffer)?;

                        // Write out the blobfs data.
                        if partition.partition_type == FvmPartitionType::BlobFs {
                            info!("Extracting BlobFs FVM partiion");
                            let mut blobfs_dir = fvm_dir.clone();
                            blobfs_dir.push("blobfs");
                            fs::create_dir_all(blobfs_dir.clone())?;
                            let mut reader = BlobFsReader::new(partition.buffer.clone());
                            let blobs = reader.parse()?;

                            for blob in blobs {
                                let mut path = blobfs_dir.clone();
                                path.push(blob.merkle.clone());
                                let mut file = File::create(path)?;
                                file.write_all(&blob.buffer)?;
                            }
                        }
                    }
                } else {
                    info!("No FvmPartitions found in StorageRamdisk");
                }
            }
        }

        Ok(json!({"status": "ok"}))
    }

    fn description(&self) -> String {
        "Extracts a ZBI and outputs all the file contents.".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("tool.zbi.extract - Extracts Zircon Boot Images")
            .summary("tool.zbi.extract --input foo.zbi --output /foo/bar")
            .description(
                "Extracts zircon boot images into their sections \
            some recognized sections like bootfs are further parsed out into \
            directories.",
            )
            .arg("--input", "Path to the input zbi file")
            .arg("--output", "Path to the output directory")
            .build()
    }

    fn hints(&self) -> Vec<(String, HintDataType)> {
        vec![
            ("--input".to_string(), HintDataType::NoType),
            ("--output".to_string(), HintDataType::NoType),
        ]
    }

    /// ZbiExtract is only available to the local shell as it directly
    /// modifies files on disk.
    fn connection_mode(&self) -> ConnectionMode {
        ConnectionMode::Local
    }
}
