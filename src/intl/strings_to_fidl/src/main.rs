// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # A message ID generator for the `strings.xml` format
//!
//! This crate contains a binary `strings_to_fidl`, which generates message IDs from the
//! Android-formatted [strings.xml resource file][strings-xml], as a set of FIDL constants.
//! Complete support is not a specific goal, rather the generator will be amended to include more
//! features as more features are needed.
//!
//! [strings-xml]: https://developer.android.com/guide/topics/resources/string-resource

// TODO(fmil): temporary, until all code is used.
#![allow(dead_code)]

use {
    anyhow::Context,
    anyhow::Error,
    anyhow::Result,
    intl_strings::{message_ids, parser, veprintln},
    std::env,
    std::fs::File,
    std::io,
    std::path::PathBuf,
    structopt::StructOpt,
    xml::reader::EventReader,
};

// TODO(fmil): Add usage link here.
#[derive(Debug, StructOpt)]
#[structopt(name = "Extracts information from strings.xml into FIDL")]
struct Args {
    #[structopt(long = "input", help = "The path to the input strings.xml format file")]
    input: PathBuf,
    #[structopt(long = "output", help = "The path to the output FIDL file")]
    output: PathBuf,
    #[structopt(long = "verbose", help = "Verbose output, for debugging")]
    verbose: bool,
    #[structopt(
        long = "library",
        default_value = "fuchsia.intl.l10n",
        help = "The FIDL library name for which to generate the code"
    )]
    library: String,
}

/// Open the needed files, and handle usual errors.
fn open_files(args: &Args) -> Result<(impl io::Read, impl io::Write), Error> {
    let input_str = args.input.to_str().with_context(|| {
        format!("input filename is not utf-8, what? Use --verbose flag to print the value.")
    })?;
    let input = io::BufReader::new(
        File::open(&args.input)
            .with_context(|| format!("could not open input file: {}", input_str))?,
    );
    let output_str = args.output.to_str().with_context(|| {
        format!("output filename is not utf-8, what? Use --verbose flag to print the value.")
    })?;
    let output = File::create(&args.output)
        .with_context(|| format!("could not open output file: {}", output_str))?;
    Ok((input, output))
}

fn main() -> Result<(), Error> {
    env::set_var("RUST_BACKTRACE", "full");
    let args: Args = Args::from_args();
    veprintln!(args.verbose, "args: {:?}", args);

    let (input, mut output) = open_files(&args).with_context(|| "while opening files")?;
    let reader = EventReader::new(input);

    let mut parser = parser::Instance::new(args.verbose);
    let dictionary = parser.parse(reader).with_context(|| "while parsing dictionary")?;
    let model = message_ids::from_dictionary(dictionary, &args.library)?;
    message_ids::render(model, &mut output)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn basic() -> Result<(), Error> {
        Ok(())
    }
}
