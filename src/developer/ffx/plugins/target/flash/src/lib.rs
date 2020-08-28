// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::manifest::FlashManifest,
    anyhow::{bail, Context, Result},
    ffx_core::ffx_plugin,
    ffx_flash_args::FlashCommand,
    std::fs::File,
    std::io::{stdout, BufReader, Read, Write},
    std::path::Path,
};

mod manifest;

#[ffx_plugin()]
pub fn flash(cmd: FlashCommand) -> Result<()> {
    let path = Path::new(&cmd.manifest);
    if !path.is_file() {
        bail!("File does not exist: {}", cmd.manifest);
    }
    let mut writer = Box::new(stdout());
    let reader = File::open(path).context("opening file for read").map(BufReader::new)?;
    flash_impl(&mut writer, reader)
}

fn flash_impl<W: Write, R: Read>(writer: &mut W, reader: R) -> Result<()> {
    match FlashManifest::load(reader)? {
        FlashManifest::V1(v) => serde_json::to_writer_pretty(writer, &v)?,
    }
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_nonexistent_file_throws_err() {
        assert!(flash(FlashCommand { manifest: "ffx_test_does_not_exist".to_string() }).is_err())
    }
}
