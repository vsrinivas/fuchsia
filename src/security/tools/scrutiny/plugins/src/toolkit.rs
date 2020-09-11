// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    anyhow::Result,
    fuchsia_archive::Reader as FarReader,
    scrutiny::{
        collectors, controllers,
        engine::hook::PluginHooks,
        engine::plugin::{Plugin, PluginDescriptor},
        model::collector::DataCollector,
        model::controller::{ConnectionMode, DataController, HintDataType},
        model::model::*,
        plugin,
    },
    scrutiny_utils::{bootfs::*, usage::*, zbi::*},
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::env,
    std::fs::{self, File},
    std::io::prelude::*,
    std::io::Cursor,
    std::path::{Path, PathBuf},
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
        let mut section_count = 0;
        for section in zbi_sections.iter() {
            let section_name = format!("{}_{:?}", section_count, section.section_type);
            let mut path = output_path.clone();
            path.push(section_name);
            let mut file = File::create(path)?;
            file.write_all(&section.buffer)?;
            section_count += 1;

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

pub const BLOBS_PATH: &str = "out/default/amber-files/repository/blobs";

#[derive(Default)]
pub struct PackageExtractController {}

impl DataController for PackageExtractController {
    fn query(&self, model: Arc<DataModel>, query: Value) -> Result<Value> {
        let fuchsia_dir = env::var("FUCHSIA_DIR")?;
        let blob_dir = Path::new(&fuchsia_dir).join(BLOBS_PATH);

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

plugin!(
    ToolkitPlugin,
    PluginHooks::new(
        collectors! {},
        controllers! {
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
}
