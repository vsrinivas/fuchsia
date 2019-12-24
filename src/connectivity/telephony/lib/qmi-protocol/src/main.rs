// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ast::ServiceSet;
#[cfg(test)]
use crate::tests;
use anyhow::Error;
use std::env;
use std::fs;

mod ast;
mod codegen;
#[cfg(test)]
mod tests;

fn usage() {
    println!("usage: qmigen -i <qmi json defs> -o <protocol.rs>");
    println!("");
    println!("Generates type-safe rust bindings for QMI off of a set of");
    println!("custom protocol definitions in JSON");
    ::std::process::exit(1);
}

fn main() -> Result<(), Error> {
    let mut args: Vec<String> = env::args().collect();
    if args.len() < 3 {
        usage();
    }

    if !args.contains(&String::from("-i")) && !args.contains(&String::from("-o")) {
        usage();
    }
    // extract subsets of the args
    let i_offset = args.clone().into_iter().position(|s| s == "-i").unwrap();
    let o_offset = args.clone().into_iter().position(|s| s == "-o").unwrap();
    let mut inputs = args.split_off(i_offset + 1);
    let outputs = inputs.split_off(o_offset - i_offset);

    if outputs.len() != 1 {
        eprintln!("Only one output is expected");
        ::std::process::exit(1);
    }

    // for each input, add to the Service Set code generator
    let mut svc_set = ServiceSet::new();
    let mut file = fs::File::create(&outputs[0])?;
    let mut c = codegen::Codegen::new(&mut file);
    for file in inputs.into_iter().take_while(|s| s != "-o") {
        let svc_file = fs::File::open(&file)?;
        svc_set.parse_service_file(svc_file)?;
    }

    c.codegen(svc_set)
}
