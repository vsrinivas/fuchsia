// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for working with structured configuration during the product assembly process.

use cm_rust::FidlIntoNative;
use fidl_fuchsia_component_config as fconfig;
use fidl_fuchsia_component_decl as fdecl;
use std::io::{Read, Seek};

/// Validate a component manifest within the `meta/` directory.
///
/// Ensures that if a component has a `config` stanza it can be paired with the specified
/// value file and that together they can produce a valid configuration for the component.
pub fn validate_component(
    manifest_path: &str,
    reader: &mut fuchsia_archive::Reader<impl Read + Seek>,
) -> Result<(), ValidationError> {
    // get the manifest and validate it
    let manifest_bytes =
        reader.read_file(manifest_path).map_err(ValidationError::ManifestMissing)?;
    let manifest: fdecl::Component = fidl::encoding::decode_persistent(&manifest_bytes)
        .map_err(ValidationError::DecodeManifest)?;
    cm_fidl_validator::validate(&manifest).map_err(ValidationError::ValidateManifest)?;
    let manifest = manifest.fidl_into_native();

    // check for config
    if let Some(config_decl) = manifest.config {
        // it's required, so find out where it's stored
        let cm_rust::ConfigValueSource::PackagePath(path) = &config_decl.value_source;
        let config_bytes = reader.read_file(&path).map_err(ValidationError::ConfigValuesMissing)?;

        // read and validate the config values
        let config_values: fconfig::ValuesData = fidl::encoding::decode_persistent(&config_bytes)
            .map_err(ValidationError::DecodeConfig)?;
        cm_fidl_validator::validate_values_data(&config_values)
            .map_err(ValidationError::ValidateConfig)?;
        let config_values = config_values.fidl_into_native();

        // we have config, make sure it's compatible with the manifest which references it
        config_encoder::ConfigFields::resolve(&config_decl, config_values)
            .map_err(ValidationError::ResolveConfig)?;
    }
    Ok(())
}

#[derive(Debug, thiserror::Error)]
pub enum ValidationError {
    #[error("Couldn't read manifest.")]
    ManifestMissing(#[source] fuchsia_archive::Error),
    #[error("Couldn't decode manifest.")]
    DecodeManifest(#[source] fidl::Error),
    #[error("Couldn't validate manifest.")]
    ValidateManifest(#[source] cm_fidl_validator::error::ErrorList),
    #[error("Couldn't read config values.")]
    ConfigValuesMissing(#[source] fuchsia_archive::Error),
    #[error("Couldn't decode config values.")]
    DecodeConfig(#[source] fidl::Error),
    #[error("Couldn't validate config values.")]
    ValidateConfig(#[source] cm_fidl_validator::error::ErrorList),
    #[error("Couldn't resolve config.")]
    ResolveConfig(#[source] config_encoder::ResolutionError),
}
