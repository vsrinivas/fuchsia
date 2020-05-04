// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_pkg::{PackageResolverMarker, UpdatePolicy},
    fuchsia_syslog::fx_log_info,
    update_package::{UpdateMode, UpdatePackage},
};

mod args;
mod images;
mod paver;

#[fuchsia_async::run_singlethreaded]
async fn main() {
    let args: crate::args::Args = argh::from_env();
    println!("All new projects need to start somewhere.");
    println!("This is not the system updater you are looking for...yet.");
    println!("Args: {:#?}", args);

    // TODO(49911) write a real main that doesn't panic on error, and, you know, updates the
    // system.

    let update_pkg = resolve_update_package(&args.update).await;

    let image_list = images::load_image_list().await.unwrap();
    let images = update_pkg.resolve_images(&image_list[..]).await.unwrap();
    let images = images.verify(UpdateMode::Normal).unwrap();
    println!("Images: {:#?}", images);

    std::process::exit(1);
}

// TODO(49911) rewrite this to return an error instead of panicing before using it for real.
async fn resolve_update_package(url: &str) -> UpdatePackage {
    let resolver =
        fuchsia_component::client::connect_to_service::<PackageResolverMarker>().unwrap();

    let (dir, dir_server_end) = fidl::endpoints::create_proxy().unwrap();

    let res = resolver
        .resolve(
            url,
            &mut std::iter::empty(),
            &mut UpdatePolicy { fetch_if_absent: true, allow_old_versions: true },
            dir_server_end,
        )
        .await
        .unwrap();
    fuchsia_zircon::Status::ok(res).unwrap();

    UpdatePackage::new(dir)
}

// TODO(49911) use this. Note: this behavior will be tested with the integration tests.
#[allow(dead_code)]
async fn verify_board(pkg: UpdatePackage) -> Result<(), Error> {
    let system_board_file = io_util::file::open_in_namespace(
        "/config/build-info/board",
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
    )
    .context("open /config/build-info/board")?;

    let system_board_name = io_util::file::read_to_string(&system_board_file)
        .await
        .context("read /config/build-info/board")?;

    pkg.verify_board(&system_board_name).await.context("verify system board")
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
