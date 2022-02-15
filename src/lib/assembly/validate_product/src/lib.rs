// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_pkg::PackageManifest;
use rayon::iter::{ParallelBridge, ParallelIterator};
use std::{
    collections::BTreeMap,
    fmt,
    fs::File,
    io::BufReader,
    path::{Path, PathBuf},
};

/// Validate all the packages in a product config.
pub fn validate_product<'a>(
    // TODO change this to a reference to a whole image assembly once that type is in a library
    package_manifests: impl Iterator<Item = impl AsRef<Path> + Send> + Send,
) -> Result<(), ProductValidationError> {
    let errors: BTreeMap<_, _> = package_manifests
        .par_bridge()
        .filter_map(|package| {
            let package = package.as_ref();
            if let Err(e) = validate_package(package) {
                Some((package.to_owned(), e))
            } else {
                None
            }
        })
        .collect();

    if errors.is_empty() {
        Ok(())
    } else {
        Err(ProductValidationError { packages: errors })
    }
}

/// Validate a package's contents.
///
/// Assumes that all component manifests will be in the `meta/` directory and have a `.cm` extension
/// within the package namespace.
fn validate_package(manifest_path: &Path) -> Result<(), PackageValidationError> {
    // deserialize the manifest for the package
    let file = File::open(manifest_path).map_err(|source| PackageValidationError::Open {
        source,
        path: manifest_path.to_owned(),
    })?;
    let manifest: PackageManifest = serde_json::from_reader(BufReader::new(file))
        .map_err(PackageValidationError::JsonDecode)?;
    let blobs = manifest.into_blobs();

    // read meta.far contents
    let meta_far_info = blobs
        .into_iter()
        .find(|b| b.path == "meta/")
        .ok_or(PackageValidationError::MissingMetaFar)?;
    let meta_far = File::open(&meta_far_info.source_path).map_err(|source| {
        PackageValidationError::Open { source, path: PathBuf::from(meta_far_info.source_path) }
    })?;
    let mut reader =
        fuchsia_archive::Reader::new(meta_far).map_err(PackageValidationError::ReadArchive)?;

    // validate components in the meta/ directory
    let component_manifests = reader
        .list()
        .filter_map(|e| if e.path().ends_with(".cm") { Some(e.path().to_owned()) } else { None })
        .collect::<Vec<_>>();
    let mut errors = BTreeMap::new();
    for path in component_manifests {
        if let Err(e) = validate_component(&path, &mut reader) {
            errors.insert(path, e);
        }
    }
    if !errors.is_empty() {
        return Err(PackageValidationError::InvalidComponents(errors));
    }

    Ok(())
}

/// Validate an individual component within the package.
fn validate_component(
    manifest_path: &str,
    meta_far: &mut fuchsia_archive::Reader<File>,
) -> anyhow::Result<()> {
    assembly_structured_config::validate_component(manifest_path, meta_far)?;
    Ok(())
}

/// Collection of all package validation failures within a product.
#[derive(Debug)]
pub struct ProductValidationError {
    packages: BTreeMap<PathBuf, PackageValidationError>,
}

impl From<ProductValidationError> for anyhow::Error {
    fn from(e: ProductValidationError) -> anyhow::Error {
        anyhow::Error::msg(e)
    }
}

impl fmt::Display for ProductValidationError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "Validating structured configuration of product failed:")?;
        for (package, error) in &self.packages {
            let error_msg = textwrap::indent(&error.to_string(), "        ");
            write!(f, "    └── {}: {}", package.display(), error_msg)?;
        }
        Ok(())
    }
}

/// Failures that can occur when validating packages.
#[derive(Debug)]
pub enum PackageValidationError {
    Open { path: PathBuf, source: std::io::Error },
    JsonDecode(serde_json::Error),
    MissingMetaFar,
    ReadArchive(fuchsia_archive::Error),
    InvalidComponents(BTreeMap<String, anyhow::Error>),
}

impl fmt::Display for PackageValidationError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        use PackageValidationError::*;
        match self {
            Open { path, source } => write!(f, "Unable to open `{}`: {}.", path.display(), source),
            JsonDecode(source) => {
                write!(f, "Unable to decode JSON for package manifest: {}.", source)
            }
            MissingMetaFar => write!(f, "The package seems to be missing a meta/ directory."),
            ReadArchive(source) => write!(f, "Unable to read the package's meta.far: {}.", source),
            InvalidComponents(components) => {
                for (name, error) in components {
                    write!(f, "\n└── {}: {}", name, error)?;
                    if let Some(source) = error.source() {
                        write!(f, "\n    └── {}", source)?;
                    }
                }
                Ok(())
            }
        }
    }
}
