// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::collection::Packages,
    anyhow::{anyhow, Context, Result},
    fuchsia_archive::Reader as FarReader,
    scrutiny::{
        model::controller::{ConnectionMode, DataController, HintDataType},
        model::model::DataModel,
    },
    scrutiny_utils::{key_value::parse_key_value, usage::UsageBuilder},
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::{
        fs::{self, File},
        io::{Cursor, Read, Write},
        path::{Path, PathBuf},
        sync::Arc,
    },
};

#[derive(Deserialize, Serialize)]
pub struct PackageExtractRequest {
    // The input path for the ZBI.
    pub url: String,
    // The output directory for the extracted ZBI.
    pub output: String,
}

fn read_file_to_string<P>(path: &P) -> Result<String>
where
    P: AsRef<Path>,
{
    fs::read_to_string(path).map_err(|err| {
        anyhow!("Failed to read file from {}: {}", path.as_ref().display(), err.to_string())
    })
}

fn create_dir_all<P: AsRef<Path> + ?Sized>(path: &P) -> Result<()> {
    fs::create_dir_all(path).map_err(|err| {
        anyhow!("Failed to create directory {}: {}", path.as_ref().display(), err.to_string())
    })
}

fn open_file<P>(path: &P) -> Result<fs::File>
where
    P: AsRef<Path>,
{
    File::open(path).map_err(|err| {
        anyhow!("Failed to open file at {}: {}", path.as_ref().display(), err.to_string())
    })
}

fn read_file_to_end<P>(path: &P, file: &mut fs::File, buf: &mut Vec<u8>) -> Result<usize>
where
    P: AsRef<Path>,
{
    file.read_to_end(buf).map_err(|err| {
        anyhow!("Failed to read file at {}: {}", path.as_ref().display(), err.to_string())
    })
}

fn create_file<P>(path: &P) -> Result<fs::File>
where
    P: AsRef<Path>,
{
    File::create(path).map_err(|err| {
        anyhow!("Failed to create file at {}: {}", path.as_ref().display(), err.to_string())
    })
}

fn write_file<P>(path: &P, file: &mut fs::File, buf: &mut [u8]) -> Result<()>
where
    P: AsRef<Path>,
{
    file.write_all(buf).map_err(|err| {
        anyhow!("Failed to write file at {}: {}", path.as_ref().display(), err.to_string())
    })
}

#[derive(Default)]
pub struct PackageExtractController {}

impl DataController for PackageExtractController {
    fn query(&self, model: Arc<DataModel>, query: Value) -> Result<Value> {
        let blob_manifest_path = model.config().blob_manifest_path();
        let blob_manifest = parse_key_value(
            read_file_to_string(&blob_manifest_path).context("Failed to read blob manifest")?,
        )
        .context("Failed to parse blob manifest")?;

        // Paths in blob manifest are relative to manifest file's directory.
        let mut blob_dir = blob_manifest_path.clone();
        blob_dir.pop();

        let request: PackageExtractRequest = serde_json::from_value(query)?;
        let packages = &model.get::<Packages>()?.entries;
        for package in packages.iter() {
            if package.url == request.url {
                let output_path = PathBuf::from(request.output);
                create_dir_all(&output_path).context("Failed to create output directory")?;

                let pkg_path = blob_manifest.get(&package.merkle);
                if pkg_path.is_none() {
                    return Err(anyhow!(
                        "Failed to locate blob ID {} in blob manifest at {}",
                        &package.merkle,
                        blob_manifest_path.as_path().display()
                    ));
                }

                let pkg_path = blob_dir.clone().join(pkg_path.unwrap());
                let mut pkg_file = open_file(&pkg_path).context("Failed to open package")?;
                let mut pkg_buffer = Vec::new();
                read_file_to_end(&pkg_path, &mut pkg_file, &mut pkg_buffer)
                    .context("Failed to read package")?;

                let mut cursor = Cursor::new(pkg_buffer);
                let mut far = FarReader::new(&mut cursor)?;

                let pkg_files: Vec<String> = far.list().map(|e| e.path().to_string()).collect();
                // Extract all the far meta files.
                for file_name in pkg_files.iter() {
                    let mut data = far.read_file(file_name)?;
                    let file_path = output_path.clone().join(file_name);
                    if let Some(parent_dir) = file_path.as_path().parent() {
                        create_dir_all(parent_dir)
                            .context("Failed to create far meta directory")?;
                    }
                    let mut package_file =
                        create_file(&file_path).context("Failed to create package file")?;
                    write_file(&file_path, &mut package_file, &mut data)
                        .context("Failed to write to package file")?;
                }

                // Extract all the contents of the package.
                for (file_name, blob) in package.contents.iter() {
                    let blob_path = blob_manifest.get(blob);
                    if blob_path.is_none() {
                        return Err(anyhow!(
                            "Failed to locate blob ID {} in blob manifest at {}",
                            blob,
                            blob_manifest_path.as_path().display()
                        ));
                    }

                    let blob_path = blob_dir.clone().join(blob_path.unwrap());
                    let file_path = output_path.clone().join(file_name);
                    let mut blob_file =
                        open_file(&blob_path).context("Failed to open package contents blob")?;
                    let mut blob_buffer = Vec::new();
                    read_file_to_end(&blob_path, &mut blob_file, &mut blob_buffer)
                        .context("Failed to read package contents blob")?;

                    if let Some(parent_dir) = file_path.as_path().parent() {
                        create_dir_all(parent_dir)
                            .context("Failed to create directory for package contents file")?;
                    }
                    let mut packaged_file = create_file(&file_path)
                        .context("Failed to create package contents file")?;
                    write_file(&file_path, &mut packaged_file, &mut blob_buffer)
                        .context("Failed to write package contents file")?;
                }
                return Ok(json!({"status": "ok"}));
            }
        }
        Err(anyhow!("Unable to find package with url {}", request.url))
    }

    fn description(&self) -> String {
        "Extracts a package from a url to a directory.".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("package.extract - Extracts Fuchsia package to a directory.")
            .summary("package.extract --url fuchsia-pkg://fuchsia.com/foo --output /tmp/foo")
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
