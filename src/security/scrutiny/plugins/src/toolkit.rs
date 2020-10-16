// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    anyhow::Result,
    fuchsia_archive::Reader as FarReader,
    log::info,
    scrutiny::{
        collectors, controllers,
        engine::hook::PluginHooks,
        engine::plugin::{Plugin, PluginDescriptor},
        model::collector::DataCollector,
        model::controller::{ConnectionMode, DataController, HintDataType},
        model::model::*,
        plugin,
    },
    scrutiny_utils::{blobfs::*, bootfs::*, env, fvm::*, usage::*, zbi::*},
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::collections::HashMap,
    std::fs::{self, File},
    std::io::prelude::*,
    std::io::Cursor,
    std::path::PathBuf,
    std::sync::Arc,
};

#[derive(Deserialize, Serialize)]
struct ZbiExtractRequest {
    // The input path for the ZBI.
    input: String,
    // The output directory for the extracted ZBI.
    output: String,
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
                    info!("Found {} Partitions in StorageRamdisk", fvm_partitions.len());
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

#[derive(Deserialize, Serialize)]
struct PackageExtractRequest {
    // The input path for the ZBI.
    url: String,
    // The output directory for the extracted ZBI.
    output: String,
}

pub const BLOBS_PATH: &str = "amber-files/repository/blobs";

#[derive(Default)]
pub struct PackageExtractController {}

impl DataController for PackageExtractController {
    fn query(&self, model: Arc<DataModel>, query: Value) -> Result<Value> {
        let fuchsia_build_dir = env::fuchsia_build_dir()?;
        let blob_dir = fuchsia_build_dir.join(BLOBS_PATH);

        let request: PackageExtractRequest = serde_json::from_value(query)?;
        let packages = model.packages().read().unwrap();
        for package in packages.iter() {
            if package.url == request.url {
                let output_path = PathBuf::from(request.output);
                fs::create_dir_all(&output_path)?;

                let pkg_path = blob_dir.clone().join(package.merkle.clone());
                let mut pkg_file = File::open(pkg_path)?;
                let mut pkg_buffer = Vec::new();
                pkg_file.read_to_end(&mut pkg_buffer)?;

                let mut cursor = Cursor::new(pkg_buffer);
                let mut far = FarReader::new(&mut cursor)?;

                let pkg_files: Vec<String> = far.list().map(|s| String::from(s)).collect();
                // Extract all the far meta files.
                for file_name in pkg_files.iter() {
                    let data = far.read_file(file_name)?;
                    let file_path = output_path.clone().join(file_name);
                    if let Some(parent_dir) = file_path.as_path().parent() {
                        fs::create_dir_all(parent_dir)?;
                    }
                    let mut package_file = File::create(file_path)?;
                    package_file.write_all(&data)?;
                }

                // Extract all the contents of the package.
                for (file_name, blob) in package.contents.iter() {
                    let file_path = output_path.clone().join(file_name);

                    let blob_path = blob_dir.clone().join(blob);
                    let mut blob_file = File::open(blob_path)?;
                    let mut blob_buffer = Vec::new();
                    blob_file.read_to_end(&mut blob_buffer)?;

                    if let Some(parent_dir) = file_path.as_path().parent() {
                        fs::create_dir_all(parent_dir)?;
                    }
                    let mut package_file = File::create(file_path)?;
                    package_file.write_all(&blob_buffer)?;
                }
                return Ok(json!({"status": "ok"}));
            }
        }
        Err(anyhow!("Unable to find package url"))
    }

    fn description(&self) -> String {
        "Extracts a package from a url to a directory.".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("tool.package.extract - Extracts Fuchsia package to a directory.")
            .summary("tool.package.extract --url fuchsia-pkg://fuchsia.com/foo --output /tmp/foo")
            .description(
                "Extracts package from a given url to some provided file path. \
                Internally this is resolving the URL and extracting the internal
                Fuchsia Archive and resolving all the merkle paths.
                ",
            )
            .arg("--url", "Package url that you wish to extract.")
            .arg("--output", "Path to the output directory")
            .build()
    }

    fn hints(&self) -> Vec<(String, HintDataType)> {
        vec![
            ("--url".to_string(), HintDataType::NoType),
            ("--output".to_string(), HintDataType::NoType),
        ]
    }

    /// PackageExtract is only available to the local shell as it directly
    /// modifies files on disk.
    fn connection_mode(&self) -> ConnectionMode {
        ConnectionMode::Local
    }
}

#[derive(Deserialize, Serialize)]
struct FvmExtractRequest {
    // The input path for the FVM to extract.
    input: String,
    // The output directory for the extracted FVM.
    output: String,
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

    /// FvmExtract is only available to the local shell as it directly
    /// modifies files on disk.
    fn connection_mode(&self) -> ConnectionMode {
        ConnectionMode::Local
    }
}

#[derive(Deserialize, Serialize)]
struct BlobFsExtractRequest {
    // The input path for the BlobFs to extract.
    input: String,
    // The output directory for the extracted BlobFs.
    output: String,
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

plugin!(
    ToolkitPlugin,
    PluginHooks::new(
        collectors! {},
        controllers! {
            "/tool/blobfs/extract" => BlobFsExtractController::default(),
            "/tool/fvm/extract" => FvmExtractController::default(),
            "/tool/package/extract" => PackageExtractController::default(),
            "/tool/zbi/extract" => ZbiExtractController::default(),
        }
    ),
    vec![PluginDescriptor::new("CorePlugin")]
);

#[cfg(test)]
mod tests {
    use {super::*, tempfile::tempdir};

    #[test]
    fn test_zbi_extractor_empty_zbi() {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(uri).unwrap());
        let zbi_controller = ZbiExtractController::default();
        let input_dir = tempdir().unwrap();
        let input_path = input_dir.path().join("empty-zbi");
        let output_dir = tempdir().unwrap();
        let output_path = output_dir.path();
        let request = ZbiExtractRequest {
            input: input_path.to_str().unwrap().to_string(),
            output: output_path.to_str().unwrap().to_string(),
        };
        let query = serde_json::to_value(request).unwrap();
        let response = zbi_controller.query(model, query);
        assert_eq!(response.is_ok(), false);
    }

    #[test]
    fn test_package_extractor_invalid_url() {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(uri).unwrap());
        let package_controller = PackageExtractController::default();
        let output_dir = tempdir().unwrap();
        let output_path = output_dir.path();
        let request = PackageExtractRequest {
            url: "fake_path".to_string(),
            output: output_path.to_str().unwrap().to_string(),
        };
        let query = serde_json::to_value(request).unwrap();
        let response = package_controller.query(model, query);
        assert_eq!(response.is_ok(), false);
    }

    #[test]
    fn test_fvm_extractor_empty_fvm() {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(uri).unwrap());
        let fvm_controller = FvmExtractController::default();
        let input_dir = tempdir().unwrap();
        let input_path = input_dir.path().join("empty-fvm");
        let output_dir = tempdir().unwrap();
        let output_path = output_dir.path();
        let request = FvmExtractRequest {
            input: input_path.to_str().unwrap().to_string(),
            output: output_path.to_str().unwrap().to_string(),
        };
        let query = serde_json::to_value(request).unwrap();
        let response = fvm_controller.query(model, query);
        assert_eq!(response.is_ok(), false);
    }
}
