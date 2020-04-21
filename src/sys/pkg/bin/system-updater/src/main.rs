// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    fuchsia_syslog::fx_log_info,
    update_package::{UpdateMode, UpdatePackage},
};

mod args;

fn main() {
    let _: crate::args::Args = argh::from_env();
    println!("All new projects need to start somewhere.");
}

// TODO(49911) use this. Note: this behavior will be tested with the integration tests.
#[allow(dead_code)]
async fn verify_board(pkg: UpdatePackage) -> Result<(), anyhow::Error> {
    let system_board_file = io_util::file::open_in_namespace(
        "/config/build-info/board",
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
    )
    .map_err(|e| anyhow!(e).context("open /config/build-info/board"))?;

    let system_board_name = io_util::file::read_to_string(&system_board_file)
        .await
        .map_err(|e| anyhow!(e).context("read /config/build-info/board"))?;

    pkg.verify_board(&system_board_name)
        .await
        .map_err(|e| anyhow!(e).context("verify system board"))
}

// TODO(49911) use this. Note: this behavior will be tested with the integration tests.
#[allow(dead_code)]
async fn update_mode(
    pkg: UpdatePackage,
) -> Result<UpdateMode, update_package::ParseUpdateModeError> {
    pkg.update_mode().await.map(|opt| {
        opt.unwrap_or_else(|| {
            let mode = UpdateMode::default();
            fx_log_info!("update-mode file not found, using default mode: {:?}", mode);
            mode
        })
    })
}
