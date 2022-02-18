// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_ir_lib::fidl::*,
    fidlgen_banjo_lib::backends::*,
    std::{fs::File, io::BufReader, path::PathBuf, str::FromStr},
    structopt::StructOpt,
};

#[derive(Debug)]
enum BackendName {
    C,
    Cpp,
    CppInternal,
    CppMock,
    Rust,
}

impl FromStr for BackendName {
    type Err = String;

    fn from_str(s: &str) -> Result<BackendName, Self::Err> {
        match s.to_lowercase().as_str() {
            "c" => Ok(BackendName::C),
            "cpp" => Ok(BackendName::Cpp),
            "cpp_internal" => Ok(BackendName::CppInternal),
            "cpp_mock" => Ok(BackendName::CppMock),
            "rust" => Ok(BackendName::Rust),
            _ => Err(format!(
                "Unrecognized backend for fidlgen_banjo. \
                 Current valid ones are: c, cpp, cpp_internal, cpp_mock, rust"
            )),
        }
    }
}

#[derive(StructOpt, Debug)]
#[structopt(name = "fidlgen_banjo")]
struct Flags {
    #[structopt(short = "i", long = "ir")]
    ir: PathBuf,

    #[structopt(short = "b", long = "backend")]
    backend: BackendName,

    #[structopt(short = "o", long = "output")]
    output: PathBuf,
}

fn main() -> Result<(), Error> {
    let flags = Flags::from_args();
    let mut output = File::create(flags.output)?;
    let mut backend: Box<dyn Backend<'_, _>> = match flags.backend {
        BackendName::C => Box::new(CBackend::new(&mut output)),
        BackendName::Cpp => Box::new(CppBackend::new(&mut output)),
        BackendName::CppInternal => Box::new(CppInternalBackend::new(&mut output)),
        BackendName::CppMock => Box::new(CppMockBackend::new(&mut output)),
        BackendName::Rust => Box::new(RustBackend::new(&mut output)),
    };
    let mut ir: FidlIr = serde_json::from_reader(BufReader::new(File::open(flags.ir)?))?;
    ir.build()?;
    backend.codegen(ir)
}
