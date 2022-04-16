// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use ffx_snapshot::snapshot_impl;
use ffx_snapshot_args::SnapshotCommand;
use fidl_fuchsia_feedback::DataProviderProxy;
use std::{
    fs::File,
    io::{copy, Write},
    path::{Path, PathBuf},
};
use zip::ZipArchive;

/// Creates new snapshot at given directory path and unzips it.
pub async fn create_snapshot<W: Write>(
    data_provider_proxy: DataProviderProxy,
    directory: &Path,
    writer: &mut W,
) -> Result<()> {
    let cmd = SnapshotCommand {
        dump_annotations: false,
        output_file: Some(directory.to_string_lossy().into()),
    };
    snapshot_impl(data_provider_proxy, cmd, writer)
        .await
        .context("Unable to take snapshot of target.")?;

    let snapshot_zip_path = directory.join("snapshot.zip");

    unzip_file(&snapshot_zip_path, directory)
}

/// Extracts a snapshot zip file to a directory.
fn unzip_file(zip_file: &Path, extract_dir: &Path) -> Result<()> {
    let file = File::open(&zip_file).context("Unable to open archive file.")?;
    let mut zip = ZipArchive::new(file).context("Unable to read archive.")?;

    for i in 0..zip.len() {
        let mut file = zip.by_index(i).context("Unable to get file by index from archive.")?;
        let outpath = file.sanitized_name();
        let mut dest = PathBuf::from(extract_dir);
        dest.push(outpath);
        let mut outfile = File::create(&dest).context("Unable to create output file.")?;
        copy(&mut file, &mut outfile)?;
    }

    Ok(())
}
