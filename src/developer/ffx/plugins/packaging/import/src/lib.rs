// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_packaging_import_args::ImportCommand;
use ffx_packaging_repository::Repository;
use std::fs::File;
use std::io::{Cursor, Write};

#[ffx_plugin("ffx_package")]
pub async fn cmd_package(cmd: ImportCommand) -> Result<()> {
    let repo = &Repository::default_repo().await?;
    cmd_package_import(cmd, std::io::stdout(), repo)?;
    Ok(())
}

pub fn cmd_package_import(cmd: ImportCommand, mut w: impl Write, repo: &Repository) -> Result<()> {
    let mut archive = File::open(cmd.archive)?;
    let mut archive = fuchsia_archive::Reader::new(&mut archive)?;
    let mut meta_far = Cursor::new(archive.read_file("meta.far")?);
    let meta_hash = repo.blobs().add_blob(&mut meta_far)?;
    meta_far.set_position(0);
    let mut meta_far = fuchsia_archive::Reader::new(&mut meta_far)?;
    let meta_contents = meta_far.read_file("meta/contents")?;
    let meta_contents =
        fuchsia_pkg::MetaContents::deserialize(meta_contents.as_slice())?.into_contents();
    for hash in meta_contents.values() {
        repo.blobs().add_blob(Cursor::new(archive.read_file(&hash.to_string())?))?;
    }
    writeln!(w, "{}", meta_hash)?;
    Ok(())
}
