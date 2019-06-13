// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod errors;
use errors::PmBuildError;

use std::io::Read;
use std::path::PathBuf;
use structopt::StructOpt;

#[derive(Debug, StructOpt)]
struct Options {
    #[structopt(long = "creation-manifest", help = "path to creation manifest file")]
    creation_manifest: PathBuf,

    #[structopt(long = "meta-package", help = "path to meta/package file")]
    meta_package: PathBuf,

    #[structopt(
        long = "output",
        help = "path to write the Fuchsia archive representing the package"
    )]
    output: PathBuf,
}

fn main() -> Result<(), PmBuildError> {
    let options = Options::from_args();
    let creation_manifest =
        fuchsia_pkg::CreationManifest::from_json(std::fs::File::open(options.creation_manifest)?)?;
    let meta_package =
        fuchsia_pkg::MetaPackage::deserialize(std::fs::File::open(options.meta_package)?)?;
    let output = std::fs::File::create(options.output)?;
    fuchsia_pkg::build(&creation_manifest, &meta_package, output)?;
    Ok(())
}
