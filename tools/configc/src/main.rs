// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use argh::FromArgs;
use cm_rust::FidlIntoNative;
use fidl::encoding::{decode_persistent, encode_persistent};
use fidl_fuchsia_sys2 as fsys;
use std::{collections::BTreeMap, fs, io::Write, path::PathBuf};

/// compile a configuration value file
#[derive(Debug, FromArgs)]
struct Options {
    /// compiled manifest containing the config declaration
    #[argh(option)]
    cm: PathBuf,

    /// JSON5 file containing a single object with each config field as a top level key in an
    /// object.
    #[argh(option)]
    values: PathBuf,

    /// path to which to write configuration value file
    #[argh(option)]
    output: PathBuf,
}

fn main() -> Result<(), Error> {
    let Options { cm, values, output } = argh::from_env::<Options>();

    // load & parse the manifest
    let cm_raw = fs::read(cm).context("reading component manifest")?;
    let component: fsys::ComponentDecl =
        decode_persistent(&cm_raw).context("decoding component manifest")?;
    let component = component.fidl_into_native();
    let config_decl = component
        .config
        .as_ref()
        .ok_or_else(|| anyhow::format_err!("missing config declaration in manifest"))?;

    // load & parse the json file containing value defs
    let values_raw = fs::read_to_string(values).context("reading values JSON")?;
    let values: BTreeMap<String, serde_json::Value> =
        serde_json5::from_str(&values_raw).context("parsing values JSON")?;

    // combine the manifest and provided values
    let mut values_data = config_value_file::populate_value_file(config_decl, values)
        .context("populating config values")?;
    let encoded_output = encode_persistent(&mut values_data).context("encoding value file")?;

    // write result to value file output
    if let Some(parent) = output.parent() {
        // attempt to create all parent directories, ignore failures bc they might already exist
        std::fs::create_dir_all(parent).ok();
    }
    let mut out_file = fs::OpenOptions::new()
        .create(true)
        .truncate(true)
        .write(true)
        .open(output)
        .context("opening output file")?;
    out_file.write(&encoded_output).context("writing value file to output")?;

    Ok(())
}
