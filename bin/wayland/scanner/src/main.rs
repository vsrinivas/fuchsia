// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use std::env;
use std::fs;

use wayland_scanner_lib::{Codegen, Parser, Protocol};

fn usage(exe: &str) {
    println!("usage: {} <in_protocol.xml> <out_protocol.rs>", exe);
    println!("");
    println!("Generates server bindings for the given protocol file.");
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 3 {
        usage(&args[0]);
        return;
    }

    // Open input/output files.
    let infile = match fs::File::open(&args[1]) {
        Ok(file) => file,
        Err(_) => {
            println!("Failed to open input file {}", &args[1]);
            return;
        }
    };
    let outfile = match fs::File::create(&args[2]) {
        Ok(file) => file,
        Err(_) => {
            println!("Failed to open output file {}", &args[2]);
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
    if let Err(e) = codegen.codegen(protocol) {
        println!("Failed to codegen rust module {}", e);
    }
}
