// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_packaging_download_args::DownloadCommand;
use fuchsia_hyper::new_https_client;
use pkg::repository::http_repository::package_download;

#[ffx_plugin("ffx_package")]
pub async fn cmd_package(cmd: DownloadCommand) -> Result<()> {
    let client = new_https_client();
    package_download(client, cmd.tuf_url, cmd.blob_url, cmd.target_path, cmd.output_path).await?;
    Ok(())
}
