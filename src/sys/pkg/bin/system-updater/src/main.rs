// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::history::UpdateHistory,
    anyhow::{Context, Error},
    fidl_fuchsia_pkg::{PackageResolverMarker, UpdatePolicy},
    fuchsia_syslog::fx_log_info,
    std::time::{Instant, SystemTime},
    update_package::{UpdateMode, UpdatePackage},
};

mod args;
mod channel;
mod history;
mod images;
mod metrics;
mod paver;

#[fuchsia_async::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init_with_tags(&["system-updater"]).expect("can't init logger");
    fx_log_info!("starting system updater");

    let (cobalt, cobalt_fut) = metrics::connect_to_cobalt();

    let args: crate::args::Args = argh::from_env();

    // wait for both the update attempt to finish and for all cobalt events to be flushed to the
    // service.
    let ((), ()) = futures::join!(update(args, cobalt), cobalt_fut);

    std::process::exit(1);
}

async fn update(args: crate::args::Args, mut cobalt: metrics::Client) {
    // The OTA attempt started during the update check, so use that time if possible.  Fallback to
    // now if that data wasn't provided.
    let start_time = args.start.unwrap_or_else(SystemTime::now);
    let start_time_mono =
        metrics::system_time_to_monotonic_time(start_time).unwrap_or_else(Instant::now);

    cobalt.log_ota_start(&args.target, args.initiator, start_time);
    println!("All new projects need to start somewhere.");
    println!("This is not the system updater you are looking for...yet.");
    println!("Args: {:#?}", args);

    let history = UpdateHistory::increment_or_create(&args.source, &args.target, start_time).await;
    println!("History: {:#?}", history);

    // TODO(49911) write a real main that doesn't panic on error, and, you know, updates the
    // system. Also, ensure metrics are emitted on both success and failure.

    let update_pkg = resolve_update_package(&args.update).await;

    let image_list = images::load_image_list().await.unwrap();
    let images = update_pkg.resolve_images(&image_list[..]).await.unwrap();
    let images = images.verify(UpdateMode::Normal).unwrap();
    println!("Images: {:#?}", images);

    let (_data_sink, boot_manager) = paver::connect_in_namespace().unwrap();
    let inactive_config = paver::query_inactive_configuration(&boot_manager).await.unwrap();
    println!("Inactive configuration: {:#?}", inactive_config);

    cobalt.log_ota_result_attempt(
        &args.target,
        args.initiator,
        history.attempts(),
        metrics::Phase::Success,
        metrics::StatusCode::Success,
    );

    cobalt.log_ota_result_duration(
        &args.target,
        args.initiator,
        metrics::Phase::Success,
        metrics::StatusCode::Success,
        start_time_mono.elapsed(),
    );

    cobalt.log_ota_result_free_space_delta(
        &args.target,
        args.initiator,
        metrics::Phase::Success,
        metrics::StatusCode::Success,
    );
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
