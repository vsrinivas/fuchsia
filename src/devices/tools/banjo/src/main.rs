// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
mod fidl;
mod parser;

#[derive(Debug)]
enum BackendName {
    C,
    Cpp(backends::CppSubtype),
    Rust,
    Json,
    Ast,
    Fidlcat,
    Syzkaller,
}

impl FromStr for BackendName {
    type Err = String;

    fn from_str(s: &str) -> Result<BackendName, Self::Err> {
        match s.to_lowercase().as_str() {
            "c" => Ok(BackendName::C),
            "cpp" => Ok(BackendName::Cpp(backends::CppSubtype::Base)),
            "cpp_mock" => Ok(BackendName::Cpp(backends::CppSubtype::Mock)),
            "cpp_i" => Ok(BackendName::Cpp(backends::CppSubtype::Internal)),
            "rust" => Ok(BackendName::Rust),
            "json" => Ok(BackendName::Json),
            "ast" => Ok(BackendName::Ast),
            "fidlcat" => Ok(BackendName::Fidlcat),
            "syzkaller" => Ok(BackendName::Syzkaller),
            _ => Err(format!(
                "Unrecognized backend for banjo. Current valid ones are: \
                 C, Cpp, CppMock, Rust, Ast, Fidlcat"
            )),
        }
    }
}

/// A tool for generating Fuchsia driver protocol interfaces.
///
/// All the following arguments can also be placed in a file that has
/// a path prefixed with @. That file will be read in and used instead
/// of command line arguments.
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

    /// Banjo IDL files to process. These are expected to be in the format described by
    /// https://fuchsia.googlesource.com/fuchsia/+/HEAD/docs/zircon/ddk/banjo-tutorial.md#reference
    #[structopt(short = "f", long = "files", parse(from_os_str))]
    input: Vec<PathBuf>,

    /// FIDL IR JSON files to process. These files are expected to be in the format described by
    /// https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/tools/fidl/schema.json
    #[structopt(short = "i", long = "fidl-ir", parse(from_os_str))]
    fidl_ir: Vec<PathBuf>,

    /// Don't include default zx types
    #[structopt(long = "omit-zx")]
    no_zx: bool,
}

fn main() -> Result<(), Error> {
    let mut args: Vec<String> = std::env::args().collect();

    if args.len() > 1 {
        // @ gn integration
        if let Some(c) = args[1].chars().nth(0) {
            if c == String::from("@").chars().nth(0).unwrap() {
                let (_, f_name) = args[1].split_at(1);
                let mut f = File::open(f_name).expect("file not found");
                let mut contents = String::new();
                f.read_to_string(&mut contents).expect("something went wrong reading the file");
                // program name
                args = vec![args[0].clone()];
                // program arguments
                args.extend(contents.split(|c| c == ' ' || c == '\n').filter_map(|s| {
                    let resp = s.trim().to_string();
                    if resp != "" {
                        Some(resp)
                    } else {
                        None
                    }
                }));
            }
        }
    }

    let opt = Opt::from_iter(args);
    let mut pair_vec = Vec::new();
    let mut fidl_vec = Vec::new();
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

    let fidl_files: Vec<String> = opt
        .fidl_ir
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
    for file in fidl_files.iter() {
        fidl_vec.push(serde_json::from_str(file.as_str())?);
    }

    let ast = BanjoAst::parse(pair_vec, fidl_vec)?;
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
        BackendName::Cpp(subtype) => Box::new(CppBackend::new(&mut output, subtype)),
        BackendName::Ast => Box::new(AstBackend::new(&mut output)),
        BackendName::Fidlcat => Box::new(FidlcatBackend::new(&mut output)),
        BackendName::Syzkaller => Box::new(SyzkallerBackend::new(&mut output)),
        e => {
            eprintln!("{:?} backend is not yet implemented", e);
            ::std::process::exit(1);
        }
    };
    backend.codegen(ast)
}
