// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Error},
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_paver::DataSinkProxy,
    fidl_fuchsia_pkg::PackageCacheProxy,
    fidl_fuchsia_space::ManagerProxy as SpaceManagerProxy,
    fidl_fuchsia_update_installer_ext::{Options, State, UpdateInfo},
    fuchsia_async::Task,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_url::pkg_url::PkgUrl,
    futures::prelude::*,
    parking_lot::Mutex,
    std::sync::Arc,
    update_package::{Image, UpdateMode, UpdatePackage},
};

mod channel;
mod config;
mod environment;
mod genutil;
mod history;
mod images;
mod metrics;
mod paver;
mod reboot;
mod resolver;
mod state;

use genutil::GeneratorExt;
pub(super) use {
    config::Config,
    environment::{BuildInfo, CobaltConnector, Environment},
    history::{UpdateAttempt, UpdateHistory},
    reboot::RebootController,
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
///
/// If a reboot is required to complete the update and `Config::should_reboot` is true, this task
/// will initiate the reboot without any delay.  However, if `Config::should_reboot` is false and a
/// `reboot_controller` is provided, this task will wait in the WaitToReboot state until the
/// controller unblocks the reboot.  If `Config::should_reboot` is false and no controller is
/// provided, this task will skip the reboot step, requiring the initiator to perform the reboot.
pub async fn update(
    config: Config,
    env: Environment,
    history: Arc<Mutex<UpdateHistory>>,
    reboot_controller: Option<RebootController>,
) -> impl Stream<Item = State> {
    let attempt_fut = history.lock().start_update_attempt(
        // TODO(fxb/55408): replace with the real options
        Options {
            initiator: config.initiator.into(),
            allow_attach_to_existing_attempt: false,
            should_write_recovery: config.should_write_recovery,
        },
        config.update_url.clone(),
        config.start_time,
        &env.data_sink,
        &env.boot_manager,
        &env.build_info,
        &env.pkgfs_system,
    );
    let attempt = attempt_fut.await;
    let source_version = attempt.source_version().clone();
    let power_state_control = env.power_state_control.clone();

    let history_clone = Arc::clone(&history);
    async_generator::generate(move |mut co| async move {
        let history = history_clone;
        // The only operation allowed to fail in this function is update_attempt. The rest of the
        // functionality here sets up the update attempt and takes the appropriate actions based on
        // whether the update attempt succeeds or fails.

        let mut phase = metrics::Phase::Tufupdate;
        let (mut cobalt, cobalt_forwarder_task) = env.cobalt_connector.connect();
        let cobalt_forwarder_task = Task::spawn(cobalt_forwarder_task);

        fx_log_info!("starting system update with config: {:?}", config);
        cobalt.log_ota_start(&config.target_version, config.initiator, config.start_time);

        let mut target_version = history::Version::default();

        let attempt_res = Attempt { config: &config, env: &env }
            .run(&mut co, &mut phase, &mut target_version)
            .await;

        let status_code = metrics::result_to_status_code(attempt_res.as_ref().map(|_| ()));
        cobalt.log_ota_result_attempt(
            &config.target_version,
            config.initiator,
            history.lock().attempts_for(&source_version, &target_version) + 1,
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
        drop(cobalt);

        if attempt_res.is_ok() {
            channel::update_current_channel().await;
        }

        // wait for all cobalt events to be flushed to the service.
        let () = cobalt_forwarder_task.await;

        let (state, commit_action, _packages) = match attempt_res {
            Ok((state, commit_action, packages)) => (state, commit_action, packages),
            Err(e) => {
                fx_log_err!("system update failed: {:#}", anyhow!(e));
                return target_version;
            }
        };

        match (commit_action, reboot_controller) {
            (CommitAction::Reboot, _) => {
                fx_log_info!("system update complete, rebooting...");
            }
            (CommitAction::RebootDeferred, Some(mut reboot_controller)) => {
                fx_log_info!(
                    "system update complete, waiting for initiator to signal ready for reboot."
                );
                reboot_controller.wait_to_reboot().await;
                fx_log_info!("initiator ready to reboot, rebooting...");
            }
            (CommitAction::RebootDeferred, None) => {
                fx_log_info!("system update complete, reboot to new version deferred to caller.");
                state.enter_defer_reboot(&mut co).await;
                return target_version;
            }
        }

        state.enter_reboot(&mut co).await;
        target_version
    })
    .when_done(move |last_state: Option<State>, target_version| async move {
        let last_state = last_state.unwrap_or(State::Prepare);

        let should_reboot = matches!(last_state, State::Reboot{ .. });

        let attempt = attempt.finish(target_version, last_state);
        history.lock().record_update_attempt(attempt);
        let save_fut = history.lock().save();
        save_fut.await;

        if should_reboot {
            reboot::reboot(&power_state_control).await;
        }
    })
}

struct Attempt<'a> {
    config: &'a Config,
    env: &'a Environment,
}

impl<'a> Attempt<'a> {
    async fn run(
        mut self,
        co: &mut async_generator::Yield<State>,
        phase: &mut metrics::Phase,
        target_version: &mut history::Version,
    ) -> Result<(state::WaitToReboot, CommitAction, Vec<DirectoryProxy>), Error> {
        // Prepare
        let state = state::Prepare::enter(co).await;

        let (update_pkg, mode, packages_to_fetch) = match self.prepare(target_version).await {
            Ok((update_pkg, mode, packages_to_fetch)) => (update_pkg, mode, packages_to_fetch),
            Err(e) => {
                state.fail(co).await;
                return Err(e);
            }
        };

        // Fetch packages
        let mut state = state
            .enter_fetch(
                co,
                UpdateInfo::builder().download_size(0).build(),
                packages_to_fetch.len() as u64 + 1,
            )
            .await;
        *phase = metrics::Phase::PackageDownload;

        let packages = match self.fetch_packages(co, &mut state, packages_to_fetch, mode).await {
            Ok(packages) => packages,
            Err(e) => {
                state.fail(co).await;
                return Err(e);
            }
        };

        // Write images
        let mut state = state.enter_stage(co).await;
        *phase = metrics::Phase::ImageWrite;

        let () = match self.stage_images(co, &mut state, &update_pkg, mode).await {
            Ok(()) => (),
            Err(e) => {
                state.fail(co).await;
                return Err(e);
            }
        };

        // Success!
        let state = state.enter_wait_to_reboot(co).await;
        *phase = metrics::Phase::SuccessPendingReboot;

        let commit_action = match mode {
            UpdateMode::Normal => {
                if self.config.should_reboot {
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
        Ok((state, commit_action, packages))
    }

    /// Acquire the necessary data to perform the update.
    ///
    /// This includes fetching the update package, which contains the list of packages in the
    /// target OS and kernel images that need written.
    async fn prepare(
        &mut self,
        target_version: &mut history::Version,
    ) -> Result<(UpdatePackage, UpdateMode, Vec<PkgUrl>), Error> {
        if let Err(e) = gc(&self.env.space_manager).await {
            fx_log_err!("unable to gc packages (1/2): {:#}", anyhow!(e));
        }

        let update_pkg =
            resolver::resolve_update_package(&self.env.pkg_resolver, &self.config.update_url)
                .await?;
        *target_version = history::Version::for_update_package(&update_pkg).await;
        let () = update_pkg.verify_name().await?;

        if let Err(e) = gc(&self.env.space_manager).await {
            fx_log_err!("unable to gc packages (2/2): {:#}", anyhow!(e));
        }

        let mode = update_mode(&update_pkg).await.context("while determining update mode")?;
        match mode {
            UpdateMode::Normal => {}
            UpdateMode::ForceRecovery => {
                if !self.config.should_write_recovery {
                    bail!("force-recovery mode is incompatible with skip-recovery option");
                }
            }
        }

        verify_board(&self.env.build_info, &update_pkg).await?;

        let packages_to_fetch = match mode {
            UpdateMode::Normal => {
                update_pkg.packages().await.context("while determining packages to fetch")?
            }
            UpdateMode::ForceRecovery => vec![],
        };

        Ok((update_pkg, mode, packages_to_fetch))
    }

    /// Fetch all base packages needed by the target OS.
    async fn fetch_packages(
        &mut self,
        co: &mut async_generator::Yield<State>,
        state: &mut state::Fetch,
        packages_to_fetch: Vec<PkgUrl>,
        mode: UpdateMode,
    ) -> Result<Vec<DirectoryProxy>, Error> {
        let mut packages = Vec::with_capacity(packages_to_fetch.len());

        let package_dir_futs =
            resolver::resolve_packages(&self.env.pkg_resolver, packages_to_fetch.iter());
        futures::pin_mut!(package_dir_futs);

        while let Some(package_dir) =
            package_dir_futs.try_next().await.context("while fetching packages")?
        {
            packages.push(package_dir);

            state.add_progress(co, 1).await;
        }

        match mode {
            UpdateMode::Normal => sync_package_cache(&self.env.pkg_cache).await?,
            UpdateMode::ForceRecovery => {}
        }

        Ok(packages)
    }

    /// Pave the various raw images (zbi, bootloaders, vbmeta), and configure the inactive
    /// configuration as active for the next boot.
    async fn stage_images(
        &mut self,
        co: &mut async_generator::Yield<State>,
        state: &mut state::Stage,
        update_pkg: &UpdatePackage,
        mode: UpdateMode,
    ) -> Result<(), Error> {
        let image_list = images::load_image_list().await?;

        let images = update_pkg
            .resolve_images(&image_list[..])
            .await
            .context("while determining which images to write")?;
        let images = images
            .verify(mode)
            .context("while ensuring the target images are compatible with this update mode")?
            .filter(|image| {
                if self.config.should_write_recovery {
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

        let inactive_config = paver::query_inactive_configuration(&self.env.boot_manager)
            .await
            .context("while determining inactive configuration")?;
        fx_log_info!("Targeting inactive configuration: {:?}", inactive_config);

        write_images(&self.env.data_sink, &update_pkg, inactive_config, images.iter()).await?;
        match mode {
            UpdateMode::Normal => {
                let () = paver::set_configuration_active(&self.env.boot_manager, inactive_config)
                    .await?;
            }
            UpdateMode::ForceRecovery => {
                let () = paver::set_recovery_configuration_active(&self.env.boot_manager).await?;
            }
        }
        let () = paver::flush(&self.env.data_sink, &self.env.boot_manager, inactive_config).await?;

        state.add_progress(co, 1).await;

        Ok(())
    }
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
