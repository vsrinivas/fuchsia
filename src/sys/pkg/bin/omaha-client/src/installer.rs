// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is the Fuchsia Installer implementation that talks to fuchsia.update.installer FIDL API.

use crate::{
    app_set::FuchsiaAppSet,
    install_plan::{FuchsiaInstallPlan, UpdatePackageUrl},
};
use anyhow::{anyhow, Context as _};
use fidl_connector::{Connect, ServiceReconnector};
use fidl_fuchsia_hardware_power_statecontrol::RebootReason;
use fidl_fuchsia_pkg::{self as fpkg, CupData, CupMarker, CupProxy, WriteError};
use fidl_fuchsia_update_installer::{
    InstallerMarker, InstallerProxy, RebootControllerMarker, RebootControllerProxy,
};
use fidl_fuchsia_update_installer_ext::{
    start_update, FetchFailureReason, Initiator, MonitorUpdateAttemptError, Options,
    PrepareFailureReason, State, StateId, UpdateAttemptError,
};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_url::PinnedAbsolutePackageUrl;
use fuchsia_zircon as zx;
use futures::{future::LocalBoxFuture, lock::Mutex as AsyncMutex, prelude::*};
use omaha_client::{
    app_set::AppSet as _,
    cup_ecdsa::RequestMetadata,
    installer::{AppInstallResult, Installer, ProgressObserver},
    protocol::{
        request::InstallSource,
        response::{OmahaStatus, Response},
    },
    request_builder::RequestParams,
};
use std::{rc::Rc, time::Duration};
use thiserror::Error;
use tracing::{info, warn};

/// Represents possible reasons the installer could have ended in a failure state. Not exhaustive.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum InstallerFailureReason {
    Internal,
    OutOfSpace,
    UnsupportedDowngrade,
}

impl From<FetchFailureReason> for InstallerFailureReason {
    fn from(r: FetchFailureReason) -> InstallerFailureReason {
        match r {
            FetchFailureReason::Internal => InstallerFailureReason::Internal,
            FetchFailureReason::OutOfSpace => InstallerFailureReason::OutOfSpace,
        }
    }
}

impl From<PrepareFailureReason> for InstallerFailureReason {
    fn from(r: PrepareFailureReason) -> InstallerFailureReason {
        match r {
            PrepareFailureReason::Internal => InstallerFailureReason::Internal,
            PrepareFailureReason::OutOfSpace => InstallerFailureReason::OutOfSpace,
            PrepareFailureReason::UnsupportedDowngrade => {
                InstallerFailureReason::UnsupportedDowngrade
            }
        }
    }
}

/// Information from the config about whether an update is urgent.
#[derive(Debug)]
pub struct InstallResult {
    pub urgent_update: bool,
}

/// Information about a specific failure state that the installer ended in.
#[derive(Debug, Copy, Clone)]
pub struct InstallerFailure {
    state_name: &'static str,
    reason: InstallerFailureReason,
}

impl InstallerFailure {
    /// Returns the name of the system-updater state this failure occurred in
    pub fn state_name(self) -> &'static str {
        self.state_name
    }

    /// Returns the reason this failure occurred
    pub fn reason(self) -> InstallerFailureReason {
        self.reason
    }

    #[cfg(test)]
    pub fn new(state_name: &'static str, reason: InstallerFailureReason) -> Self {
        Self { state_name, reason }
    }
}

#[derive(Debug, Error)]
pub enum FuchsiaInstallError {
    #[error("generic error")]
    Failure(#[from] anyhow::Error),

    #[error("FIDL error")]
    Fidl(#[from] fidl::Error),

    /// System update installer error.
    #[error("start update installer failed")]
    StartUpdate(#[from] UpdateAttemptError),

    #[error("monitor update installer failed")]
    MonitorUpdate(#[from] MonitorUpdateAttemptError),

    #[error("installer encountered failure state: {0:?}")]
    InstallerFailureState(InstallerFailure),

    #[error("installation ended unexpectedly")]
    InstallationEndedUnexpectedly,

    #[error("connect to installer service failed")]
    Connect(#[source] anyhow::Error),

    #[error("eager package cup write failed: {0:?}")]
    CupWrite(WriteError),

    #[error("CupWrite failed, missing request metadata")]
    MissingRequestMetadata,
}

#[derive(Debug)]
pub struct FuchsiaInstaller<
    I = ServiceReconnector<InstallerMarker>,
    C = ServiceReconnector<CupMarker>,
> {
    installer_connector: I,
    cup_connector: C,
    reboot_controller: Option<RebootControllerProxy>,
    app_set: Rc<AsyncMutex<FuchsiaAppSet>>,
}

impl FuchsiaInstaller<ServiceReconnector<InstallerMarker>, ServiceReconnector<CupMarker>> {
    pub fn new(app_set: Rc<AsyncMutex<FuchsiaAppSet>>) -> Self {
        let installer_connector = ServiceReconnector::<InstallerMarker>::new();
        let cup_connector = ServiceReconnector::<CupMarker>::new();
        Self { installer_connector, cup_connector, reboot_controller: None, app_set }
    }
}

impl<I, C> FuchsiaInstaller<I, C>
where
    I: Connect<Proxy = InstallerProxy> + Send,
    C: Connect<Proxy = CupProxy> + Send,
{
    async fn perform_install_system_update<'a>(
        &'a mut self,
        url: &'a PinnedAbsolutePackageUrl,
        install_source: &'a InstallSource,
        observer: Option<&'a dyn ProgressObserver>,
    ) -> Result<(), FuchsiaInstallError> {
        let options = Options {
            initiator: match install_source {
                InstallSource::ScheduledTask => Initiator::Service,
                InstallSource::OnDemand => Initiator::User,
            },
            should_write_recovery: true,
            allow_attach_to_existing_attempt: true,
        };

        let proxy = self.installer_connector.connect().map_err(FuchsiaInstallError::Connect)?;
        let (reboot_controller, reboot_controller_server_end) =
            fidl::endpoints::create_proxy::<RebootControllerMarker>()
                .map_err(FuchsiaInstallError::Fidl)?;

        self.reboot_controller = Some(reboot_controller);

        let mut update_attempt =
            start_update(&url.clone().into(), options, &proxy, Some(reboot_controller_server_end))
                .await?;

        while let Some(state) = update_attempt.try_next().await? {
            info!("Installer entered state: {}", state.name());
            if let Some(observer) = observer {
                if let Some(progress) = state.progress() {
                    observer
                        .receive_progress(
                            Some(state.name()),
                            progress.fraction_completed(),
                            state.download_size(),
                            Some(progress.bytes_downloaded()),
                        )
                        .await;
                } else {
                    observer.receive_progress(Some(state.name()), 0., None, None).await;
                }
            }
            if state.id() == StateId::WaitToReboot || state.is_success() {
                return Ok(());
            } else if state.is_failure() {
                match state {
                    State::FailFetch(fail_fetch_data) => {
                        return Err(FuchsiaInstallError::InstallerFailureState(InstallerFailure {
                            state_name: state.name(),
                            reason: fail_fetch_data.reason().into(),
                        }));
                    }
                    State::FailPrepare(prepare_failure_reason) => {
                        return Err(FuchsiaInstallError::InstallerFailureState(InstallerFailure {
                            state_name: state.name(),
                            reason: prepare_failure_reason.into(),
                        }));
                    }
                    _ => {
                        return Err(FuchsiaInstallError::InstallerFailureState(InstallerFailure {
                            state_name: state.name(),
                            reason: InstallerFailureReason::Internal,
                        }))
                    }
                }
            }
        }

        Err(FuchsiaInstallError::InstallationEndedUnexpectedly)
    }

    async fn perform_install_eager_package(
        &mut self,
        url: &PinnedAbsolutePackageUrl,
        install_plan: &FuchsiaInstallPlan,
    ) -> Result<(), FuchsiaInstallError> {
        let proxy = self.cup_connector.connect().map_err(FuchsiaInstallError::Connect)?;
        let mut url = fpkg::PackageUrl { url: url.to_string() };
        let rm = install_plan
            .request_metadata
            .as_ref()
            .ok_or(FuchsiaInstallError::MissingRequestMetadata)?;
        let cup_data: CupData = CupData {
            request: Some(rm.request_body.clone()),
            key_id: Some(rm.public_key_id),
            nonce: Some(rm.nonce.into()),
            response: Some(install_plan.omaha_response.clone()),
            signature: install_plan.ecdsa_signature.as_ref().cloned(),
            ..CupData::EMPTY
        };
        proxy.write(&mut url, cup_data).await?.map_err(FuchsiaInstallError::CupWrite)
    }
}

impl<I, C> Installer for FuchsiaInstaller<I, C>
where
    I: Connect<Proxy = InstallerProxy> + Send,
    C: Connect<Proxy = CupProxy> + Send,
{
    type InstallPlan = FuchsiaInstallPlan;
    type Error = FuchsiaInstallError;
    type InstallResult = InstallResult;

    fn perform_install<'a>(
        &'a mut self,
        install_plan: &'a FuchsiaInstallPlan,
        observer: Option<&'a dyn ProgressObserver>,
    ) -> LocalBoxFuture<'a, (Self::InstallResult, Vec<AppInstallResult<Self::Error>>)> {
        let is_system_update = install_plan.is_system_update();

        async move {
            let mut app_results = vec![];
            for (i, url) in install_plan.update_package_urls.iter().enumerate() {
                app_results.push(match url {
                    UpdatePackageUrl::System(ref url) => self
                        .perform_install_system_update(url, &install_plan.install_source, observer)
                        .await
                        .into(),
                    UpdatePackageUrl::Package(ref url) => {
                        if is_system_update {
                            AppInstallResult::Deferred
                        } else {
                            let result =
                                self.perform_install_eager_package(url, install_plan).await.into();
                            if let Some(observer) = observer {
                                observer
                                    .receive_progress(
                                        Some(&url.to_string()),
                                        (i + 1) as f32
                                            / install_plan.update_package_urls.len() as f32,
                                        None,
                                        None,
                                    )
                                    .await;
                            }
                            result
                        }
                    }
                });
            }
            (InstallResult { urgent_update: install_plan.urgent_update }, app_results)
        }
        .boxed_local()
    }

    fn perform_reboot(&mut self) -> LocalBoxFuture<'_, Result<(), anyhow::Error>> {
        async move {
            match self.reboot_controller.take() {
                Some(reboot_controller) => {
                    reboot_controller
                        .unblock()
                        .context("notify installer it can reboot when ready")?;
                }
                None => {
                    // FIXME Need the direct reboot path anymore?
                    connect_to_protocol::<fidl_fuchsia_hardware_power_statecontrol::AdminMarker>()?
                        .reboot(RebootReason::SystemUpdate)
                        .await?
                        .map_err(zx::Status::from_raw)
                        .context("reboot error")?;
                }
            }

            // Device should be rebooting now, do not return because state machine expects
            // perform_reboot() to block, wait for 5 minutes and if reboot still hasn't happened,
            // return an error.
            fasync::Timer::new(Duration::from_secs(60 * 5)).await;

            Err(anyhow!("timed out while waiting for device to reboot"))
        }
        .boxed_local()
    }

    fn try_create_install_plan<'a>(
        &'a self,
        request_params: &'a RequestParams,
        request_metadata: Option<&'a RequestMetadata>,
        response: &'a Response,
        response_bytes: Vec<u8>,
        ecdsa_signature: Option<Vec<u8>>,
    ) -> LocalBoxFuture<'a, Result<Self::InstallPlan, Self::Error>> {
        async move {
            let system_app_id = self.app_set.lock().await.get_system_app_id().to_owned();
            try_create_install_plan_impl(
                request_params,
                request_metadata,
                response,
                response_bytes,
                ecdsa_signature,
                system_app_id,
            )
        }
        .boxed_local()
    }
}

fn try_create_install_plan_impl(
    request_params: &RequestParams,
    request_metadata: Option<&RequestMetadata>,
    response: &Response,
    response_bytes: Vec<u8>,
    ecdsa_signature: Option<Vec<u8>>,
    system_app_id: String,
) -> Result<FuchsiaInstallPlan, FuchsiaInstallError> {
    let mut update_package_urls = vec![];
    let mut urgent_update = false;

    if response.apps.is_empty() {
        return Err(FuchsiaInstallError::Failure(anyhow!("No app in Omaha response")));
    }

    for app in &response.apps {
        if app.status != OmahaStatus::Ok {
            return Err(FuchsiaInstallError::Failure(anyhow!(
                "Found non-ok app status for {:?}: {:?}",
                app.id,
                app.status
            )));
        }
        let update_check = if let Some(update_check) = &app.update_check {
            update_check
        } else {
            return Err(FuchsiaInstallError::Failure(anyhow!("No update_check in Omaha response")));
        };

        let mut urls = match update_check.status {
            OmahaStatus::Ok => update_check.get_all_url_codebases(),
            OmahaStatus::NoUpdate => {
                continue;
            }
            _ => {
                if let Some(info) = &update_check.info {
                    warn!("update check status info: {}", info);
                }
                return Err(FuchsiaInstallError::Failure(anyhow!(
                    "Unexpected update check status: {:?}",
                    update_check.status
                )));
            }
        };
        let url = urls
            .next()
            .ok_or_else(|| FuchsiaInstallError::Failure(anyhow!("No url in Omaha response")))?;

        let rest_count = urls.count();
        if rest_count != 0 {
            warn!("Only 1 url is supported, found {}", rest_count + 1);
        }

        let mut packages = update_check.get_all_packages();
        let package = packages
            .next()
            .ok_or_else(|| FuchsiaInstallError::Failure(anyhow!("No package in Omaha response")))?;

        let rest_count = packages.count();
        if rest_count != 0 {
            warn!("Only 1 package is supported, found {}", rest_count + 1);
        }

        let full_url = url.to_owned() + &package.name;

        let pkg_url = match PinnedAbsolutePackageUrl::parse(&full_url) {
            Ok(pkg_url) => pkg_url,
            Err(err) => {
                return Err(FuchsiaInstallError::Failure(anyhow!(
                    "Failed to parse {} to PinnedAbsolutePackageUrl: {}",
                    full_url,
                    err
                )))
            }
        };

        update_package_urls.push(if app.id == system_app_id {
            urgent_update = update_check.urgent_update.unwrap_or(false);

            UpdatePackageUrl::System(pkg_url)
        } else {
            UpdatePackageUrl::Package(pkg_url)
        });
    }
    if update_package_urls.is_empty() {
        return Err(FuchsiaInstallError::Failure(anyhow!("No app has update available")));
    }
    Ok(FuchsiaInstallPlan {
        update_package_urls,
        install_source: request_params.source,
        urgent_update,
        omaha_response: response_bytes,
        request_metadata: request_metadata.cloned(),
        ecdsa_signature,
    })
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::app_set::{AppIdSource, AppMetadata},
        assert_matches::assert_matches,
        fidl_fuchsia_pkg::{CupRequest, CupRequestStream},
        fidl_fuchsia_update_installer::{
            FailPrepareData, InstallationProgress, InstallerRequest, InstallerRequestStream,
            RebootControllerRequest, State, UpdateInfo,
        },
        fuchsia_async as fasync,
        futures::future::BoxFuture,
        omaha_client::protocol::response::{App, Manifest, Package, Packages, UpdateCheck},
        parking_lot::Mutex,
        std::{convert::TryInto, sync::Arc, task::Poll},
    };

    const TEST_URL: &str = "fuchsia-pkg://fuchsia.test/update/0?hash=0000000000000000000000000000000000000000000000000000000000000000";
    const TEST_URL_BASE: &str = "fuchsia-pkg://fuchsia.test/";
    const TEST_PACKAGE_NAME: &str =
        "update/0?hash=0000000000000000000000000000000000000000000000000000000000000000";

    #[derive(Debug, PartialEq)]
    struct Progress {
        operation: Option<String>,
        progress: f32,
        total_size: Option<u64>,
        size_so_far: Option<u64>,
    }

    impl Eq for Progress {}
    struct MockProgressObserver {
        progresses: Arc<Mutex<Vec<Progress>>>,
    }

    impl MockProgressObserver {
        fn new() -> Self {
            Self { progresses: Arc::new(Mutex::new(vec![])) }
        }
        fn progresses(&self) -> Arc<Mutex<Vec<Progress>>> {
            Arc::clone(&self.progresses)
        }
    }
    impl ProgressObserver for MockProgressObserver {
        fn receive_progress(
            &self,
            operation: Option<&str>,
            progress: f32,
            total_size: Option<u64>,
            size_so_far: Option<u64>,
        ) -> BoxFuture<'_, ()> {
            let operation = operation.map(|s| s.into());
            self.progresses.lock().push(Progress { operation, progress, total_size, size_so_far });
            future::ready(()).boxed()
        }
    }

    struct MockConnector<T> {
        proxy: Option<T>,
    }

    impl<T> MockConnector<T> {
        fn new(proxy: T) -> Self {
            Self { proxy: Some(proxy) }
        }
        fn failing() -> Self {
            Self { proxy: None }
        }
    }

    impl<T: fidl::endpoints::Proxy + Clone> Connect for MockConnector<T> {
        type Proxy = T;

        fn connect(&self) -> Result<Self::Proxy, anyhow::Error> {
            self.proxy.clone().ok_or_else(|| anyhow::anyhow!("no proxy available"))
        }
    }

    fn new_mock_installer() -> (
        FuchsiaInstaller<MockConnector<InstallerProxy>, MockConnector<CupProxy>>,
        InstallerRequestStream,
        CupRequestStream,
    ) {
        let (installer_proxy, installer_stream) =
            fidl::endpoints::create_proxy_and_stream::<InstallerMarker>().unwrap();
        let (cup_proxy, cup_stream) =
            fidl::endpoints::create_proxy_and_stream::<CupMarker>().unwrap();
        let app = omaha_client::common::App::builder().id("system_id").version([1]).build();
        let app_metadata = AppMetadata { appid_source: AppIdSource::VbMetadata };
        let app_set = Rc::new(AsyncMutex::new(FuchsiaAppSet::new(app, app_metadata)));
        let installer = FuchsiaInstaller {
            installer_connector: MockConnector::new(installer_proxy),
            cup_connector: MockConnector::new(cup_proxy),
            reboot_controller: None,
            app_set,
        };
        (installer, installer_stream, cup_stream)
    }

    fn new_installer() -> FuchsiaInstaller<ServiceReconnector<InstallerMarker>> {
        let app = omaha_client::common::App::builder().id("system_id").version([1]).build();
        let app_metadata = AppMetadata { appid_source: AppIdSource::VbMetadata };
        let app_set = Rc::new(AsyncMutex::new(FuchsiaAppSet::new(app, app_metadata)));
        FuchsiaInstaller::new(app_set)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_start_update() {
        let (mut installer, mut stream, _) = new_mock_installer();
        let plan = FuchsiaInstallPlan {
            update_package_urls: vec![
                UpdatePackageUrl::System(TEST_URL.parse().unwrap()),
                UpdatePackageUrl::Package(TEST_URL.parse().unwrap()),
            ],
            install_source: InstallSource::OnDemand,
            ..FuchsiaInstallPlan::default()
        };
        let observer = MockProgressObserver::new();
        let progresses = observer.progresses();
        let installer_fut = async move {
            let (install_result, app_install_results) =
                installer.perform_install(&plan, Some(&observer)).await;
            assert!(!install_result.urgent_update);
            assert_matches!(
                app_install_results.as_slice(),
                &[AppInstallResult::Installed, AppInstallResult::Deferred]
            );
            assert_matches!(installer.reboot_controller, Some(_));
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(InstallerRequest::StartUpdate {
                    url,
                    options,
                    monitor,
                    reboot_controller,
                    responder,
                }) => {
                    assert_eq!(url.url, TEST_URL);
                    let Options {
                        initiator,
                        should_write_recovery,
                        allow_attach_to_existing_attempt,
                    } = options.try_into().unwrap();
                    assert_eq!(initiator, Initiator::User);
                    assert_matches!(reboot_controller, Some(_));
                    assert!(should_write_recovery);
                    assert!(allow_attach_to_existing_attempt);
                    responder
                        .send(&mut Ok("00000000-0000-0000-0000-000000000001".to_owned()))
                        .unwrap();
                    let monitor = monitor.into_proxy().unwrap();
                    let () = monitor
                        .on_state(&mut State::Stage(fidl_fuchsia_update_installer::StageData {
                            info: Some(UpdateInfo {
                                download_size: Some(1000),
                                ..UpdateInfo::EMPTY
                            }),
                            progress: Some(InstallationProgress {
                                fraction_completed: Some(0.5),
                                bytes_downloaded: Some(500),
                                ..InstallationProgress::EMPTY
                            }),
                            ..fidl_fuchsia_update_installer::StageData::EMPTY
                        }))
                        .await
                        .unwrap();
                    let () = monitor
                        .on_state(&mut State::WaitToReboot(
                            fidl_fuchsia_update_installer::WaitToRebootData {
                                info: Some(UpdateInfo {
                                    download_size: Some(1000),
                                    ..UpdateInfo::EMPTY
                                }),
                                progress: Some(InstallationProgress {
                                    fraction_completed: Some(1.0),
                                    bytes_downloaded: Some(1000),
                                    ..InstallationProgress::EMPTY
                                }),
                                ..fidl_fuchsia_update_installer::WaitToRebootData::EMPTY
                            },
                        ))
                        .await
                        .unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(installer_fut, stream_fut).await;
        assert_eq!(
            *progresses.lock(),
            vec![
                Progress {
                    operation: Some("stage".to_string()),
                    progress: 0.5,
                    total_size: Some(1000),
                    size_so_far: Some(500)
                },
                Progress {
                    operation: Some("wait_to_reboot".to_string()),
                    progress: 1.0,
                    total_size: Some(1000),
                    size_so_far: Some(1000)
                }
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_eager_package_update() {
        let (mut installer, _, mut stream) = new_mock_installer();
        let plan = FuchsiaInstallPlan {
            update_package_urls: vec![UpdatePackageUrl::Package(TEST_URL.parse().unwrap())],
            install_source: InstallSource::OnDemand,
            omaha_response: vec![1, 2, 3],
            request_metadata: Some(RequestMetadata {
                request_body: vec![4, 5, 6],
                public_key_id: 7_u64,
                nonce: [8_u8; 32].into(),
            }),
            ecdsa_signature: Some(vec![10, 11, 12]),
            ..FuchsiaInstallPlan::default()
        };
        let observer = MockProgressObserver::new();
        let progresses = observer.progresses();
        let installer_fut = async move {
            let (install_result, app_install_results) =
                installer.perform_install(&plan, Some(&observer)).await;
            assert!(!install_result.urgent_update);
            assert_matches!(app_install_results.as_slice(), &[AppInstallResult::Installed]);
            assert_matches!(installer.reboot_controller, None);
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(CupRequest::Write { url, cup, responder }) => {
                    assert_eq!(url.url, TEST_URL);
                    let CupData { request, key_id, nonce, response, signature, .. } = cup;
                    assert_eq!(request, Some(vec![4, 5, 6]));
                    assert_eq!(key_id, Some(7_u64));
                    assert_eq!(nonce, Some([8_u8; 32]));
                    assert_eq!(response, Some(vec![1, 2, 3]));
                    assert_eq!(signature, Some(vec![10, 11, 12]));
                    responder.send(&mut Ok(())).unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(installer_fut, stream_fut).await;
        assert_eq!(
            *progresses.lock(),
            vec![Progress {
                operation: Some(TEST_URL.to_string()),
                progress: 1.0,
                total_size: None,
                size_so_far: None
            }]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_install_error() {
        let (mut installer, mut stream, _) = new_mock_installer();
        let plan = FuchsiaInstallPlan {
            update_package_urls: vec![UpdatePackageUrl::System(TEST_URL.parse().unwrap())],
            ..FuchsiaInstallPlan::default()
        };
        let installer_fut = async move {
            assert_matches!(
                installer.perform_install(&plan, None).await.1.as_slice(),
                &[AppInstallResult::Failed(FuchsiaInstallError::InstallerFailureState(
                    InstallerFailure {
                        state_name: "fail_prepare",
                        reason: InstallerFailureReason::OutOfSpace
                    }
                ))]
            );
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(InstallerRequest::StartUpdate { monitor, responder, .. }) => {
                    responder
                        .send(&mut Ok("00000000-0000-0000-0000-000000000002".to_owned()))
                        .unwrap();

                    let monitor = monitor.into_proxy().unwrap();
                    let () = monitor
                        .on_state(&mut State::FailPrepare(FailPrepareData {
                            reason: Some(
                                fidl_fuchsia_update_installer::PrepareFailureReason::OutOfSpace,
                            ),
                            ..FailPrepareData::EMPTY
                        }))
                        .await
                        .unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(installer_fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_server_close_unexpectedly() {
        let (mut installer, mut stream, _) = new_mock_installer();
        let plan = FuchsiaInstallPlan {
            update_package_urls: vec![UpdatePackageUrl::System(TEST_URL.parse().unwrap())],
            ..FuchsiaInstallPlan::default()
        };
        let installer_fut = async move {
            assert_matches!(
                installer.perform_install(&plan, None).await.1.as_slice(),
                &[AppInstallResult::Failed(FuchsiaInstallError::InstallationEndedUnexpectedly)]
            );
        };
        let stream_fut = async move {
            match stream.next().await.unwrap() {
                Ok(InstallerRequest::StartUpdate { monitor, responder, .. }) => {
                    responder
                        .send(&mut Ok("00000000-0000-0000-0000-000000000003".to_owned()))
                        .unwrap();

                    let monitor = monitor.into_proxy().unwrap();
                    let () = monitor
                        .on_state(&mut State::Prepare(
                            fidl_fuchsia_update_installer::PrepareData::EMPTY,
                        ))
                        .await
                        .unwrap();
                    let () = monitor
                        .on_state(&mut State::Fetch(fidl_fuchsia_update_installer::FetchData {
                            info: Some(UpdateInfo { download_size: None, ..UpdateInfo::EMPTY }),
                            progress: Some(InstallationProgress {
                                fraction_completed: Some(0.0),
                                bytes_downloaded: None,
                                ..InstallationProgress::EMPTY
                            }),
                            ..fidl_fuchsia_update_installer::FetchData::EMPTY
                        }))
                        .await
                        .unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        };
        future::join(installer_fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_connect_to_installer_failed() {
        let (mut installer, _, _) = new_mock_installer();
        installer.installer_connector = MockConnector::failing();
        let plan = FuchsiaInstallPlan {
            update_package_urls: vec![UpdatePackageUrl::System(TEST_URL.parse().unwrap())],
            ..FuchsiaInstallPlan::default()
        };
        assert_matches!(
            installer.perform_install(&plan, None).await.1.as_slice(),
            &[AppInstallResult::Failed(FuchsiaInstallError::Connect(_))]
        );
    }

    #[test]
    fn test_reboot() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let mut installer = new_installer();
        let (reboot_controller, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<RebootControllerMarker>().unwrap();
        installer.reboot_controller = Some(reboot_controller);

        {
            let mut reboot_future = installer.perform_reboot();
            assert_matches!(exec.run_until_stalled(&mut reboot_future), Poll::Pending);
            assert_matches!(exec.wake_next_timer(), Some(_));
            assert_matches!(exec.run_until_stalled(&mut reboot_future), Poll::Ready(Err(_)));
        }

        assert_matches!(installer.reboot_controller, None);
        assert_matches!(
            exec.run_singlethreaded(stream.next()),
            Some(Ok(RebootControllerRequest::Unblock { .. }))
        );
        assert_matches!(exec.run_singlethreaded(stream.next()), None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_simple_response() {
        let request_params = RequestParams::default();
        let request_metadata = RequestMetadata {
            request_body: vec![4, 5, 6],
            public_key_id: 7_u64,
            nonce: [8_u8; 32].into(),
        };
        let signature = Some(vec![13, 14, 15]);
        let mut update_check = UpdateCheck::ok([TEST_URL_BASE]);
        update_check.manifest = Some(Manifest {
            packages: Packages::new(vec![Package::with_name(TEST_PACKAGE_NAME)]),
            ..Manifest::default()
        });
        let response = Response {
            apps: vec![App {
                update_check: Some(update_check),
                id: "system_id".into(),
                ..App::default()
            }],
            ..Response::default()
        };

        let install_plan = new_installer()
            .try_create_install_plan(
                &request_params,
                Some(&request_metadata),
                &response,
                vec![1, 2, 3],
                signature,
            )
            .await
            .unwrap();
        assert_eq!(
            install_plan.update_package_urls,
            vec![UpdatePackageUrl::System(TEST_URL.parse().unwrap())],
        );
        assert_eq!(install_plan.install_source, request_params.source);
        assert!(!install_plan.urgent_update);
        assert_eq!(install_plan.omaha_response, vec![1, 2, 3]);
        assert_eq!(
            install_plan.request_metadata,
            Some(RequestMetadata {
                request_body: vec![4, 5, 6],
                public_key_id: 7_u64,
                nonce: [8_u8; 32].into(),
            })
        );
        assert_eq!(install_plan.ecdsa_signature, Some(vec![13, 14, 15]));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_app() {
        let request_params = RequestParams::default();
        let request_metadata = None;
        let signature = None;
        let response = Response::default();

        assert_matches!(
            new_installer()
                .try_create_install_plan(
                    &request_params,
                    request_metadata,
                    &response,
                    vec![],
                    signature
                )
                .await,
            Err(FuchsiaInstallError::Failure(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_app() {
        let request_params = RequestParams::default();
        let request_metadata = None;
        let signature = None;

        let system_app = App {
            update_check: Some(UpdateCheck {
                manifest: Some(Manifest {
                    packages: Packages::new(vec![Package::with_name(TEST_PACKAGE_NAME)]),
                    ..Manifest::default()
                }),
                ..UpdateCheck::ok([TEST_URL_BASE])
            }),
            id: "system_id".into(),
            ..App::default()
        };
        let response = Response {
            apps: vec![
                system_app,
                App { update_check: Some(UpdateCheck::no_update()), ..App::default() },
            ],
            ..Response::default()
        };

        let install_plan = new_installer()
            .try_create_install_plan(
                &request_params,
                request_metadata,
                &response,
                vec![],
                signature,
            )
            .await
            .unwrap();
        assert_eq!(
            install_plan.update_package_urls,
            vec![UpdatePackageUrl::System(TEST_URL.parse().unwrap())],
        );
        assert_eq!(install_plan.install_source, request_params.source);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_package_updates() {
        let request_params = RequestParams::default();
        let request_metadata = None;
        let signature = None;

        let system_app = App {
            update_check: Some(UpdateCheck::no_update()),
            id: "system_id".into(),
            ..App::default()
        };
        let package1_app = App {
            update_check: Some(UpdateCheck {
                manifest: Some(Manifest {
                    packages: Packages::new(vec![Package::with_name("package1?hash=0000000000000000000000000000000000000000000000000000000000000000")]),
                    ..Manifest::default()
                }),
                ..UpdateCheck::ok([TEST_URL_BASE])
            }),
            id: "package1_id".into(),
            ..App::default()
        };
        let package2_app = App {
            update_check: Some(UpdateCheck::no_update()),
            id: "package2_id".into(),
            ..App::default()
        };
        let package3_app = App {
            update_check: Some(UpdateCheck {
                manifest: Some(Manifest {
                    packages: Packages::new(vec![Package::with_name("package3?hash=0000000000000000000000000000000000000000000000000000000000000000")]),
                    ..Manifest::default()
                }),
                ..UpdateCheck::ok([TEST_URL_BASE])
            }),
            id: "package3_id".into(),
            ..App::default()
        };
        let response = Response {
            apps: vec![system_app, package1_app, package2_app, package3_app],
            ..Response::default()
        };

        let install_plan = new_installer()
            .try_create_install_plan(
                &request_params,
                request_metadata,
                &response,
                vec![],
                signature,
            )
            .await
            .unwrap();
        assert_eq!(
            install_plan.update_package_urls,
            vec![
                UpdatePackageUrl::Package(format!("{TEST_URL_BASE}package1?hash=0000000000000000000000000000000000000000000000000000000000000000").parse().unwrap()),
                UpdatePackageUrl::Package(format!("{TEST_URL_BASE}package3?hash=0000000000000000000000000000000000000000000000000000000000000000").parse().unwrap())
            ]
        );
        assert_eq!(install_plan.install_source, request_params.source);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_mixed_update() {
        let request_params = RequestParams::default();
        let request_metadata = None;
        let signature = None;
        let system_app = App {
            update_check: Some(UpdateCheck {
                manifest: Some(Manifest {
                    packages: Packages::new(vec![Package::with_name(TEST_PACKAGE_NAME)]),
                    ..Manifest::default()
                }),
                ..UpdateCheck::ok([TEST_URL_BASE])
            }),
            id: "system_id".into(),
            ..App::default()
        };
        let package_app = App {
            update_check: Some(UpdateCheck {
                manifest: Some(Manifest {
                    packages: Packages::new(vec![Package::with_name("some-package?hash=0000000000000000000000000000000000000000000000000000000000000000")]),
                    ..Manifest::default()
                }),
                ..UpdateCheck::ok([TEST_URL_BASE])
            }),
            id: "package_id".into(),
            ..App::default()
        };
        let response = Response { apps: vec![package_app, system_app], ..Response::default() };

        let install_plan = new_installer()
            .try_create_install_plan(
                &request_params,
                request_metadata,
                &response,
                vec![],
                signature,
            )
            .await
            .unwrap();
        assert_eq!(
            install_plan.update_package_urls,
            vec![
                UpdatePackageUrl::Package(format!("{TEST_URL_BASE}some-package?hash=0000000000000000000000000000000000000000000000000000000000000000").parse().unwrap()),
                UpdatePackageUrl::System(TEST_URL.parse().unwrap())
            ],
        );
        assert_eq!(install_plan.install_source, request_params.source);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_update_check() {
        let request_params = RequestParams::default();
        let request_metadata = None;
        let signature = None;
        let response = Response {
            apps: vec![App { id: "system_id".into(), ..App::default() }],
            ..Response::default()
        };

        assert_matches!(
            new_installer()
                .try_create_install_plan(
                    &request_params,
                    request_metadata,
                    &response,
                    vec![],
                    signature
                )
                .await,
            Err(FuchsiaInstallError::Failure(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_urls() {
        let request_params = RequestParams::default();
        let request_metadata = None;
        let signature = None;
        let response = Response {
            apps: vec![App {
                update_check: Some(UpdateCheck::default()),
                id: "system_id".into(),
                ..App::default()
            }],
            ..Response::default()
        };

        assert_matches!(
            new_installer()
                .try_create_install_plan(
                    &request_params,
                    request_metadata,
                    &response,
                    vec![],
                    signature
                )
                .await,
            Err(FuchsiaInstallError::Failure(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_app_error_status() {
        let request_params = RequestParams::default();
        let request_metadata = None;
        let signature = None;
        let response = Response {
            apps: vec![App {
                status: OmahaStatus::Error("error-unknownApplication".to_string()),
                ..App::default()
            }],
            ..Response::default()
        };

        assert_matches!(
            new_installer()
                .try_create_install_plan(
                    &request_params,
                    request_metadata,
                    &response,
                    vec![],
                    signature
                )
                .await,
            Err(FuchsiaInstallError::Failure(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_update() {
        let request_params = RequestParams::default();
        let request_metadata = None;
        let signature = None;
        let response = Response {
            apps: vec![App { update_check: Some(UpdateCheck::no_update()), ..App::default() }],
            ..Response::default()
        };

        assert_matches!(
            new_installer()
                .try_create_install_plan(
                    &request_params,
                    request_metadata,
                    &response,
                    vec![],
                    signature
                )
                .await,
            Err(FuchsiaInstallError::Failure(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_invalid_url() {
        let request_params = RequestParams::default();
        let request_metadata = None;
        let signature = None;
        let response = Response {
            apps: vec![App {
                update_check: Some(UpdateCheck::ok(["invalid-url"])),
                id: "system_id".into(),
                ..App::default()
            }],
            ..Response::default()
        };

        assert_matches!(
            new_installer()
                .try_create_install_plan(
                    &request_params,
                    request_metadata,
                    &response,
                    vec![],
                    signature
                )
                .await,
            Err(FuchsiaInstallError::Failure(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_manifest() {
        let request_params = RequestParams::default();
        let request_metadata = None;
        let signature = None;
        let response = Response {
            apps: vec![App {
                update_check: Some(UpdateCheck::ok([TEST_URL_BASE])),
                id: "system_id".into(),
                ..App::default()
            }],
            ..Response::default()
        };

        assert_matches!(
            new_installer()
                .try_create_install_plan(
                    &request_params,
                    request_metadata,
                    &response,
                    vec![],
                    signature
                )
                .await,
            Err(FuchsiaInstallError::Failure(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_urgent_update_attribute_true() {
        let request_params = RequestParams::default();
        let request_metadata = None;
        let signature = None;
        let mut update_check = UpdateCheck::ok([TEST_URL_BASE]);
        update_check.urgent_update = Some(true);
        update_check.manifest = Some(Manifest {
            packages: Packages::new(vec![Package::with_name(TEST_PACKAGE_NAME)]),
            ..Manifest::default()
        });
        let response = Response {
            apps: vec![App {
                update_check: Some(update_check),
                id: "system_id".into(),
                ..App::default()
            }],
            ..Response::default()
        };

        let install_plan = new_installer()
            .try_create_install_plan(
                &request_params,
                request_metadata,
                &response,
                vec![],
                signature,
            )
            .await
            .unwrap();

        assert!(install_plan.urgent_update);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_urgent_update_attribute_false() {
        let request_params = RequestParams::default();
        let request_metadata = None;
        let signature = None;
        let mut update_check = UpdateCheck::ok([TEST_URL_BASE]);
        update_check.urgent_update = Some(false);
        update_check.manifest = Some(Manifest {
            packages: Packages::new(vec![Package::with_name(TEST_PACKAGE_NAME)]),
            ..Manifest::default()
        });
        let response = Response {
            apps: vec![App {
                update_check: Some(update_check),
                id: "system_id".into(),
                ..App::default()
            }],
            ..Response::default()
        };

        let install_plan = new_installer()
            .try_create_install_plan(
                &request_params,
                request_metadata,
                &response,
                vec![],
                signature,
            )
            .await
            .unwrap();

        assert!(!install_plan.urgent_update);
    }
}
