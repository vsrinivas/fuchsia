// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Error},
    async_trait::async_trait,
    epoch::EpochFile,
    fidl::endpoints::ProtocolMarker as _,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_mem::Buffer,
    fidl_fuchsia_paver::{Asset, DataSinkProxy},
    fidl_fuchsia_pkg::{
        PackageCacheProxy, PackageResolverProxy, RetainedPackagesMarker, RetainedPackagesProxy,
    },
    fidl_fuchsia_space::ManagerProxy as SpaceManagerProxy,
    fidl_fuchsia_update_installer_ext::{
        FetchFailureReason, Options, PrepareFailureReason, StageFailureReason, State, UpdateInfo,
    },
    fuchsia_async::{Task, TimeoutExt as _},
    fuchsia_hash::Hash,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_url::{AbsoluteComponentUrl, AbsolutePackageUrl},
    futures::{prelude::*, stream::FusedStream},
    parking_lot::Mutex,
    sha2::{Digest, Sha256},
    std::{collections::HashSet, pin::Pin, sync::Arc, time::Duration},
    thiserror::Error,
    update_package::{
        Image, ImagePackagesSlots, ImageType, ResolveImagesError, UpdateImagePackage, UpdateMode,
        UpdatePackage, VerifyError,
    },
};

mod config;
mod environment;
mod genutil;
mod history;
mod metrics;
mod paver;
mod reboot;
mod resolver;
mod state;

pub(super) use {
    config::Config,
    environment::{
        BuildInfo, CobaltConnector, Environment, EnvironmentConnector,
        NamespaceEnvironmentConnector, SystemInfo,
    },
    genutil::GeneratorExt,
    history::{UpdateAttempt, UpdateHistory},
    reboot::{ControlRequest, RebootController},
    resolver::ResolveError,
};

#[cfg(test)]
pub(super) use {
    config::ConfigBuilder,
    environment::{NamespaceBuildInfo, NamespaceCobaltConnector, NamespaceSystemInfo},
};

const COBALT_FLUSH_TIMEOUT: Duration = Duration::from_secs(30);
const SOURCE_EPOCH_RAW: &str = include_str!(env!("EPOCH_PATH"));

/// Error encountered in the Prepare state.
#[derive(Debug, Error)]
enum PrepareError {
    #[error("while determining source epoch: '{0:?}'")]
    ParseSourceEpochError(String, #[source] serde_json::Error),

    #[error("while determining target epoch")]
    ParseTargetEpochError(#[source] update_package::ParseEpochError),

    #[error("while determining packages to fetch")]
    ParsePackages(#[source] update_package::ParsePackageError),

    #[error("while determining update mode")]
    ParseUpdateMode(#[source] update_package::ParseUpdateModeError),

    #[error("while reading from paver")]
    PaverRead(#[source] anyhow::Error),

    #[error("while writing asset to paver")]
    PaverWriteAsset(#[source] anyhow::Error),

    #[error("while writing firmware to paver")]
    PaverWriteFirmware(#[source] anyhow::Error),

    #[error("while preparing partitions for update")]
    PreparePartitionMetdata(#[source] paver::PreparePartitionMetadataError),

    #[error("while resolving the update package")]
    ResolveUpdate(#[source] ResolveError),

    #[error(
        "downgrades from epoch {src} to {target} are not allowed. For more context, see RFC-0071: https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0071_ota_backstop."
    )]
    UnsupportedDowngrade { src: u64, target: u64 },

    #[error("while verifying board name")]
    VerifyBoard(#[source] anyhow::Error),

    #[error("while verifying images to write")]
    VerifyImages(#[source] update_package::VerifyError),

    #[error("while verifying update package name")]
    VerifyName(#[source] update_package::VerifyNameError),

    #[error("force-recovery mode is incompatible with skip-recovery option")]
    VerifyUpdateMode,

    #[error("while reading a buffer")]
    VmoRead(#[source] fuchsia_zircon::Status),
}

impl PrepareError {
    fn reason(&self) -> PrepareFailureReason {
        match self {
            Self::ResolveUpdate(ResolveError::Error(
                fidl_fuchsia_pkg_ext::ResolveError::NoSpace,
                _,
            )) => PrepareFailureReason::OutOfSpace,
            Self::UnsupportedDowngrade { .. } => PrepareFailureReason::UnsupportedDowngrade,
            _ => PrepareFailureReason::Internal,
        }
    }
}

/// Error encountered in the Stage state.
#[derive(Debug, Error)]
enum StageError {
    #[error("while attempting to open the image")]
    OpenImageError(#[source] update_package::OpenImageError),

    #[error("while persisting target boot slot")]
    PaverFlush(#[source] anyhow::Error),

    #[error("while resolving an image package")]
    Resolve(#[source] ResolveError),

    #[error("while determining which images to write")]
    ResolveImages(#[source] ResolveImagesError),

    #[error("while ensuring the target images are compatible with this update mode")]
    Verify(#[source] VerifyError),

    #[error("while writing images")]
    Write(#[source] anyhow::Error),
}

impl StageError {
    fn reason(&self) -> StageFailureReason {
        match self {
            Self::Resolve(ResolveError::Error(fidl_fuchsia_pkg_ext::ResolveError::NoSpace, _)) => {
                StageFailureReason::OutOfSpace
            }
            _ => StageFailureReason::Internal,
        }
    }
}

/// Error encountered in the Fetch state.
#[derive(Debug, Error)]
enum FetchError {
    #[error("while resolving a package")]
    Resolve(#[source] ResolveError),

    #[error("while syncing pkg-cache")]
    Sync(#[source] anyhow::Error),
}

impl FetchError {
    fn reason(&self) -> FetchFailureReason {
        match self {
            Self::Resolve(ResolveError::Error(fidl_fuchsia_pkg_ext::ResolveError::NoSpace, _)) => {
                FetchFailureReason::OutOfSpace
            }
            _ => FetchFailureReason::Internal,
        }
    }
}

#[derive(Debug, PartialEq, Eq)]
pub enum CommitAction {
    /// A reboot is required to apply the update, which should be performed by the system updater.
    Reboot,

    /// A reboot is required to apply the update, but the initiator of the update requested to
    /// perform the reboot themselves.
    RebootDeferred,
}

/// A trait to update the system in the given `Environment` using the provided config options.
#[async_trait(?Send)]
pub trait Updater {
    type UpdateStream: FusedStream<Item = State>;

    async fn update(
        &mut self,
        config: Config,
        env: Environment,
        reboot_controller: RebootController,
    ) -> (String, Self::UpdateStream);
}

pub struct RealUpdater {
    history: Arc<Mutex<UpdateHistory>>,
}

impl RealUpdater {
    pub fn new(history: Arc<Mutex<UpdateHistory>>) -> Self {
        Self { history }
    }
}

#[async_trait(?Send)]
impl Updater for RealUpdater {
    type UpdateStream = Pin<Box<dyn FusedStream<Item = State>>>;

    async fn update(
        &mut self,
        config: Config,
        env: Environment,
        reboot_controller: RebootController,
    ) -> (String, Self::UpdateStream) {
        let (attempt_id, attempt) =
            update(config, env, Arc::clone(&self.history), reboot_controller).await;
        (attempt_id, Box::pin(attempt))
    }
}

/// Updates the system in the given `Environment` using the provided config options.
///
/// Reboot vs RebootDeferred behavior is determined in the following priority order:
/// * is mode ForceRecovery? If so, reboot.
/// * is there a reboot controller? If so, yield reboot to the controller.
/// * if none of the above are true, reboot depending on the value of `Config::should_reboot`.
///
/// If a reboot is deferred, the initiator of the update is responsible for triggering
/// the reboot.
async fn update(
    config: Config,
    env: Environment,
    history: Arc<Mutex<UpdateHistory>>,
    reboot_controller: RebootController,
) -> (String, impl FusedStream<Item = State>) {
    let attempt_fut = history.lock().start_update_attempt(
        Options {
            initiator: config.initiator.into(),
            allow_attach_to_existing_attempt: config.allow_attach_to_existing_attempt,
            should_write_recovery: config.should_write_recovery,
        },
        config.update_url.clone(),
        config.start_time,
        &env.data_sink,
        &env.boot_manager,
        &env.build_info,
        &env.system_info,
    );
    let attempt = attempt_fut.await;
    let source_version = attempt.source_version().clone();
    let power_state_control = env.power_state_control.clone();

    let history_clone = Arc::clone(&history);
    let attempt_id = attempt.attempt_id().to_string();
    let stream = async_generator::generate(move |mut co| async move {
        let history = history_clone;
        // The only operation allowed to fail in this function is update_attempt. The rest of the
        // functionality here sets up the update attempt and takes the appropriate actions based on
        // whether the update attempt succeeds or fails.

        let mut phase = metrics::Phase::Tufupdate;
        let (mut cobalt, cobalt_forwarder_task) = env.cobalt_connector.connect();
        let cobalt_forwarder_task = Task::spawn(cobalt_forwarder_task);

        fx_log_info!("starting system update with config: {:?}", config);
        cobalt.log_ota_start(config.initiator, config.start_time);

        let mut target_version = history::Version::default();

        let attempt_res = Attempt { config: &config, env: &env }
            .run(&mut co, &mut phase, &mut target_version)
            .await;

        fx_log_info!("system update attempt completed, logging metrics");
        let status_code = metrics::result_to_status_code(attempt_res.as_ref().map(|_| ()));
        cobalt.log_ota_result_attempt(
            config.initiator,
            history.lock().attempts_for(&source_version, &target_version) + 1,
            phase,
            status_code,
        );
        cobalt.log_ota_result_duration(
            config.initiator,
            phase,
            status_code,
            config.start_time_mono.elapsed(),
        );
        drop(cobalt);

        // wait for all cobalt events to be flushed to the service.
        fx_log_info!("flushing cobalt events");
        let () = flush_cobalt(cobalt_forwarder_task, COBALT_FLUSH_TIMEOUT).await;

        let (state, mode, _packages) = match attempt_res {
            Ok(ok) => ok,
            Err(e) => {
                fx_log_err!("system update failed: {:#}", anyhow!(e));
                return target_version;
            }
        };

        fx_log_info!("checking if reboot is required or should be deferred, mode: {:?}", mode);
        // Figure out if we should reboot.
        match mode {
            // First priority: Always reboot on ForceRecovery success, even if the caller
            // asked to defer the reboot.
            UpdateMode::ForceRecovery => {
                fx_log_info!("system update in ForceRecovery mode complete, rebooting...");
            }
            // Second priority: Use the attached reboot controller.
            UpdateMode::Normal => {
                fx_log_info!("system update complete, waiting for initiator to signal reboot.");
                match reboot_controller.wait_to_reboot().await {
                    CommitAction::Reboot => {
                        fx_log_info!("initiator ready to reboot, rebooting...");
                    }
                    CommitAction::RebootDeferred => {
                        fx_log_info!("initiator deferred reboot to caller.");
                        state.enter_defer_reboot(&mut co).await;
                        return target_version;
                    }
                }
            }
        }

        state.enter_reboot(&mut co).await;
        target_version
    })
    .when_done(move |last_state: Option<State>, target_version| async move {
        let last_state = last_state.unwrap_or(State::Prepare);

        let should_reboot = matches!(last_state, State::Reboot { .. });

        let attempt = attempt.finish(target_version, last_state);
        history.lock().record_update_attempt(attempt);
        let save_fut = history.lock().save();
        save_fut.await;

        if should_reboot {
            reboot::reboot(&power_state_control).await;
        }
    });
    (attempt_id, stream)
}

async fn flush_cobalt(cobalt_forwarder_task: impl Future<Output = ()>, flush_timeout: Duration) {
    cobalt_forwarder_task.on_timeout(flush_timeout, || {
        fx_log_err!(
            "Couldn't flush cobalt events within the timeout. Proceeding, but may have dropped metrics."
        );
    })
    .await;
}

/// Struct representing images that need to be written during the update.
/// Determined by parsing `images.json`.
struct ImagesToWrite {
    fuchsia: BootSlot,
    recovery: BootSlot,
    /// Unordered vector of (firmware_type, url).
    firmware: Vec<(String, AbsoluteComponentUrl)>,
}

impl ImagesToWrite {
    /// Default fields indicate that no images need to be written.
    fn new() -> Self {
        ImagesToWrite { fuchsia: BootSlot::new(), recovery: BootSlot::new(), firmware: vec![] }
    }

    fn is_empty(&self) -> bool {
        self.fuchsia.is_empty() && self.recovery.is_empty() && self.firmware.is_empty()
    }

    fn get_url_hashes(&self) -> HashSet<fuchsia_hash::Hash> {
        let mut hashes = HashSet::new();
        hashes.extend(&mut self.fuchsia.get_url_hashes().iter());

        hashes.extend(&mut self.recovery.get_url_hashes().iter());

        for firmware_hash in self.firmware.iter().filter_map(|(_, url)| url.package_url().hash()) {
            hashes.insert(firmware_hash);
        }

        hashes
    }

    fn get_urls(&self) -> HashSet<AbsolutePackageUrl> {
        let mut package_urls = HashSet::new();
        for (_, absolute_component_url) in &self.firmware {
            package_urls.insert(absolute_component_url.package_url().to_owned());
        }

        package_urls.extend(self.fuchsia.get_urls());
        package_urls.extend(self.recovery.get_urls());

        package_urls
    }

    fn print_list(&self) -> Vec<String> {
        if self.is_empty() {
            return vec!["ImagesToWrite is empty".to_string()];
        }

        let mut image_list = vec![];
        for (filename, _) in &self.firmware {
            image_list.push(filename.to_string());
        }

        for fuchsia_file in &self.fuchsia.print_names() {
            image_list.push(format!("Fuchsia{fuchsia_file}"));
        }
        for recovery_file in &self.recovery.print_names() {
            image_list.push(format!("Recovery{recovery_file}"));
        }

        image_list
    }

    async fn write(
        &self,
        pkg_resolver: &PackageResolverProxy,
        desired_config: paver::NonCurrentConfiguration,
        data_sink: &DataSinkProxy,
    ) -> Result<(), StageError> {
        let package_urls = self.get_urls();

        let url_directory_map = resolver::resolve_image_packages(pkg_resolver, package_urls.iter())
            .await
            .map_err(StageError::Resolve)?;

        for (filename, absolute_component_url) in &self.firmware {
            let package_url = absolute_component_url.package_url();
            let resource = absolute_component_url.resource();
            let proxy = &url_directory_map[package_url];
            let image = Image::new(ImageType::Firmware, Some(filename));
            write_image(proxy, resource, &image, data_sink, desired_config).await?;
        }

        if let Some(zbi) = &self.fuchsia.zbi {
            let package_url = zbi.package_url();
            let resource = zbi.resource();
            let proxy = &url_directory_map[package_url];
            let image = Image::new(ImageType::Zbi, None);
            write_image(proxy, resource, &image, data_sink, desired_config).await?;
        }

        if let Some(vbmeta) = &self.fuchsia.vbmeta {
            let package_url = vbmeta.package_url();
            let proxy = &url_directory_map[package_url];
            let image = Image::new(ImageType::FuchsiaVbmeta, None);
            let resource = vbmeta.resource();
            write_image(proxy, resource, &image, data_sink, desired_config).await?;
        }

        if let Some(zbi) = &self.recovery.zbi {
            let package_url = zbi.package_url();
            let proxy = &url_directory_map[package_url];
            let image = Image::new(ImageType::Recovery, None);
            let resource = zbi.resource();
            write_image(proxy, resource, &image, data_sink, desired_config).await?;
        }

        if let Some(vbmeta) = &self.recovery.vbmeta {
            let package_url = vbmeta.package_url();
            let proxy = &url_directory_map[package_url];
            let image = Image::new(ImageType::RecoveryVbmeta, None);
            let resource = vbmeta.resource();
            write_image(proxy, resource, &image, data_sink, desired_config).await?;
        }

        Ok(())
    }
}

struct BootSlot {
    zbi: Option<AbsoluteComponentUrl>,
    vbmeta: Option<AbsoluteComponentUrl>,
}

impl BootSlot {
    fn new() -> Self {
        BootSlot { zbi: None, vbmeta: None }
    }

    fn set_zbi(&mut self, zbi: Option<AbsoluteComponentUrl>) -> &mut Self {
        self.zbi = zbi;
        self
    }

    fn set_vbmeta(&mut self, vbmeta: Option<AbsoluteComponentUrl>) -> &mut Self {
        self.vbmeta = vbmeta;
        self
    }

    fn is_empty(&self) -> bool {
        matches!(self, BootSlot { zbi: None, vbmeta: None })
    }

    fn print_names(&self) -> Vec<String> {
        let mut image_names = vec![];
        if self.zbi.is_some() {
            image_names.push("Zbi".to_string());
        }
        if self.vbmeta.is_some() {
            image_names.push("Vbmeta".to_string());
        }

        image_names
    }

    fn get_url_hashes(&self) -> HashSet<fuchsia_hash::Hash> {
        let mut hashes = HashSet::new();

        if let Some(zbi) = &self.zbi {
            if let Some(zbi_hash) = zbi.package_url().hash() {
                hashes.insert(zbi_hash);
            }
        }

        if let Some(vbmeta) = &self.vbmeta {
            if let Some(vbmeta_hash) = vbmeta.package_url().hash() {
                hashes.insert(vbmeta_hash);
            }
        }

        hashes
    }

    fn get_urls(&self) -> HashSet<AbsolutePackageUrl> {
        let mut urls = HashSet::new();
        if let Some(zbi) = &self.zbi {
            urls.insert(zbi.package_url().to_owned());
        }
        if let Some(vbmeta) = &self.vbmeta {
            urls.insert(vbmeta.package_url().to_owned());
        }
        urls
    }
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
    ) -> Result<(state::WaitToReboot, UpdateMode, Vec<fio::DirectoryProxy>), Error> {
        // Prepare
        let state = state::Prepare::enter(co).await;

        let (update_pkg, mode, packages_to_fetch, images_to_write, current_configuration) =
            match self.prepare(target_version).await {
                Ok((
                    update_pkg,
                    mode,
                    packages_to_fetch,
                    images_to_write,
                    current_configuration,
                )) => (update_pkg, mode, packages_to_fetch, images_to_write, current_configuration),
                Err(e) => {
                    state.fail(co, e.reason()).await;
                    bail!(e);
                }
            };

        // Write images
        let mut state = state
            .enter_stage(
                co,
                UpdateInfo::builder().download_size(0).build(),
                packages_to_fetch.len() as u64 + 1,
            )
            .await;
        *phase = metrics::Phase::ImageWrite;

        let () = match self
            .stage_images(
                co,
                &mut state,
                &update_pkg,
                mode,
                current_configuration,
                images_to_write,
                &packages_to_fetch,
            )
            .await
        {
            Ok(()) => (),
            Err(e) => {
                state.fail(co, e.reason()).await;
                bail!(e);
            }
        };

        // Fetch packages
        let mut state = state.enter_fetch(co).await;
        *phase = metrics::Phase::PackageDownload;

        let packages = match self.fetch_packages(co, &mut state, packages_to_fetch, mode).await {
            Ok(packages) => packages,
            Err(e) => {
                state.fail(co, e.reason()).await;
                bail!(e);
            }
        };

        // Commit the update
        let state = state.enter_commit(co).await;
        *phase = metrics::Phase::ImageCommit;

        let () = match self.commit_images(mode, current_configuration).await {
            Ok(()) => (),
            Err(e) => {
                state.fail(co).await;
                bail!(e);
            }
        };

        // Success!
        let state = state.enter_wait_to_reboot(co).await;
        *phase = metrics::Phase::SuccessPendingReboot;

        Ok((state, mode, packages))
    }

    /// Acquire the necessary data to perform the update.
    ///
    /// This includes fetching the update package, which contains the list of packages in the
    /// target OS and kernel images that need written.
    async fn prepare(
        &mut self,
        target_version: &mut history::Version,
    ) -> Result<
        (
            UpdatePackage,
            UpdateMode,
            Vec<AbsolutePackageUrl>,
            Option<ImagesToWrite>,
            paver::CurrentConfiguration,
        ),
        PrepareError,
    > {
        // Ensure that the partition boot metadata is ready for the update to begin. Specifically:
        // - the current configuration must be Healthy and Active, and
        // - the non-current configuration must be Unbootable.
        //
        // If anything goes wrong, abort the update. See the comments in
        // `prepare_partition_metadata` for why this is justified.
        //
        // We do this here rather than just before we write images because this location allows us
        // to "unstage" a previously staged OS in the non-current configuration that would otherwise
        // become active on next reboot. If we moved this to just before writing images, we would be
        // susceptible to a bug of the form:
        // - A is active/current running system version 1.
        // - Stage an OTA of version 2 to B, B is now marked active. Defer reboot.
        // - Start to stage a new OTA (version 3). Fetch packages encounters an error after fetching
        //   half of the updated packages.
        // - Retry the attempt for the new OTA (version 3). This GC may delete packages from the
        //   not-yet-booted system (version 2).
        // - Interrupt the update attempt, reboot.
        // - System attempts to boot to B (version 2), but the packages are not all present anymore
        let current_config = paver::prepare_partition_metadata(&self.env.boot_manager)
            .await
            .map_err(PrepareError::PreparePartitionMetdata)?;

        let update_pkg = resolve_update_package(
            &self.env.pkg_resolver,
            &self.config.update_url,
            &self.env.space_manager,
            &self.env.retained_packages,
        )
        .await
        .map_err(PrepareError::ResolveUpdate)?;

        *target_version = history::Version::for_update_package(&update_pkg).await;
        let () = update_pkg.verify_name().await.map_err(PrepareError::VerifyName)?;

        let mode = update_mode(&update_pkg).await.map_err(PrepareError::ParseUpdateMode)?;
        match mode {
            UpdateMode::Normal => {}
            UpdateMode::ForceRecovery => {
                if !self.config.should_write_recovery {
                    return Err(PrepareError::VerifyUpdateMode);
                }
            }
        }

        verify_board(&self.env.build_info, &update_pkg).await.map_err(PrepareError::VerifyBoard)?;

        let packages_to_fetch = match mode {
            UpdateMode::Normal => {
                update_pkg.packages().await.map_err(PrepareError::ParsePackages)?
            }
            UpdateMode::ForceRecovery => vec![],
        };

        let () = validate_epoch(SOURCE_EPOCH_RAW, &update_pkg).await?;

        let mut images_to_write = ImagesToWrite::new();

        if let Ok(image_package_manifest) = update_pkg.image_packages().await {
            let manifest: ImagePackagesSlots = image_package_manifest.into();

            manifest.verify(mode).map_err(PrepareError::VerifyImages)?;

            if let Some(fuchsia) = manifest.fuchsia() {
                target_version.zbi_hash = fuchsia.zbi().hash().to_string();

                // Determine if the fuchsia zbi has changed in this update. If an error is raised, do not fail the update.
                match asset_to_write(
                    fuchsia.zbi(),
                    current_config,
                    &self.env.data_sink,
                    Asset::Kernel,
                    &Image::new(ImageType::Zbi, None),
                )
                .await
                {
                    Ok(url) => images_to_write.fuchsia.set_zbi(url),
                    Err(e) => {
                        fx_log_err!(
                            "Error while determining whether to write the zbi image, assume update is needed: {:#}", anyhow!(e));
                        images_to_write.fuchsia.set_zbi(Some(fuchsia.zbi().url().to_owned()))
                    }
                };

                if let Some(vbmeta_image) = fuchsia.vbmeta() {
                    target_version.vbmeta_hash = vbmeta_image.hash().to_string();
                    // Determine if the vbmeta has changed in this update. If an error is raised, do not fail the update.
                    match asset_to_write(
                        vbmeta_image,
                        current_config,
                        &self.env.data_sink,
                        Asset::VerifiedBootMetadata,
                        &Image::new(ImageType::FuchsiaVbmeta, None),
                    )
                    .await
                    {
                        Ok(url) => images_to_write.fuchsia.set_vbmeta(url),
                        Err(e) => {
                            fx_log_err!(
                                "Error while determining whether to write the vbmeta image, assume update is needed: {:#}", anyhow!(e));
                            images_to_write.fuchsia.set_vbmeta(Some(vbmeta_image.url().to_owned()))
                        }
                    };
                }
            }

            // Only check these images if we have to.
            if self.config.should_write_recovery {
                if let Some(recovery) = manifest.recovery() {
                    match recovery_to_write(recovery.zbi(), &self.env.data_sink, Asset::Kernel)
                        .await
                    {
                        Ok(url) => images_to_write.recovery.set_zbi(url),
                        Err(e) => {
                            fx_log_err!(
                                "Error while determining whether to write the recovery zbi image, assume update is needed: {:#}", anyhow!(e));
                            images_to_write.recovery.set_zbi(Some(recovery.zbi().url().to_owned()))
                        }
                    };

                    if let Some(vbmeta_image) = recovery.vbmeta() {
                        // Determine if the vbmeta has changed in this update. If an error is raised, do not fail the update.
                        match recovery_to_write(
                            vbmeta_image,
                            &self.env.data_sink,
                            Asset::VerifiedBootMetadata,
                        )
                        .await
                        {
                            Ok(url) => images_to_write.recovery.set_vbmeta(url),
                            Err(e) => {
                                fx_log_err!("Error while determining whether to write the recovery vbmeta image, assume update is needed: {:#}", anyhow!(e));
                                images_to_write
                                    .recovery
                                    .set_vbmeta(Some(vbmeta_image.url().to_owned()))
                            }
                        };
                    }
                }
            }

            for (filename, imagemetadata) in manifest.firmware() {
                match firmware_to_write(
                    filename,
                    imagemetadata,
                    current_config,
                    &self.env.data_sink,
                    &Image::new(ImageType::Firmware, Some(filename)),
                )
                .await
                {
                    Ok(Some(url)) => images_to_write.firmware.push((filename.to_string(), url)),
                    Ok(None) => (),
                    Err(e) => {
                        // If an error is raised, do not fail the update.
                        fx_log_err!("Error while determining firmware to write, assume update is needed: {:#}", anyhow!(e));
                        images_to_write
                            .firmware
                            .push((filename.to_string(), imagemetadata.url().to_owned()))
                    }
                }
            }

            return Ok((
                update_pkg,
                mode,
                packages_to_fetch,
                Some(images_to_write),
                current_config,
            ));
        }

        Ok((update_pkg, mode, packages_to_fetch, None, current_config))
    }

    /// Pave the various raw images (zbi, firmware, vbmeta) for fuchsia and/or recovery.
    #[allow(clippy::too_many_arguments)]
    async fn stage_images(
        &mut self,
        co: &mut async_generator::Yield<State>,
        state: &mut state::Stage,
        update_pkg: &UpdatePackage,
        mode: UpdateMode,
        current_configuration: paver::CurrentConfiguration,
        images_to_write: Option<ImagesToWrite>,
        packages_to_fetch: &[AbsolutePackageUrl],
    ) -> Result<(), StageError> {
        if let Some(images_to_write) = images_to_write {
            if images_to_write.is_empty() {
                // This is possible if the images for the update were on one of the partitions already
                // and written during State::Prepare.
                //
                // This is a separate block so that we avoid unnecessarily replacing the retained index
                // and garbage collecting.
                fx_log_info!("Images have already been written!");

                // Be sure to persist those images that were written during State::Prepare!
                paver::paver_flush_data_sink(&self.env.data_sink)
                    .await
                    .map_err(StageError::PaverFlush)?;

                state.add_progress(co, 1).await;
                return Ok(());
            }

            let () = replace_retained_packages(
                packages_to_fetch
                    .iter()
                    .filter_map(|url| url.hash())
                    .chain(images_to_write.get_url_hashes())
                    .chain(self.config.update_url.hash()),
                &self.env.retained_packages,
            )
            .await
            .unwrap_or_else(|e| {
                fx_log_err!(
                    "unable to replace retained packages set before gc in preparation \
                        for fetching image packages listed in update package: {:#}",
                    anyhow!(e)
                )
            });

            if let Err(e) = gc(&self.env.space_manager).await {
                fx_log_err!(
                    "unable to gc packages in preparation to write image packages: {:#}",
                    anyhow!(e)
                );
            }

            fx_log_info!("Images to write: {:?}", images_to_write.print_list());
            let desired_config = current_configuration.to_non_current_configuration();
            fx_log_info!("Targeting configuration: {:?}", desired_config);

            write_image_packages(
                images_to_write,
                &self.env.pkg_resolver,
                desired_config,
                &self.env.data_sink,
                self.config.update_url.hash(),
                &self.env.retained_packages,
                &self.env.space_manager,
            )
            .await?;

            paver::paver_flush_data_sink(&self.env.data_sink)
                .await
                .map_err(StageError::PaverFlush)?;

            state.add_progress(co, 1).await;
            return Ok(());
        }

        let image_list = [
            ImageType::Bootloader,
            ImageType::Firmware,
            ImageType::Zbi,
            ImageType::ZbiSigned,
            ImageType::FuchsiaVbmeta,
            ImageType::Recovery,
            ImageType::RecoveryVbmeta,
        ];

        let images =
            update_pkg.resolve_images(&image_list[..]).await.map_err(StageError::ResolveImages)?;

        let images = images.verify(mode).map_err(StageError::Verify)?.filter(|image| {
            if self.config.should_write_recovery {
                true
            } else if image.classify().targets_recovery() {
                fx_log_info!("Skipping recovery image: {}", image.name());
                false
            } else {
                true
            }
        });
        fx_log_info!("Images to write via legacy path: {:?}", images);
        let desired_config = current_configuration.to_non_current_configuration();
        fx_log_info!("Targeting configuration: {:?}", desired_config);

        write_images(&self.env.data_sink, update_pkg, desired_config, images.iter())
            .await
            .map_err(StageError::Write)?;
        paver::paver_flush_data_sink(&self.env.data_sink).await.map_err(StageError::PaverFlush)?;

        state.add_progress(co, 1).await;

        Ok(())
    }

    /// Fetch all base packages needed by the target OS.
    async fn fetch_packages(
        &mut self,
        co: &mut async_generator::Yield<State>,
        state: &mut state::Fetch,
        packages_to_fetch: Vec<AbsolutePackageUrl>,
        mode: UpdateMode,
    ) -> Result<Vec<fio::DirectoryProxy>, FetchError> {
        // Remove ImagesToWrite from the retained_index.
        // GC to remove the ImagesToWrite from blobfs.
        let () = replace_retained_packages(
            packages_to_fetch
                .iter()
                .filter_map(|url| url.hash())
                .chain(self.config.update_url.hash()),
            &self.env.retained_packages,
        )
        .await
        .unwrap_or_else(|e| {
            fx_log_err!(
                "unable to replace retained packages set before gc in preparation \
                 for fetching packages listed in update package: {:#}",
                anyhow!(e)
            )
        });

        if let Err(e) = gc(&self.env.space_manager).await {
            fx_log_err!("unable to gc packages during Fetch state: {:#}", anyhow!(e));
        }

        let mut packages = Vec::with_capacity(packages_to_fetch.len());

        let package_dir_futs =
            resolver::resolve_packages(&self.env.pkg_resolver, packages_to_fetch.iter());
        futures::pin_mut!(package_dir_futs);

        while let Some(package_dir) =
            package_dir_futs.try_next().await.map_err(FetchError::Resolve)?
        {
            packages.push(package_dir);

            state.add_progress(co, 1).await;
        }

        match mode {
            UpdateMode::Normal => {
                sync_package_cache(&self.env.pkg_cache).await.map_err(FetchError::Sync)?
            }
            UpdateMode::ForceRecovery => {}
        }

        Ok(packages)
    }

    /// Configure the non-current configuration (or recovery) as active for the next boot.
    async fn commit_images(
        &self,
        mode: UpdateMode,
        current_configuration: paver::CurrentConfiguration,
    ) -> Result<(), Error> {
        let desired_config = current_configuration.to_non_current_configuration();

        match mode {
            UpdateMode::Normal => {
                let () =
                    paver::set_configuration_active(&self.env.boot_manager, desired_config).await?;
            }
            UpdateMode::ForceRecovery => {
                let () = paver::set_recovery_configuration_active(&self.env.boot_manager).await?;
            }
        }

        match desired_config {
            paver::NonCurrentConfiguration::A | paver::NonCurrentConfiguration::B => {
                paver::paver_flush_boot_manager(&self.env.boot_manager).await?;
            }
            paver::NonCurrentConfiguration::NotSupported => {}
        }

        Ok(())
    }
}

async fn write_images<'a, I>(
    data_sink: &DataSinkProxy,
    update_pkg: &UpdatePackage,
    desired_config: paver::NonCurrentConfiguration,
    images: I,
) -> Result<(), Error>
where
    I: Iterator<Item = &'a Image>,
{
    for image in images {
        paver::write_image(data_sink, update_pkg, image, desired_config)
            .await
            .context("while writing images")?;
    }
    Ok(())
}

async fn write_image(
    proxy: &UpdateImagePackage,
    path: &str,
    image: &Image,
    data_sink: &DataSinkProxy,
    desired_config: paver::NonCurrentConfiguration,
) -> Result<(), StageError> {
    let buffer = proxy.open_image(path).await.map_err(StageError::OpenImageError)?;
    paver::write_image_buffer(data_sink, buffer, image, desired_config)
        .await
        .map_err(StageError::Write)?;
    Ok(())
}

/// Ok(None) indicates that the firmware image is on the device in the desired configuration.
/// If the firmware image is on the active configuration, this function will write it to the desired
/// configuration before returning Ok(None).
///
/// Ok(Some(url)) indicates that the firmware image in the update differs from what is on the device.
async fn firmware_to_write(
    filename: &str,
    image_metadata: &update_package::ImageMetadata,
    current_config: paver::CurrentConfiguration,
    data_sink: &DataSinkProxy,
    image: &Image,
) -> Result<Option<AbsoluteComponentUrl>, PrepareError> {
    let desired_config = current_config.to_non_current_configuration();
    if let Some(non_current_config) = desired_config.to_configuration() {
        if (does_firmware_match_hash_and_size(
            data_sink,
            non_current_config,
            filename,
            image_metadata,
        )
        .await?)
            .is_some()
        {
            return Ok(None);
        }

        if let Some(current_config) = current_config.to_configuration() {
            if let Some(buffer) = does_firmware_match_hash_and_size(
                data_sink,
                current_config,
                filename,
                image_metadata,
            )
            .await?
            {
                paver::write_image_buffer(data_sink, buffer, image, desired_config)
                    .await
                    .map_err(PrepareError::PaverWriteFirmware)?;
                return Ok(None);
            }
        }
    }
    Ok(Some(image_metadata.url().to_owned()))
}

/// Ok(None) indicates that the asset is on the device in the desired configuration.
/// If the asset is on the active configuration, this function will write it to the desired
/// configuration before returning Ok(None).
///
/// Ok(Some(url)) indicates that the asset in the update differs from what is on the device.
async fn asset_to_write(
    image_metadata: &update_package::ImageMetadata,
    current_config: paver::CurrentConfiguration,
    data_sink: &DataSinkProxy,
    asset: Asset,
    image: &Image,
) -> Result<Option<AbsoluteComponentUrl>, PrepareError> {
    let desired_config = current_config.to_non_current_configuration();
    if let Some(non_current_config) = desired_config.to_configuration() {
        if (does_asset_match_hash_and_size(data_sink, non_current_config, asset, image_metadata)
            .await?)
            .is_some()
        {
            return Ok(None);
        }

        if let Some(current_config) = current_config.to_configuration() {
            if let Some(buffer) =
                does_asset_match_hash_and_size(data_sink, current_config, asset, image_metadata)
                    .await?
            {
                paver::write_image_buffer(data_sink, buffer, image, desired_config)
                    .await
                    .map_err(PrepareError::PaverWriteAsset)?;
                return Ok(None);
            }

            return Ok(Some(image_metadata.url().to_owned()));
        }
    }
    Ok(Some(image_metadata.url().to_owned()))
}

/// Ok(None) indicates that the recovery asset is on the device in the recovery configuration.
///
/// Ok(Some(url)) indicates that the asset in the update differs from what is on the device.
async fn recovery_to_write(
    image_metadata: &update_package::ImageMetadata,
    data_sink: &DataSinkProxy,
    asset: Asset,
) -> Result<Option<AbsoluteComponentUrl>, PrepareError> {
    if (does_asset_match_hash_and_size(
        data_sink,
        fidl_fuchsia_paver::Configuration::Recovery,
        asset,
        image_metadata,
    )
    .await?)
        .is_some()
    {
        return Ok(None);
    }
    return Ok(Some(image_metadata.url().to_owned()));
}

async fn does_firmware_match_hash_and_size(
    data_sink: &DataSinkProxy,
    desired_configuration: fidl_fuchsia_paver::Configuration,
    subtype: &str,
    image: &update_package::ImageMetadata,
) -> Result<Option<Buffer>, PrepareError> {
    if let Ok(buffer) = paver::paver_read_firmware(data_sink, desired_configuration, subtype).await
    {
        let calculated_hash = calculate_hash(&buffer, image.size() as usize)?;
        if calculated_hash == image.hash() {
            return Ok(Some(buffer));
        }
    }
    Ok(None)
}

async fn does_asset_match_hash_and_size(
    data_sink: &DataSinkProxy,
    configuration: fidl_fuchsia_paver::Configuration,
    asset: Asset,
    image: &update_package::ImageMetadata,
) -> Result<Option<Buffer>, PrepareError> {
    let buffer = paver::paver_read_asset(data_sink, configuration, asset)
        .await
        .map_err(PrepareError::PaverRead)?;
    let calculated_hash = calculate_hash(&buffer, image.size() as usize)?;
    if calculated_hash == image.hash() {
        return Ok(Some(buffer));
    }
    Ok(None)
}

fn calculate_hash(buffer: &Buffer, mut remaining: usize) -> Result<Hash, PrepareError> {
    let mut hasher = Sha256::new();
    let mut offset = 0;

    while remaining > 0 {
        let mut chunk = [0; 4096];
        let chunk_len = remaining.min(4096);
        let chunk = &mut chunk[..chunk_len];

        buffer.vmo.read(chunk, offset).map_err(PrepareError::VmoRead)?;
        hasher.update(chunk);

        offset += chunk_len as u64;
        remaining -= chunk_len;
    }

    Ok(Hash::from(*AsRef::<[u8; 32]>::as_ref(&hasher.finalize())))
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

// Resolve and write the image packages to their appropriate partitions,
// incorporating an increasingly aggressive GC and retry strategy.
async fn write_image_packages(
    images_to_write: ImagesToWrite,
    pkg_resolver: &PackageResolverProxy,
    desired_config: paver::NonCurrentConfiguration,
    data_sink: &DataSinkProxy,
    update_pkg_hash: Option<Hash>,
    retained_packages: &RetainedPackagesProxy,
    space_manager: &SpaceManagerProxy,
) -> Result<(), StageError> {
    match images_to_write.write(pkg_resolver, desired_config, data_sink).await {
        Ok(()) => return Ok(()),
        Err(StageError::Resolve(ResolveError::Error(
            fidl_fuchsia_pkg_ext::ResolveError::NoSpace,
            _,
        ))) => {}
        Err(e) => return Err(e),
    };

    let mut hashes = images_to_write.get_url_hashes();
    if let Some(update_pkg_hash) = update_pkg_hash {
        hashes.insert(update_pkg_hash);
    }

    let () = replace_retained_packages(hashes, retained_packages).await.unwrap_or_else(|e| {
        fx_log_err!(
            "while resolving image packages, unable to minimize retained packages set before \
                    second gc attempt: {:#}",
            anyhow!(e)
        )
    });
    if let Err(e) = gc(space_manager).await {
        fx_log_err!(
            "unable to gc base packages before second image package write retry: {:#}",
            anyhow!(e)
        );
    }

    images_to_write.write(pkg_resolver, desired_config, data_sink).await
}

/// Resolve the update package, incorporating an increasingly aggressive GC and retry strategy.
async fn resolve_update_package(
    pkg_resolver: &PackageResolverProxy,
    update_url: &AbsolutePackageUrl,
    space_manager: &SpaceManagerProxy,
    retained_packages: &RetainedPackagesProxy,
) -> Result<UpdatePackage, ResolveError> {
    // First, attempt to resolve the update package.
    match resolver::resolve_update_package(pkg_resolver, update_url).await {
        Ok(update_pkg) => return Ok(update_pkg),
        Err(ResolveError::Error(fidl_fuchsia_pkg_ext::ResolveError::NoSpace, _)) => (),
        Err(e) => return Err(e),
    }

    // If the first attempt fails with NoSpace, perform a GC and retry.
    if let Err(e) = gc(space_manager).await {
        fx_log_err!("unable to gc packages before first resolve retry: {:#}", anyhow!(e));
    }
    match resolver::resolve_update_package(pkg_resolver, update_url).await {
        Ok(update_pkg) => return Ok(update_pkg),
        Err(ResolveError::Error(fidl_fuchsia_pkg_ext::ResolveError::NoSpace, _)) => (),
        Err(e) => return Err(e),
    }

    // If the second attempt fails with NoSpace, remove packages we aren't sure we need from the
    // retained packages set, perform a GC and retry. If the third attempt fails,
    // return the error regardless of type.
    let () = async {
        if let Some(hash) = update_url.hash() {
            let () = replace_retained_packages(std::iter::once(hash), retained_packages)
                .await
                .context("serve_blob_id_iterator")?;
        } else {
            let () = retained_packages.clear().await.context("calling RetainedPackages.Clear")?;
        }
        Ok(())
    }
    .await
    .unwrap_or_else(|e: anyhow::Error| {
        fx_log_err!(
            "while resolving update package, unable to minimize retained packages set before \
             second gc attempt: {:#}",
            anyhow!(e)
        )
    });

    if let Err(e) = gc(space_manager).await {
        fx_log_err!("unable to gc packages before second resolve retry: {:#}", anyhow!(e));
    }
    resolver::resolve_update_package(pkg_resolver, update_url).await
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

/// Verify that epoch is non-decreasing. For more context, see
/// [RFC-0071](https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0071_ota_backstop).
async fn validate_epoch(source_epoch_raw: &str, pkg: &UpdatePackage) -> Result<(), PrepareError> {
    let src = match serde_json::from_str(source_epoch_raw)
        .map_err(|e| PrepareError::ParseSourceEpochError(source_epoch_raw.to_string(), e))?
    {
        EpochFile::Version1 { epoch } => epoch,
    };
    let target =
        pkg.epoch().await.map_err(PrepareError::ParseTargetEpochError)?.unwrap_or_else(|| {
            fx_log_info!("no epoch in update package, assuming it's 0");
            0
        });
    if target < src {
        return Err(PrepareError::UnsupportedDowngrade { src, target });
    }
    Ok(())
}

async fn replace_retained_packages(
    hashes: impl IntoIterator<Item = fuchsia_hash::Hash>,
    retained_packages: &RetainedPackagesProxy,
) -> Result<(), anyhow::Error> {
    let (client_end, stream) =
        fidl::endpoints::create_request_stream().context("creating request stream")?;
    let replace_resp = retained_packages.replace(client_end);
    let () = fidl_fuchsia_pkg_ext::serve_fidl_iterator_from_slice(
        stream,
        hashes
            .into_iter()
            .map(|hash| fidl_fuchsia_pkg_ext::BlobId::from(hash).into())
            .collect::<Vec<_>>(),
    )
    .await
    .unwrap_or_else(|e| {
        fx_log_err!(
            "error serving {} protocol: {:#}",
            RetainedPackagesMarker::DEBUG_NAME,
            anyhow!(e)
        )
    });
    replace_resp.await.context("calling RetainedPackages.Replace")
}

#[cfg(test)]
mod tests {
    use std::str::FromStr;

    use {
        super::*,
        assert_matches::assert_matches,
        fuchsia_async as fasync,
        fuchsia_pkg_testing::{make_epoch_json, TestUpdatePackage},
        fuchsia_zircon::Status,
    };

    // Simulate the cobalt test hanging indefinitely, and ensure we time out correctly.
    // This test deliberately logs an error.
    #[fasync::run_singlethreaded(test)]
    async fn flush_cobalt_succeeds_when_cobalt_hangs() {
        let hung_task = futures::future::pending();
        flush_cobalt(hung_task, Duration::from_secs(2)).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn validate_epoch_success() {
        let source = make_epoch_json(1);
        let target = make_epoch_json(2);
        let p = TestUpdatePackage::new().add_file("epoch.json", target).await;

        let res = validate_epoch(&source, &p).await;

        assert_matches!(res, Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn validate_epoch_fail_unsupported_downgrade() {
        let source = make_epoch_json(2);
        let target = make_epoch_json(1);
        let p = TestUpdatePackage::new().add_file("epoch.json", target).await;

        let res = validate_epoch(&source, &p).await;

        assert_matches!(res, Err(PrepareError::UnsupportedDowngrade { src: 2, target: 1 }));
    }

    #[fasync::run_singlethreaded(test)]
    async fn validate_epoch_fail_parse_source() {
        let p = TestUpdatePackage::new().add_file("epoch.json", make_epoch_json(1)).await;

        let res = validate_epoch("invalid source epoch.json", &p).await;

        assert_matches!(
            res,
            Err(PrepareError::ParseSourceEpochError(s, _)) if s == "invalid source epoch.json"
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn validate_epoch_fail_parse_target() {
        let p = TestUpdatePackage::new()
            .add_file("epoch.json", "invalid target epoch.json".to_string())
            .await;

        let res = validate_epoch(&make_epoch_json(1), &p).await;

        assert_matches!(res, Err(PrepareError::ParseTargetEpochError(_)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn validate_epoch_target_defaults_to_zero() {
        let p = TestUpdatePackage::new();

        assert_matches!(
            validate_epoch(&make_epoch_json(1), &p).await,
            Err(PrepareError::UnsupportedDowngrade { src: 1, target: 0 })
        );
    }

    fn write_mem_buffer(payload: Vec<u8>) -> Buffer {
        let vmo = fuchsia_zircon::Vmo::create(payload.len() as u64).expect("Creating VMO");
        vmo.write(&payload, 0).expect("writing to VMO");
        Buffer { vmo, size: payload.len() as u64 }
    }

    #[fasync::run_singlethreaded(test)]
    async fn calculate_hash_test_empty_buffer() {
        let buffer = write_mem_buffer(vec![]);
        let image_size = 4_usize;
        let calc_hash = calculate_hash(&buffer, image_size);

        assert_matches!(calc_hash, Err(PrepareError::VmoRead(Status::OUT_OF_RANGE)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn calculate_hash_test_same_size() {
        let buffer = write_mem_buffer(vec![0; 4]);
        let image_size = 4_usize;
        let image_hash =
            Hash::from_str("df3f619804a92fdb4057192dc43dd748ea778adc52bc498ce80524c014b81119")
                .unwrap();
        let calc_hash = calculate_hash(&buffer, image_size).unwrap();

        assert_eq!(calc_hash, image_hash);
    }

    #[fasync::run_singlethreaded(test)]
    async fn calculate_hash_test_image_smaller_than_buffer() {
        let buffer = write_mem_buffer(vec![0; 4]);

        let mut hasher = Sha256::new();
        let mut chunk = [0; 4096];
        let chunk_len = 2_usize;
        let chunk = &mut chunk[..chunk_len];

        buffer.vmo.read(chunk, 0).unwrap();
        hasher.update(chunk);
        let image_hash = Hash::from(*AsRef::<[u8; 32]>::as_ref(&hasher.finalize()));

        let image_size = 2_usize;
        let calc_hash = calculate_hash(&buffer, image_size).unwrap();

        assert_eq!(calc_hash, image_hash);
    }

    #[fasync::run_singlethreaded(test)]
    async fn calculate_hash_test_buffer_larger_than_vmo() {
        let buffer = write_mem_buffer(vec![0; 4097]);
        let image_size = 2_usize;
        let calc_hash = calculate_hash(&buffer, image_size).unwrap();
        let image_hash =
            Hash::from_str("96a296d224f285c67bee93c30f8a309157f0daa35dc5b87e410b78630a09cfc7")
                .unwrap();

        assert_eq!(calc_hash, image_hash);
    }
}
