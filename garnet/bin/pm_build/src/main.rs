// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod errors;
use errors::PmBuildError;

use mundane::public::ed25519::{Ed25519PrivKey, Ed25519PubKey};
use std::io::Read;
use std::path::PathBuf;
use structopt::StructOpt;

#[derive(Debug, StructOpt)]
struct Options {
    #[structopt(long = "creation-manifest", help = "path to creation manifest file")]
    creation_manifest: PathBuf,

    #[structopt(long = "signing-key", help = "path to signing key")]
    signing_key: PathBuf,

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
    let signing_key = get_signing_key(&options.signing_key)?;
    let meta_package =
        fuchsia_pkg::MetaPackage::deserialize(std::fs::File::open(options.meta_package)?)?;
    let output = std::fs::File::create(options.output)?;
    fuchsia_pkg::build(&creation_manifest, &signing_key, &meta_package, output)?;
    Ok(())
}

fn get_signing_key(path: &PathBuf) -> Result<Ed25519PrivKey, PmBuildError> {
    let mut file = std::fs::File::open(path)?;
    let file_size = file.metadata()?.len();
    if file_size != 64 {
        return Err(PmBuildError::WrongSizeSigningKey { actual_size: file_size });
    }

    let mut private_key_array = [0u8; 32];
    file.read_exact(&mut private_key_array)?;

    let mut public_key_array = [0u8; 32];
    file.read_exact(&mut public_key_array)?;
    let public_key = Ed25519PubKey::from_bytes(public_key_array);

    Ok(Ed25519PrivKey::from_key_pair_bytes(private_key_array, &public_key))
}
