// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{fidl::FidlServer, local_mirror_manager::LocalMirrorManager},
    anyhow::Context,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::fx_log_info,
};

mod fidl;
mod local_mirror_manager;

const METADATA_DIR_PATH: &str = "/usb/0/fuchsia_pkg/repository_metadata";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    fuchsia_syslog::init_with_tags(&["pkg-local-mirror"]).expect("can't init logger");
    fx_log_info!("starting pkg-local-mirror");

    let metadata_dir = io_util::directory::open_in_namespace(
        METADATA_DIR_PATH,
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
    )
    .with_context(|| format!("while opening {}", METADATA_DIR_PATH))?;
    let local_mirror_manager = LocalMirrorManager::new(metadata_dir);

    let mut fs = ServiceFs::new_local();
    let _ = fs.take_and_serve_directory_handle().context("while serving directory handle")?;

    let () = FidlServer::new(local_mirror_manager).run(fs).await;

    Ok(())
}
