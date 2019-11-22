// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A Fuchsia Driver Bind Program compiler

use std::fmt::Write;
use std::fs::File;
use std::io;
use std::path::PathBuf;
use structopt::StructOpt;

use bindc::{compiler, instruction::Instruction};

#[derive(StructOpt, Debug)]
struct Opt {
    /// Output file. The compiler emits a C header file.
    #[structopt(short = "o", long = "output", parse(from_os_str))]
    output: Option<PathBuf>,

    /// The bind program input file. This should be in the format described in
    /// //tools/bindc/README.md.
    #[structopt(parse(from_os_str))]
    input: PathBuf,

    /// The bind library input files. These may be included by the bind program. They should be in
    /// the format described in //tools/bindc/README.md.
    #[structopt(short = "i", long = "include", parse(from_os_str))]
    include: Vec<PathBuf>,
}

fn write_bind_template(instructions: Vec<Instruction>) -> Option<String> {
    let bind_count = instructions.len();
    let binding = instructions
        .into_iter()
        .map(|instr| instr.encode_pair())
        .map(|(word0, word1)| format!("{{{:#x},{:#x}}},", word0, word1))
        .collect::<String>();
    let mut output = String::new();
    output
        .write_fmt(format_args!(
            include_str!("templates/bind.h.template"),
            bind_count = bind_count,
            binding = binding,
        ))
        .ok()?;
    Some(output)
}

fn main() {
    let opt = Opt::from_iter(std::env::args());

    let mut output: Box<dyn io::Write> = if let Some(output) = opt.output {
        Box::new(File::create(output).unwrap())
    } else {
        Box::new(io::stdout())
    };

    match compiler::compile(opt.input, &opt.include) {
        Ok(instructions) => match write_bind_template(instructions) {
            Some(out_string) => {
                let r = output.write(out_string.as_bytes());
                if r.is_err() {
                    eprintln!("Failed to write to output");
                    std::process::exit(1);
                }
            }
            None => {
                eprintln!("Failed to format output");
                std::process::exit(1);
            }
        },
        Err(err) => {
            eprintln!("{}", err);
            std::process::exit(1);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use bindc::instruction::Condition;

    #[test]
    fn zero_instructions() {
        let out_string = write_bind_template(vec![]).unwrap();
        assert!(out_string.contains("ZIRCON_DRIVER_BEGIN(Driver, Ops, VendorName, Version, 0)"));
    }

    #[test]
    fn one_instruction() {
        let instructions = vec![Instruction::Match(Condition::Always)];
        let out_string = write_bind_template(instructions).unwrap();
        assert!(out_string.contains("ZIRCON_DRIVER_BEGIN(Driver, Ops, VendorName, Version, 1)"));
        assert!(out_string.contains("{0x10,0x0}"));
    }
}
