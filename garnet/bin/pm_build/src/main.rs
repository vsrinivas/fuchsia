// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io;
use std::path::PathBuf;
use structopt::StructOpt;

#[derive(Debug, StructOpt)]
struct Options {
    #[structopt(
        long = "package_creation_manifest",
        help = "path to package creation manifest file"
    )]
    package_manifest: PathBuf,
}

fn main() -> io::Result<()> {
    let options = Options::from_args();
    let creation_manifest =
        fuchsia_pkg::CreationManifest::from_json(std::fs::File::open(options.package_manifest)?)
            .unwrap();
    println!("{:?}", creation_manifest);
    Ok(())
}
