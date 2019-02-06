// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use {
    crate::{
        ast::BanjoAst,
        backends::*,
        parser::{BanjoParser, Rule},
    },
    failure::Error,
    pest::Parser,
    std::{fs::File, io, io::Read, path::PathBuf, str::FromStr},
    structopt::StructOpt,
};

mod ast;
mod backends;
mod parser;

#[derive(Debug)]
enum BackendName {
    C,
    Cpp,
    CppInternal,
    Rust,
    Json,
    Ast,
}

impl FromStr for BackendName {
    type Err = String;

    fn from_str(s: &str) -> Result<BackendName, Self::Err> {
        match s.to_lowercase().as_str() {
            "c" => Ok(BackendName::C),
            "cpp" => Ok(BackendName::Cpp),
            "cpp_i" => Ok(BackendName::CppInternal),
            "rust" => Ok(BackendName::Rust),
            "json" => Ok(BackendName::Json),
            "ast" => Ok(BackendName::Ast),
            _ => Err(format!(
                "Unrecognized backend for banjo. Current valid ones are: C, Cpp, Rust, Ast"
            )),
        }
    }
}

/// A tool for generating Fuchsia driver protocol interfaces
#[derive(StructOpt, Debug)]
#[structopt(name = "banjo")]
struct Opt {
    /// Activate debug mode
    #[structopt(short = "d", long = "debug")]
    debug: bool,

    /// Library name.
    #[structopt(short = "n", long = "name")]
    name: Option<String>,

    /// Output file
    #[structopt(short = "o", long = "output", parse(from_os_str))]
    output: Option<PathBuf>,

    /// Backend code generator to use
    #[structopt(short = "b", long = "backend")]
    backend: BackendName,

    /// Files to process
    #[structopt(short = "f", long = "files", parse(from_os_str))]
    input: Vec<PathBuf>,
}

fn main() -> Result<(), Error> {
    let opt = Opt::from_args();
    let mut pair_vec = Vec::new();
    let files: Vec<String> = opt
        .input
        .iter()
        .map(|filename| {
            let mut f = File::open(filename).expect(&format!("{} not found", filename.display()));
            let mut contents = String::new();
            f.read_to_string(&mut contents).expect("something went wrong reading the file");
            contents
        })
        .collect();

    for file in files.iter() {
        pair_vec.push(BanjoParser::parse(Rule::file, file.as_str())?);
    }

    let ast = BanjoAst::parse(pair_vec)?;
    let mut output: Box<dyn io::Write> = if let Some(output) = opt.output {
        Box::new(File::create(output)?)
    } else {
        Box::new(io::stdout())
    };

    if let Some(name) = opt.name {
        if name != ast.primary_namespace {
            eprintln!("Generated library '{}' did not match --name arguement {}",
                      ast.primary_namespace, name);
            ::std::process::exit(1);
        }
    }

    let mut backend: Box<dyn Backend<_>> = match opt.backend {
        BackendName::C => Box::new(CBackend::new(&mut output)),
        BackendName::Cpp => Box::new(CppBackend::new(&mut output)),
        BackendName::CppInternal => Box::new(CppInternalBackend::new(&mut output)),
        BackendName::Ast => Box::new(AstBackend::new(&mut output)),
        e => {
            eprintln!("{:?} backend is not yet implemented", e);
            ::std::process::exit(1);
        }
    };
    backend.codegen(ast)
}
