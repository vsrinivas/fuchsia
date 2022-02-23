// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    std::path::PathBuf,
};

/// Command-line arguments for the vbmeta tool.
#[derive(argh::FromArgs)]
struct Args {
    /// path to a ZBI.
    #[argh(option)]
    zbi: PathBuf,

    /// path to a private key PEM.
    #[argh(option)]
    private_key_pem: PathBuf,

    /// path to a public key metadata file.
    #[argh(option)]
    public_key_metadata: PathBuf,

    /// path at which to output the corresponding VBMeta.
    #[argh(option)]
    output: PathBuf,
}

fn main() -> Result<()> {
    let args: Args = argh::from_env();

    let zbi = std::fs::read(args.zbi).context("failed to read ZBI")?;
    let pem = std::fs::read(args.private_key_pem).context("failed to read private key PEM")?;
    let pem_str = std::str::from_utf8(&pem).context("failed to convert PEM to string")?;
    let metadata =
        std::fs::read(args.public_key_metadata).context("failed to read public key metadata")?;

    let key = vbmeta::Key::try_new(pem_str, metadata).context("failed to create AVB key")?;
    let salt = vbmeta::Salt::random().context("failed to season")?;
    let descriptor = vbmeta::HashDescriptor::new("zircon", &zbi, salt);
    let vbmeta = vbmeta::VBMeta::sign(vec![descriptor], key).unwrap();

    std::fs::write(args.output, vbmeta.as_bytes()).context("failed to write VBMeta to file")?;

    Ok(())
}
