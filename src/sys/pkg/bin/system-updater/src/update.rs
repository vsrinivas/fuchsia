// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Error},
    fidl_fuchsia_hardware_power_statecontrol::AdminProxy as PowerStateControlProxy,
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_paver::DataSinkProxy,
    fidl_fuchsia_pkg::PackageCacheProxy,
    fidl_fuchsia_space::ManagerProxy as SpaceManagerProxy,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    futures::prelude::*,
    update_package::{Image, UpdateMode, UpdatePackage},
};

mod channel;
mod config;
mod environment;
mod history;
mod images;
mod metrics;
mod paver;
mod resolver;

pub(super) use {
    config::Config,
    environment::{BuildInfo, CobaltConnector, Environment},
    resolver::ResolveError,
};

#[derive(Debug)]
enum CommitAction {
    /// A reboot is required to apply the update, which should be performed by the system updater.
    Reboot,

    /// A reboot is required to apply the update, but the initiator of the update requested to
    /// perform the reboot themselves.
    RebootDeferred,
}

/// Updates the system in the given `Environment` using the provided config options.
pub async fn update(config: Config, env: Environment) -> Result<(), ()> {
    // The only operation allowed to fail in this function is update_attempt. The rest of the
    // functionality here sets up the update attempt and takes the appropriate actions based on
    // whether the update attempt succeeds or fails.

    let mut phase = metrics::Phase::Tufupdate;

    // wait for both the update attempt to finish and for all cobalt events to be flushed to the
    // service.
    let (mut cobalt, cobalt_forwarder_task) = env.cobalt_connector.connect();
    let (res, ()) = future::join(
        async {
            fx_log_info!("starting system update with config: {:?}", config);
            cobalt.log_ota_start(&config.target_version, config.initiator, config.start_time);

            let history = history::UpdateHistory::increment_or_create(
                &config.source_version,
                &config.target_version,
                config.start_time,
            )
            .await;

            let res = update_attempt(&config, &env, &mut phase).await;
            let status_code = metrics::result_to_status_code(res.as_ref().map(|_| ()));

            cobalt.log_ota_result_attempt(
                &config.target_version,
                config.initiator,
                history.attempts(),
                phase,
                status_code,
            );
            cobalt.log_ota_result_duration(
                &config.target_version,
                config.initiator,
                phase,
                status_code,
                config.start_time_mono.elapsed(),
            );
            cobalt.log_ota_result_free_space_delta(
                &config.target_version,
                config.initiator,
                phase,
                status_code,
            );
            drop(cobalt);

            if res.is_ok() {
                channel::update_current_channel().await;
            }

            history.save().await;

            res
        },
        cobalt_forwarder_task,
    )
    .await;

    match res {
        Ok((CommitAction::Reboot, _packages)) => {
            fx_log_info!("system update complete, rebooting...");
            reboot(&env.power_state_control).await;
            Ok(())
        }
        Ok((CommitAction::RebootDeferred, _packages)) => {
            fx_log_info!("system update complete, reboot to new version deferred to caller.");
            Ok(())
        }
        Err(e) => {
            fx_log_err!("system update failed: {:#}", anyhow!(e));
            Err(())
        }
    }
}

async fn update_attempt(
    config: &Config,
    env: &Environment,
    phase: &mut metrics::Phase,
) -> Result<(CommitAction, Vec<DirectoryProxy>), Error> {
    if let Err(e) = gc(&env.space_manager).await {
        fx_log_err!("unable to gc packages (1/2): {:#}", anyhow!(e));
    }

    let update_pkg =
        resolver::resolve_update_package(&env.pkg_resolver, &config.update_url).await?;
    let () = update_pkg.verify_name().await?;

    if let Err(e) = gc(&env.space_manager).await {
        fx_log_err!("unable to gc packages (2/2): {:#}", anyhow!(e));
    }

    let mode = update_mode(&update_pkg).await.context("while determining update mode")?;
    match mode {
        UpdateMode::Normal => {}
        UpdateMode::ForceRecovery => {
            if !config.should_write_recovery {
                bail!("force-recovery mode is incompatible with skip-recovery option");
            }
        }
    }

    verify_board(&env.build_info, &update_pkg).await?;

    // Fetch packages

    *phase = metrics::Phase::PackageDownload;
    let packages = match mode {
        UpdateMode::Normal => {
            let packages =
                update_pkg.packages().await.context("while determining packages to fetch")?;
            let packages = resolver::resolve_packages(&env.pkg_resolver, packages.iter())
                .await
                .context("while fetching packages")?;
            let () = sync_package_cache(&env.pkg_cache).await?;
            packages
        }
        UpdateMode::ForceRecovery => {
            // FIXME(53133): this sync isn't necessary.
            let () = sync_package_cache(&env.pkg_cache).await?;
            vec![]
        }
    };

    // Write images

    *phase = metrics::Phase::ImageWrite;
    let image_list = images::load_image_list().await?;

    let images = update_pkg
        .resolve_images(&image_list[..])
        .await
        .context("while determining which images to write")?;
    let images = images
        .verify(mode)
        .context("while ensuring the target images are compatible with this update mode")?
        .filter(|image| {
            if config.should_write_recovery {
                true
            } else {
                if image.classify().map(|class| class.targets_recovery()).unwrap_or(false) {
                    fx_log_info!("Skipping recovery image: {}", image.filename());
                    false
                } else {
                    true
                }
            }
        });
    fx_log_info!("Images to write: {:?}", images);

    let inactive_config = paver::query_inactive_configuration(&env.boot_manager)
        .await
        .context("while determining inactive configuration")?;
    fx_log_info!("Targeting inactive configuration: {:?}", inactive_config);

    write_images(&env.data_sink, &update_pkg, inactive_config, images.iter()).await?;
    match mode {
        UpdateMode::Normal => {
            let () = paver::set_configuration_active(&env.boot_manager, inactive_config).await?;
        }
        UpdateMode::ForceRecovery => {
            let () = paver::set_recovery_configuration_active(&env.boot_manager).await?;
        }
    }
    let () = paver::flush(&env.data_sink, &env.boot_manager, inactive_config).await?;

    // Success!

    *phase = metrics::Phase::SuccessPendingReboot;
    let commit_action = match mode {
        UpdateMode::Normal => {
            if config.should_reboot {
                CommitAction::Reboot
            } else {
                CommitAction::RebootDeferred
            }
        }
        UpdateMode::ForceRecovery => {
            // Always reboot on success, even if the caller asked to defer the reboot.
            CommitAction::Reboot
        }
    };
    Ok((commit_action, packages))
}

async fn write_images<'a, I>(
    data_sink: &DataSinkProxy,
    update_pkg: &UpdatePackage,
    inactive_config: paver::InactiveConfiguration,
    images: I,
) -> Result<(), Error>
where
    I: Iterator<Item = &'a Image>,
{
    for image in images {
        paver::write_image(data_sink, update_pkg, image, inactive_config)
            .await
            .context("while writing images")?;
    }
    Ok(())
}

async fn sync_package_cache(pkg_cache: &PackageCacheProxy) -> Result<(), Error> {
    async move {
        pkg_cache
            .sync()
            .await
            .context("while performing sync call")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("sync responded with")
    }
    .await
    .context("while flushing packages to persistent storage")
}

async fn gc(space_manager: &SpaceManagerProxy) -> Result<(), Error> {
    let () = space_manager
        .gc()
        .await
        .context("while performing gc call")?
        .map_err(|e| anyhow!("garbage collection responded with {:?}", e))?;
    Ok(())
}

async fn reboot(proxy: &PowerStateControlProxy) {
    if let Err(e) = async move {
        use fidl_fuchsia_hardware_power_statecontrol::RebootReason;
        proxy
            .reboot(RebootReason::SystemUpdate)
            .await
            .context("while performing reboot call")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("reboot responded with")
    }
    .await
    {
        fx_log_err!("error initiating reboot: {:#}", anyhow!(e));
    }
}

async fn verify_board<B>(build_info: &B, pkg: &UpdatePackage) -> Result<(), Error>
where
    B: BuildInfo,
{
    let current_board = build_info.board().await.context("while determining current board")?;
    if let Some(current_board) = current_board {
        let () = pkg.verify_board(&current_board).await.context("while verifying target board")?;
    }
    Ok(())
}

async fn update_mode(
    pkg: &UpdatePackage,
) -> Result<UpdateMode, update_package::ParseUpdateModeError> {
    pkg.update_mode().await.map(|opt| {
        opt.unwrap_or_else(|| {
            let mode = UpdateMode::default();
            fx_log_info!("update-mode file not found, using default mode: {:?}", mode);
            mode
        })
    })
}
