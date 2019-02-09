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

    /// Don't include default zx types
    #[structopt(long = "omit-zx")]
    no_zx: bool,
}

fn main() -> Result<(), Error> {
    let mut args: Vec<String> = std::env::args().collect();
    // @ gn integration
    if let Some(c) = args[1].chars().nth(0) {
        if c == String::from("@").chars().nth(0).unwrap() {
            // TODO better way
            let (_, f_name) = args[1].split_at(1);
            let mut f = File::open(f_name).expect("file not found");
            let mut contents = String::new();
            f.read_to_string(&mut contents).expect("something went wrong reading the file");
            // program name
            args = vec![args[0].clone()];
            args.extend(contents.split(" ").map(|s| s.trim().to_string()))
        }
    }

    let opt = Opt::from_iter(args);
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

    if !opt.no_zx {
        let zx_file = include_str!("../zx.banjo");
        pair_vec.push(BanjoParser::parse(Rule::file, zx_file)?);
    }
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
            eprintln!(
                "Generated library '{}' did not match --name arguement {}",
                ast.primary_namespace, name
            );
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
