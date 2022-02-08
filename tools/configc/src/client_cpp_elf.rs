// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use argh::FromArgs;
use cm_rust::FidlIntoNative;
use config_client::cpp_elf::{create_cpp_elf_wrapper, CppSource};
use fidl::encoding::decode_persistent;
use fidl_fuchsia_component_decl as fdecl;
use std::{
    fs,
    io::Write,
    path::PathBuf,
    process::{Command as Process, Stdio},
};

#[derive(FromArgs, PartialEq, Debug)]
/// Generates a C++ ELF client library from a given manifest.
/// This also requires a FIDL client library to be generated for the given manifest.
#[argh(subcommand, name = "cpp_elf")]
pub struct GenerateCppElfSource {
    /// compiled manifest containing the config declaration
    #[argh(option)]
    cm: PathBuf,

    /// path to which to output source files
    #[argh(option)]
    output_path: PathBuf,

    /// namespace used by library
    #[argh(option)]
    namespace: String,

    /// name for the internal FIDL library
    #[argh(option)]
    fidl_library_name: String,

    /// path to clang-format binary
    #[argh(option)]
    clang_format: PathBuf,
}

impl GenerateCppElfSource {
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

        let CppSource { h_source, cc_source } =
            create_cpp_elf_wrapper(config_decl, self.namespace, self.fidl_library_name)
                .context("creating cpp elf wrapper")?;

        let formatted_cc_source = format_source(&self.clang_format, cc_source)?;
        let formatted_h_source = format_source(&self.clang_format, h_source)?;

        // Make sure directories exist
        fs::create_dir_all(&self.output_path)
            .context("Failed to create directories for source files")?;

        let mut cc_out_file = fs::OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .open(self.output_path.join("config.cc"))
            .context("opening cc output file")?;
        cc_out_file.write(formatted_cc_source.as_bytes()).context("writing cc file to output")?;

        let mut h_out_file = fs::OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .open(self.output_path.join("config.h"))
            .context("opening h output file")?;
        h_out_file.write(formatted_h_source.as_bytes()).context("writing h file to output")?;

        Ok(())
    }
}

fn format_source(clang_format: &PathBuf, contents: String) -> Result<String, Error> {
    // TODO(fxbug.dev/49757) Use --style=file and copy the .clang-format file to the correct location.
    // An alternate way to do this is to load the config directly from .clang_format and put the
    // style as JSON in quotes.
    let mut process = Process::new(clang_format)
        .arg("--style=google")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .context("could not spawn clang-format process")?;
    process
        .stdin
        .as_mut()
        .ok_or(format_err!("could not get stdin for clang-format process"))?
        .write_all(contents.as_bytes())
        .context("Could not write unformatted source to stdin of clang-format")?;
    let output =
        process.wait_with_output().context("could not wait for clang-format process to exit")?;

    if !output.status.success() {
        return Err(format_err!("failed to format cpp source: {:#?}", output));
    }

    let output = String::from_utf8(output.stdout)
        .context("output from clang-format is not UTF-8 compatible")?;
    Ok(output)
}
