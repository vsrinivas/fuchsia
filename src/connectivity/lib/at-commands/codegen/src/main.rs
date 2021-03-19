// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    codegen_lib::{codegen, definition, parser},
    log::info,
    simplelog::{Config as LogConfig, LevelFilter, SimpleLogger},
    std::{fs::File, io::Read, path::PathBuf},
};

// This code just confirms we've correctly linked in the parsers.
#[derive(argh::FromArgs)]
/// Generate rust code for AT command parsing from description files.
struct Args {
    /// path to an input file specifying AT commands
    #[argh(option)]
    input: Vec<PathBuf>,

    /// path to an output file to generate type definition code into
    #[argh(option)]
    output_types: PathBuf,

    /// path to an output file to generate translation code into
    #[argh(option)]
    output_translate: PathBuf,

    /// suppress log output below Error severity
    #[argh(switch)]
    quiet: bool,
}

fn main() -> Result<()> {
    let args: Args = argh::from_env();

    let log_level = if args.quiet { LevelFilter::Error } else { LevelFilter::Info };
    SimpleLogger::init(log_level, LogConfig::default()).context("Unable to initialize logger.")?;

    let mut parsed_definitions = Vec::new();

    for file in &args.input {
        let mut parsed_definition = parse_definition_file(&file)?;
        parsed_definitions.append(&mut parsed_definition);
    }

    let mut output_types_file =
        File::create(&args.output_types) // Create or truncate file.
            .with_context(|| format!("Unable to open output file {:?}", &args.output_types))?;
    info!("Writing generated AT type definitions to {:?} ", &args.output_types);

    let mut output_translate_file =
        File::create(&args.output_translate) // Create or truncate file.
            .with_context(|| format!("Unable to open output file {:?}", &args.output_translate))?;
    info!("Writing generated AT translation code to {:?} ", &args.output_translate);

    codegen::codegen(&mut output_types_file, &mut output_translate_file, &parsed_definitions)?;

    info!(
        "Successfully wrote generated AT code to {:?} and {:?}",
        &args.output_types, &args.output_translate,
    );

    Ok(())
}

fn parse_definition_file(file: &PathBuf) -> Result<Vec<definition::Definition>> {
    let mut input_file =
        File::open(&file).with_context(|| format!("Unable to open input file {:?}", &file))?;
    info!("Reading AT defintions from {:?} ", &file);

    let mut input_file_contents = String::new();
    let read_bytes = input_file
        .read_to_string(&mut input_file_contents)
        .with_context(|| format!("Unable to read input file {:?}", &file))?;
    info!("Successfully read {:} bytes from AT defintions file {:?} ", read_bytes, &file);

    let parsed_definition = parser::parse(&input_file_contents)
        .with_context(|| format!("Unable to parse input file {:?}", &file))?;
    info!("Successfully parsed AT definitions from {:?} ", &file);

    info!(""); // Blank line between files in log output.

    Ok(parsed_definition)
}
