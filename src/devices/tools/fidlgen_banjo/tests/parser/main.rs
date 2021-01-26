// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidlgen_banjo_lib::fidl::FidlIr,
    std::{
        fs::{write, File},
        path::PathBuf,
    },
    structopt::StructOpt,
};

#[derive(StructOpt, Debug)]
struct Flags {
    #[structopt(long)]
    input: PathBuf,

    #[structopt(long)]
    stamp: PathBuf,
}

fn main() -> Result<(), Error> {
    let flags = Flags::from_args();
    let _result: FidlIr = serde_json::from_reader(File::open(flags.input)?)?;
    write(flags.stamp, "Done!")?;
    Ok(())
}
