// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::collection::{Package, Packages},
    anyhow::{anyhow, Context, Result},
    fuchsia_archive::Utf8Reader as FarReader,
    fuchsia_url::AbsolutePackageUrl,
    scrutiny::{
        model::controller::{DataController, HintDataType},
        model::model::DataModel,
    },
    scrutiny_utils::{
        artifact::{ArtifactReader, BlobFsArtifactReader},
        usage::UsageBuilder,
    },
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::{
        fs::{self, File},
        io::Write,
        path::{Path, PathBuf},
        sync::Arc,
    },
};

#[derive(Deserialize, Serialize)]
pub struct PackageExtractRequest {
    // The input path for the ZBI.
    pub url: AbsolutePackageUrl,
    // The output directory for the extracted ZBI.
    pub output: PathBuf,
}

impl PackageExtractRequest {
    fn url_matches_package(&self, pkg: &Package) -> bool {
        // Unconditionally check package name.
        if !self.url_matches_pkg_name(pkg) {
            return false;
        }

        // Check variant iff request specifies variant.
        if self.url_has_variant() {
            if !self.url_matches_pkg_variant(pkg) {
                return false;
            }
        }

        // Check hash iff request specifies hash.
        if self.url_has_package_hash() {
            if !self.url_matches_package_hash(pkg) {
                return false;
            }
        }

        // Failed checks returned `false` early.
        return true;
    }

    fn url_has_variant(&self) -> bool {
        self.url.variant().is_some()
    }

    fn url_has_package_hash(&self) -> bool {
        self.url.hash().is_some()
    }

    fn url_matches_pkg_name(&self, pkg: &Package) -> bool {
        self.url.name() == &pkg.name
    }

    fn url_matches_pkg_variant(&self, pkg: &Package) -> bool {
        self.url.variant() == pkg.variant.as_ref()
    }

    fn url_matches_package_hash(&self, pkg: &Package) -> bool {
        self.url.hash() == Some(pkg.merkle)
    }
}

fn create_dir_all<P: AsRef<Path> + ?Sized>(path: &P) -> Result<()> {
    fs::create_dir_all(path).map_err(|err| {
        anyhow!("Failed to create directory {}: {}", path.as_ref().display(), err.to_string())
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
        let mut artifact_reader = BlobFsArtifactReader::try_compound(
            &model.config().build_path(),
            model.config().tmp_dir_path().as_ref(),
            &model.config().blobfs_paths(),
        )
        .context("Failed to construct blobfs artifact reader for package extractor")?;

        let request: PackageExtractRequest = serde_json::from_value(query)?;
        let packages = &model.get::<Packages>()?.entries;
        for package in packages.iter() {
            if request.url_matches_package(package) {
                let output_path = PathBuf::from(request.output);
                create_dir_all(&output_path).context("Failed to create output directory")?;

                let merkle_string = format!("{}", package.merkle);
                let blob = artifact_reader
                    .open(Path::new(&merkle_string))
                    .context("Failed to read package from blobfs archive(s)")?;
                let mut far = FarReader::new(blob)?;

                let pkg_files: Vec<String> = far.list().map(|e| e.path().to_string()).collect();
                // Extract all the far meta files.
                for file_name in pkg_files.iter() {
                    let mut data = far.read_file(file_name)?;
                    let file_path = output_path.join(file_name);
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
                for (file_name, file_merkle) in package.contents.iter() {
                    let merkle_string = format!("{}", file_merkle);
                    let mut blob = artifact_reader
                        .open(Path::new(&merkle_string))
                        .context("Failed to read package from blobfs archive(s)")?;

                    let file_path = output_path.join(file_name);
                    if let Some(parent_dir) = file_path.as_path().parent() {
                        create_dir_all(parent_dir)
                            .context("Failed to create directory for package contents file")?;
                    }
                    let mut packaged_file = create_file(&file_path)
                        .context("Failed to create package contents file")?;
                    std::io::copy(&mut blob, &mut packaged_file).map_err(|err| {
                        anyhow!("Failed to write file at {:?}: {}", file_path, err)
                    })?;
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
}
