// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
pub use ffx_packaging_export_args::ExportCommand;
use ffx_packaging_repository::Repository;
use std::collections::BTreeMap;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};

#[ffx_plugin("ffx_package")]
pub async fn cmd_package(cmd: ExportCommand) -> Result<()> {
    let repo = &Repository::default_repo().await?;
    cmd_package_export(cmd, repo)?;
    Ok(())
}

pub fn cmd_package_export(cmd: ExportCommand, repo: &Repository) -> Result<()> {
    let mut meta_far_blob = repo.blobs().open_blob(&cmd.package.parse()?)?;
    let mut meta_far = fuchsia_archive::Reader::new(&mut meta_far_blob)?;
    let meta_contents = meta_far.read_file("meta/contents")?;
    let meta_contents =
        fuchsia_pkg::MetaContents::deserialize(meta_contents.as_slice())?.into_contents();
    let mut contents: BTreeMap<_, (_, Box<dyn Read>)> = BTreeMap::new();
    for hash in meta_contents.values() {
        let blob = repo.blobs().open_blob(&hash)?;
        contents.insert(hash.to_string(), (blob.metadata()?.len(), Box::new(blob)));
    }
    meta_far_blob.seek(SeekFrom::Start(0))?;
    contents
        .insert("meta.far".to_string(), (meta_far_blob.metadata()?.len(), Box::new(meta_far_blob)));
    let output = File::create(cmd.output)?;
    fuchsia_archive::write(output, contents)?;
    Ok(())
}
