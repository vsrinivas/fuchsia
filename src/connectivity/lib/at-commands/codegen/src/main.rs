// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    codegen_lib::{definition, parser},
    log::info,
    simplelog::{Config as LogConfig, LevelFilter, SimpleLogger},
    std::{
        fs::File,
        io::{Read, Write},
        path::PathBuf,
    },
};

// This code just confirms we've correctly linked in the parsers.
static SOME_CODE: &'static str = r#"use crate::parser::{command_parser, response_parser};

pub fn unused() {
    let _ = command_parser::parse(&String::from(""));
    let _ = response_parser::parse(&String::from(""));
}
"#;

#[derive(argh::FromArgs)]
/// Generate rust code for AT command parsing from description files.
struct Args {
    /// path to an input file specifying AT commands
    #[argh(option, short = 'i')]
    input: Vec<PathBuf>,

    /// path to an output file to generate code into
    #[argh(option, short = 'o')]
    output: PathBuf,
}

fn main() -> Result<()> {
    SimpleLogger::init(LevelFilter::Info, LogConfig::default())
        .context("Unable to initialize logger.")?;

    let args: Args = argh::from_env();

    let mut parsed_definitions = Vec::new();

    for file in &args.input {
        let parsed_definition = parse_definition_file(&file);
        parsed_definitions.push(parsed_definition);
    }

    let mut output_file =
        File::create(&args.output) // Create or truncate file.
            .with_context(|| format!("Unable to open output file {:?}", &args.output))?;
    info!("Writing generated AT code to {:?} ", &args.output);
    output_file
        .write(SOME_CODE.as_bytes())
        .with_context(|| format!("Unable to write to output file {:?}", &args.output))?;
    info!(
        "Successfully wrote {:} bytes of generated AT code to {:?} ",
        SOME_CODE.len(),
        &args.output
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
