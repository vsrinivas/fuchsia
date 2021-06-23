// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs;
use std::path::PathBuf;
use structopt::StructOpt;

use wayland_scanner_lib::{Codegen, Parser, Protocol};

/// Generates wayland server bindings for the given protocol file.
#[derive(StructOpt, Debug)]
struct Options {
    /// Input XML file
    #[structopt(short = "i", long = "input", parse(from_os_str))]
    input: PathBuf,
    /// Generated rust source
    #[structopt(short = "o", long = "output", parse(from_os_str))]
    output: PathBuf,
    /// Additional crate dependencies of this protocol. These should be the
    /// names of any additional crates that the generated module will depend on.
    /// This just emits a `use <dep>::*` for each crate, so these crates must
    /// also be provided to properly build the generated module.
    #[structopt(name = "dep", short = "d", long = "dep")]
    dependencies: Vec<String>,
}

fn main() {
    let options = Options::from_args();

    // Open input/output files.
    let infile = match fs::File::open(&options.input) {
        Ok(file) => file,
        Err(_) => {
            println!("Failed to open input file {:?}", &options.input);
            return;
        }
    };
    let outfile = match fs::File::create(&options.output) {
        Ok(file) => file,
        Err(_) => {
            println!("Failed to open output file {:?}", &options.output);
            return;
        }
    };

    // Parse XML and generate rust module.
    let mut parser = Parser::new(infile);
    let mut codegen = Codegen::new(outfile);
    let parse_tree = match parser.read_document() {
        Ok(parse_tree) => parse_tree,
        Err(msg) => {
            println!("Failed to parse document {}", msg);
            return;
        }
    };
    let protocol = match Protocol::from_parse_tree(parse_tree) {
        Ok(protocol) => protocol,
        Err(msg) => {
            println!("Failed to build AST {}", msg);
            return;
        }
    };
    if let Err(e) = codegen.codegen(protocol, options.dependencies.as_slice()) {
        println!("Failed to codegen rust module {}", e);
    }
}
