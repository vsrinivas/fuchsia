// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidlgen_banjo_lib::{backends::*, fidl::FidlIr},
    std::{fs::File, path::PathBuf, str::FromStr},
    structopt::StructOpt,
};

#[derive(Debug)]
enum BackendName {
    DummyC,
    C,
}

impl FromStr for BackendName {
    type Err = String;

    fn from_str(s: &str) -> Result<BackendName, Self::Err> {
        match s.to_lowercase().as_str() {
            "dummy_c" => Ok(BackendName::DummyC),
            "c" => Ok(BackendName::C),
            _ => Err(format!(
                "Unrecognized backend for fidlgen_banjo. \
                 Current valid ones are: dummy_c"
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
        BackendName::DummyC => Box::new(DummyCBackend::new(&mut output)),
        BackendName::C => Box::new(CBackend::new(&mut output)),
    };
    let ir: FidlIr = serde_json::from_reader(File::open(flags.ir)?)?;
    backend.codegen(ir)
}
