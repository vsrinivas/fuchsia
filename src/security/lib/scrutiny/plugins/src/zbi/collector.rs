// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::zbi::collection::{BootFsCollection, CmdlineCollection, ZbiError},
    anyhow::{bail, format_err, Context, Result},
    fuchsia_hash::Hash,
    fuchsia_url::{PackageName, PackageVariant},
    scrutiny::prelude::{DataCollector, DataModel},
    scrutiny_utils::{
        artifact::{ArtifactReader, FileArtifactReader},
        bootfs::BootfsReader,
        key_value::parse_key_value,
        package::{open_update_package, read_content_blob},
        url::from_package_name_variant_path,
        zbi::{ZbiReader, ZbiType},
    },
    std::collections::HashMap,
    std::path::PathBuf,
    std::str::FromStr,
    std::sync::Arc,
};

/// The path of the file in bootfs that lists all the bootfs packages.
static BOOT_PACKAGE_INDEX: &str = "data/bootfs_packages";

/// A collector that returns the bootfs files in a product.
#[derive(Default)]
pub struct BootFsCollector;

impl DataCollector for BootFsCollector {
    fn collect(&self, model: Arc<DataModel>) -> Result<()> {
        let model_config = model.config();
        let update_package_path = model_config.update_package_path();
        let blobs_directory = model_config.blobs_directory();

        // Initialize artifact reader; early exit on initialization failure.
        let mut artifact_reader: Box<dyn ArtifactReader> =
            Box::new(FileArtifactReader::new(&PathBuf::new(), &blobs_directory));

        let mut far_reader = open_update_package(&update_package_path, &mut artifact_reader)
            .map_err(|err| ZbiError::FailedToOpenUpdatePackage {
                update_package_path: update_package_path.clone(),
                io_error: format!("{:?}", err),
            })?;
        let zbi_buffer = read_content_blob(&mut far_reader, &mut artifact_reader, "zbi.signed")
            .or_else(|signed_err| {
                read_content_blob(&mut far_reader, &mut artifact_reader, "zbi").map_err(|err| {
                    ZbiError::FailedToReadZbi {
                        update_package_path: update_package_path.clone(),
                        io_error: format!("{:?}\n{:?}", signed_err, err),
                    }
                })
            })?;
        let mut reader = ZbiReader::new(zbi_buffer);
        let zbi_sections = reader.parse().map_err(|zbi_error| ZbiError::FailedToParseZbi {
            update_package_path: update_package_path.clone(),
            zbi_error: zbi_error.to_string(),
        })?;

        for section in zbi_sections.iter() {
            if section.section_type == ZbiType::StorageBootfs {
                let mut bootfs_reader = BootfsReader::new(section.buffer.clone());
                let bootfs_data = bootfs_reader.parse().map_err(|bootfs_error| {
                    ZbiError::FailedToParseBootfs {
                        update_package_path: update_package_path.clone(),
                        bootfs_error: bootfs_error.to_string(),
                    }
                })?;

                // Add the bootfs files.
                let files = bootfs_data.iter().map(|(k, _)| k.clone()).collect();
                let mut collection = BootFsCollection { files, packages: Some(HashMap::new()) };

                // Add the bootfs packages.
                let mut package_index_found = false;
                for (file_name, data) in bootfs_data.iter() {
                    if file_name == BOOT_PACKAGE_INDEX {
                        // Ensure that we only find a single package index file in bootfs.
                        if package_index_found {
                            bail!("Multiple bootfs package index files found");
                        }
                        package_index_found = true;

                        let bootfs_pkg_contents = std::str::from_utf8(&data)?;
                        let bootfs_pkgs = parse_key_value(bootfs_pkg_contents)?;

                        collection.packages = Some(bootfs_pkgs
                            .into_iter()
                            .map(|(name_and_variant, merkle)| {
                                let url = from_package_name_variant_path(name_and_variant)?;
                                let merkle = Hash::from_str(&merkle)?;
                                Ok(((url.name().clone(), url.variant().map(|v| v.clone())), merkle))
                            })
                            // Handle errors via collect
                            // Iter<Result<_, __>> into Result<Vec<_>, __>.
                            .collect::<Result<Vec<((PackageName, Option<PackageVariant>), Hash)>>>()
                            .map_err(|err| {
                                format_err!(
                                    "Failed to parse bootfs package index name/variant=merkle: {:?}",
                                    err)
                            })?
                            // Collect Vec<(_, __)> into HashMap<_, __>.
                            .into_iter()
                            .collect::<HashMap<(PackageName, Option<PackageVariant>), Hash>>());
                    }
                }

                model.set(collection).with_context(|| {
                    format!(
                        "Failed to collect bootfs files in ZBI from update package at {:?}",
                        update_package_path,
                    )
                })?;
            }
        }
        Ok(())
    }
}

/// A collector that returns the kernel cmdline in a product.
#[derive(Default)]
pub struct CmdlineCollector;

impl DataCollector for CmdlineCollector {
    fn collect(&self, model: Arc<DataModel>) -> Result<()> {
        let model_config = model.config();
        let update_package_path = model_config.update_package_path();
        let blobs_directory = model_config.blobs_directory();

        // Initialize artifact reader; early exit on initialization failure.
        let mut artifact_reader: Box<dyn ArtifactReader> =
            Box::new(FileArtifactReader::new(&PathBuf::new(), &blobs_directory));

        let mut far_reader = open_update_package(&update_package_path, &mut artifact_reader)
            .map_err(|err| ZbiError::FailedToOpenUpdatePackage {
                update_package_path: update_package_path.clone(),
                io_error: format!("{:?}", err),
            })?;
        let zbi_buffer = read_content_blob(&mut far_reader, &mut artifact_reader, "zbi.signed")
            .or_else(|signed_err| {
                read_content_blob(&mut far_reader, &mut artifact_reader, "zbi").map_err(|err| {
                    ZbiError::FailedToReadZbi {
                        update_package_path: update_package_path.clone(),
                        io_error: format!("{:?}\n{:?}", signed_err, err),
                    }
                })
            })?;
        let mut reader = ZbiReader::new(zbi_buffer);
        let zbi_sections = reader.parse().map_err(|zbi_error| ZbiError::FailedToParseZbi {
            update_package_path: update_package_path.clone(),
            zbi_error: zbi_error.to_string(),
        })?;

        let mut cmdline_found = false;
        for section in zbi_sections.iter() {
            if section.section_type == ZbiType::Cmdline {
                // Ensure that we only find a single package index file in bootfs.
                if cmdline_found {
                    bail!("Multiple kernel cmdlines found");
                }
                cmdline_found = true;

                let mut cmdline_buffer = section.buffer.clone();
                // The cmdline.blk contains a trailing 0.
                cmdline_buffer.truncate(cmdline_buffer.len() - 1);
                let cmdline_str = std::str::from_utf8(&cmdline_buffer)
                    .context("Failed to convert kernel arguments to utf-8")?;
                let mut cmdline =
                    cmdline_str.split(' ').map(ToString::to_string).collect::<Vec<String>>();
                cmdline.sort();

                let collection = CmdlineCollection { cmdline };
                model.set(collection).with_context(|| {
                    format!(
                        "Failed to collect cmdline in ZBI from update package at {:?}",
                        update_package_path,
                    )
                })?;
            }
        }
        if !cmdline_found {
            bail!("Could not find a kernel cmdline in the ZBI");
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::{BootFsCollector, CmdlineCollector};
    use crate::zbi::collection::{BootFsCollection, CmdlineCollection};
    use scrutiny::prelude::{DataCollector, DataModel};
    use scrutiny_config::ModelConfig;
    use std::sync::Arc;

    const PRODUCT_BUNDLE_PATH: &str = env!("PRODUCT_BUNDLE_PATH");

    #[test]
    fn bootfs() {
        let model = ModelConfig::from_product_bundle(PRODUCT_BUNDLE_PATH).unwrap();
        let data_model = Arc::new(DataModel::new(model).unwrap());
        let collector = BootFsCollector {};
        collector.collect(data_model.clone()).unwrap();
        let collection = data_model.get::<BootFsCollection>().unwrap();
        assert!(collection.files.contains(&"path/to/version".to_string()));
    }

    #[test]
    fn cmdline() {
        let model = ModelConfig::from_product_bundle(PRODUCT_BUNDLE_PATH).unwrap();
        let data_model = Arc::new(DataModel::new(model).unwrap());
        let collector = CmdlineCollector {};
        collector.collect(data_model.clone()).unwrap();
        let collection = data_model.get::<CmdlineCollection>().unwrap();
        assert_eq!(collection.cmdline, vec!["abc".to_string(), "def".to_string(),]);
    }
}
