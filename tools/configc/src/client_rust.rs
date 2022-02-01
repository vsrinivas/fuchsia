// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use argh::FromArgs;
use cm_rust::FidlIntoNative;
use fidl::encoding::decode_persistent;
use fidl_fuchsia_component_decl as fdecl;
use std::{
    fs,
    io::Write,
    path::PathBuf,
    process::{Command as Process, Stdio},
};

#[derive(FromArgs, PartialEq, Debug)]
/// Generates a Rust client library from a given manifest.
/// This also requires a FIDL client library to be generated for the given manifest.
#[argh(subcommand, name = "rust")]
pub struct GenerateRustSource {
    /// compiled manifest containing the config declaration
    #[argh(option)]
    cm: PathBuf,

    /// path to which to output Rust source file
    #[argh(option)]
    output: PathBuf,

    /// name for the internal FIDL library
    #[argh(option)]
    fidl_library_name: String,

    /// path to rustfmt binary
    #[argh(option)]
    rustfmt: PathBuf,

    /// path to rustfmt.toml configuration file
    #[argh(option)]
    rustfmt_config: PathBuf,
}

impl GenerateRustSource {
    pub fn generate(self) -> Result<(), Error> {
        // load & parse the manifest
        let cm_raw = fs::read(self.cm).context("reading component manifest")?;
        let component: fdecl::Component =
            decode_persistent(&cm_raw).context("decoding component manifest")?;
        let component = component.fidl_into_native();
        let config_decl = component
            .config
            .as_ref()
            .ok_or_else(|| anyhow::format_err!("missing config declaration in manifest"))?;

        let rust_contents =
            config_client::rust::create_rust_wrapper(config_decl, self.fidl_library_name)
                .context("creating rust wrapper")?;

        let formatted_rust_contents =
            format_source(self.rustfmt, self.rustfmt_config, rust_contents)?;

        let mut rust_out_file = fs::OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .open(self.output)
            .context("opening output file")?;
        rust_out_file
            .write(formatted_rust_contents.as_bytes())
            .context("writing Rust file to output")?;

        Ok(())
    }
}

fn format_source(
    rustfmt: PathBuf,
    rustfmt_config: PathBuf,
    contents: String,
) -> Result<String, Error> {
    let mut process = Process::new(rustfmt)
        .arg("--config-path")
        .arg(rustfmt_config)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .context("could not spawn rustfmt process")?;
    process
        .stdin
        .as_mut()
        .ok_or(format_err!("could not get stdin for rustfmt process"))?
        .write_all(contents.as_bytes())
        .context("Could not write unformatted source to stdin of rustfmt")?;
    let output =
        process.wait_with_output().context("could not wait for rustfmt process to exit")?;

    if !output.status.success() {
        return Err(format_err!("failed to format rust source: {:#?}", output));
    }

    let output =
        String::from_utf8(output.stdout).context("output from rustfmt is not UTF-8 compatible")?;
    Ok(output)
}
