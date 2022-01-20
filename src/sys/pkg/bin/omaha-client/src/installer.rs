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
use fidl_fuchsia_update_installer::{
    InstallerMarker, InstallerProxy, RebootControllerMarker, RebootControllerProxy,
};
use fidl_fuchsia_update_installer_ext::{
    start_update, FetchFailureReason, Initiator, MonitorUpdateAttemptError, Options,
    PrepareFailureReason, State, StateId, UpdateAttemptError,
};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_url::pkg_url::PkgUrl;
use fuchsia_zircon as zx;
use futures::{future::LocalBoxFuture, lock::Mutex as AsyncMutex, prelude::*};
use log::{info, warn};
use omaha_client::{
    app_set::AppSet as _,
    installer::{AppInstallResult, Installer, ProgressObserver},
    protocol::{
        request::InstallSource,
        response::{OmahaStatus, Response},
    },
    request_builder::RequestParams,
};
use std::{rc::Rc, time::Duration};
use thiserror::Error;

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
    FIDL(#[from] fidl::Error),

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
}

#[derive(Debug)]
pub struct FuchsiaInstaller<C = ServiceReconnector<InstallerMarker>> {
    connector: C,
    reboot_controller: Option<RebootControllerProxy>,
    app_set: Rc<AsyncMutex<FuchsiaAppSet>>,
}

impl FuchsiaInstaller<ServiceReconnector<InstallerMarker>> {
    pub fn new(app_set: Rc<AsyncMutex<FuchsiaAppSet>>) -> Self {
        let connector = ServiceReconnector::<InstallerMarker>::new();
        Self { connector, reboot_controller: None, app_set }
    }
}

impl<C: Connect<Proxy = InstallerProxy> + Send> FuchsiaInstaller<C> {
    async fn perform_install_system_update<'a>(
        &'a mut self,
        url: &'a PkgUrl,
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

        let proxy = self.connector.connect().map_err(FuchsiaInstallError::Connect)?;
        let (reboot_controller, reboot_controller_server_end) =
            fidl::endpoints::create_proxy::<RebootControllerMarker>()
                .map_err(FuchsiaInstallError::FIDL)?;

        self.reboot_controller = Some(reboot_controller);

        let mut update_attempt =
            start_update(url, options, &proxy, Some(reboot_controller_server_end)).await?;

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
}

impl<C: Connect<Proxy = InstallerProxy> + Send + Sync> Installer for FuchsiaInstaller<C> {
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
            for url in &install_plan.update_package_urls {
                app_results.push(match url {
                    UpdatePackageUrl::System(url) => self
                        .perform_install_system_update(&url, &install_plan.install_source, observer)
                        .await
                        .into(),
                    UpdatePackageUrl::Package(_) => {
                        if is_system_update {
                            AppInstallResult::Deferred
                        } else {
                            todo!("implement installing packages")
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
        response: &'a Response,
    ) -> LocalBoxFuture<'a, Result<Self::InstallPlan, Self::Error>> {
        async move {
            let system_app_id = self.app_set.lock().await.get_system_app_id().to_owned();
            try_create_install_plan_impl(request_params, response, system_app_id)
        }
        .boxed_local()
    }
}

fn try_create_install_plan_impl(
    request_params: &RequestParams,
    response: &Response,
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

        let urls = match update_check.status {
            OmahaStatus::Ok => {
                if let Some(urls) = &update_check.urls {
                    &urls.url
                } else {
                    return Err(FuchsiaInstallError::Failure(anyhow!("No urls in Omaha response")));
                }
            }
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
        let (url, rest) = if let Some((url, rest)) = urls.split_first() {
            (url, rest)
        } else {
            return Err(FuchsiaInstallError::Failure(anyhow!("No url in Omaha response")));
        };

        if !rest.is_empty() {
            warn!("Only 1 url is supported, found {}", urls.len());
        }

        let manifest = if let Some(manifest) = &update_check.manifest {
            manifest
        } else {
            return Err(FuchsiaInstallError::Failure(anyhow!("No manifest in Omaha response")));
        };

        let (package, rest) = if let Some((package, rest)) = manifest.packages.package.split_first()
        {
            (package, rest)
        } else {
            return Err(FuchsiaInstallError::Failure(anyhow!("No package in Omaha response")));
        };

        if !rest.is_empty() {
            warn!("Only 1 package is supported, found {}", manifest.packages.package.len());
        }

        let full_url = url.codebase.clone() + &package.name;

        let pkg_url = match PkgUrl::parse(&full_url) {
            Ok(pkg_url) => pkg_url,
            Err(err) => {
                return Err(FuchsiaInstallError::Failure(anyhow!(
                    "Failed to parse {} to PkgUrl: {}",
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
        install_source: request_params.source.clone(),
        urgent_update,
    })
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
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

    const TEST_URL: &str = "fuchsia-pkg://fuchsia.com/update/0";
    const TEST_URL_BASE: &str = "fuchsia-pkg://fuchsia.com/";
    const TEST_PACKAGE_NAME: &str = "update/0";

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

    struct MockConnector {
        proxy: Option<InstallerProxy>,
    }

    impl MockConnector {
        fn new(proxy: InstallerProxy) -> Self {
            Self { proxy: Some(proxy) }
        }
        fn failing() -> Self {
            Self { proxy: None }
        }
    }

    impl Connect for MockConnector {
        type Proxy = InstallerProxy;

        fn connect(&self) -> Result<Self::Proxy, anyhow::Error> {
            self.proxy.clone().ok_or(anyhow::anyhow!("no proxy available"))
        }
    }

    fn new_mock_installer() -> (FuchsiaInstaller<MockConnector>, InstallerRequestStream) {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<InstallerMarker>().unwrap();
        let app = omaha_client::common::App::builder("system_id", [1]).build();
        let app_set = Rc::new(AsyncMutex::new(FuchsiaAppSet::new(app)));
        let installer = FuchsiaInstaller {
            connector: MockConnector::new(proxy),
            reboot_controller: None,
            app_set,
        };
        (installer, stream)
    }

    fn new_installer() -> FuchsiaInstaller<ServiceReconnector<InstallerMarker>> {
        let app = omaha_client::common::App::builder("system_id", [1]).build();
        let app_set = Rc::new(AsyncMutex::new(FuchsiaAppSet::new(app)));
        FuchsiaInstaller::new(app_set)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_start_update() {
        let (mut installer, mut stream) = new_mock_installer();
        let plan = FuchsiaInstallPlan {
            update_package_urls: vec![
                UpdatePackageUrl::System(TEST_URL.parse().unwrap()),
                UpdatePackageUrl::Package(TEST_URL.parse().unwrap()),
            ],
            install_source: InstallSource::OnDemand,
            urgent_update: false,
        };
        let observer = MockProgressObserver::new();
        let progresses = observer.progresses();
        let installer_fut = async move {
            let (install_result, app_install_results) =
                installer.perform_install(&plan, Some(&observer)).await;
            assert_eq!(install_result.urgent_update, false);
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
                    assert_eq!(should_write_recovery, true);
                    assert_eq!(allow_attach_to_existing_attempt, true);
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
    async fn test_install_error() {
        let (mut installer, mut stream) = new_mock_installer();
        let plan = FuchsiaInstallPlan {
            update_package_urls: vec![UpdatePackageUrl::System(TEST_URL.parse().unwrap())],
            install_source: InstallSource::OnDemand,
            urgent_update: false,
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
        let (mut installer, mut stream) = new_mock_installer();
        let plan = FuchsiaInstallPlan {
            update_package_urls: vec![UpdatePackageUrl::System(TEST_URL.parse().unwrap())],
            install_source: InstallSource::OnDemand,
            urgent_update: false,
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
        let (mut installer, _) = new_mock_installer();
        installer.connector = MockConnector::failing();
        let plan = FuchsiaInstallPlan {
            update_package_urls: vec![UpdatePackageUrl::System(TEST_URL.parse().unwrap())],
            install_source: InstallSource::OnDemand,
            urgent_update: false,
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
        let mut update_check = UpdateCheck::ok(vec![TEST_URL_BASE.to_string()]);
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

        let install_plan =
            new_installer().try_create_install_plan(&request_params, &response).await.unwrap();
        assert_eq!(
            install_plan.update_package_urls,
            vec![UpdatePackageUrl::System(TEST_URL.parse().unwrap())],
        );
        assert_eq!(install_plan.install_source, request_params.source);
        assert_eq!(install_plan.urgent_update, false);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_app() {
        let request_params = RequestParams::default();
        let response = Response::default();

        assert_matches!(
            new_installer().try_create_install_plan(&request_params, &response).await,
            Err(FuchsiaInstallError::Failure(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_app() {
        let request_params = RequestParams::default();

        let system_app = App {
            update_check: Some(UpdateCheck {
                manifest: Some(Manifest {
                    packages: Packages::new(vec![Package::with_name(TEST_PACKAGE_NAME)]),
                    ..Manifest::default()
                }),
                ..UpdateCheck::ok(vec![TEST_URL_BASE.to_string()])
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

        let install_plan =
            new_installer().try_create_install_plan(&request_params, &response).await.unwrap();
        assert_eq!(
            install_plan.update_package_urls,
            vec![UpdatePackageUrl::System(TEST_URL.parse().unwrap())],
        );
        assert_eq!(install_plan.install_source, request_params.source);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_package_updates() {
        let request_params = RequestParams::default();

        let system_app = App {
            update_check: Some(UpdateCheck::no_update()),
            id: "system_id".into(),
            ..App::default()
        };
        let package1_app = App {
            update_check: Some(UpdateCheck {
                manifest: Some(Manifest {
                    packages: Packages::new(vec![Package::with_name("package1")]),
                    ..Manifest::default()
                }),
                ..UpdateCheck::ok(vec![TEST_URL_BASE.to_string()])
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
                    packages: Packages::new(vec![Package::with_name("package3")]),
                    ..Manifest::default()
                }),
                ..UpdateCheck::ok(vec![TEST_URL_BASE.to_string()])
            }),
            id: "package3_id".into(),
            ..App::default()
        };
        let response = Response {
            apps: vec![system_app, package1_app, package2_app, package3_app],
            ..Response::default()
        };

        let install_plan =
            new_installer().try_create_install_plan(&request_params, &response).await.unwrap();
        assert_eq!(
            install_plan.update_package_urls,
            vec![
                UpdatePackageUrl::Package(format!("{TEST_URL_BASE}package1").parse().unwrap()),
                UpdatePackageUrl::Package(format!("{TEST_URL_BASE}package3").parse().unwrap())
            ]
        );
        assert_eq!(install_plan.install_source, request_params.source);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_mixed_update() {
        let request_params = RequestParams::default();
        let system_app = App {
            update_check: Some(UpdateCheck {
                manifest: Some(Manifest {
                    packages: Packages::new(vec![Package::with_name(TEST_PACKAGE_NAME)]),
                    ..Manifest::default()
                }),
                ..UpdateCheck::ok(vec![TEST_URL_BASE.to_string()])
            }),
            id: "system_id".into(),
            ..App::default()
        };
        let package_app = App {
            update_check: Some(UpdateCheck {
                manifest: Some(Manifest {
                    packages: Packages::new(vec![Package::with_name("some-package")]),
                    ..Manifest::default()
                }),
                ..UpdateCheck::ok(vec![TEST_URL_BASE.to_string()])
            }),
            id: "package_id".into(),
            ..App::default()
        };
        let response = Response { apps: vec![package_app, system_app], ..Response::default() };

        let install_plan =
            new_installer().try_create_install_plan(&request_params, &response).await.unwrap();
        assert_eq!(
            install_plan.update_package_urls,
            vec![
                UpdatePackageUrl::Package(format!("{TEST_URL_BASE}some-package").parse().unwrap()),
                UpdatePackageUrl::System(TEST_URL.parse().unwrap())
            ],
        );
        assert_eq!(install_plan.install_source, request_params.source);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_update_check() {
        let request_params = RequestParams::default();
        let response = Response {
            apps: vec![App { id: "system_id".into(), ..App::default() }],
            ..Response::default()
        };

        assert_matches!(
            new_installer().try_create_install_plan(&request_params, &response).await,
            Err(FuchsiaInstallError::Failure(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_urls() {
        let request_params = RequestParams::default();
        let response = Response {
            apps: vec![App {
                update_check: Some(UpdateCheck::default()),
                id: "system_id".into(),
                ..App::default()
            }],
            ..Response::default()
        };

        assert_matches!(
            new_installer().try_create_install_plan(&request_params, &response).await,
            Err(FuchsiaInstallError::Failure(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_app_error_status() {
        let request_params = RequestParams::default();
        let response = Response {
            apps: vec![App {
                status: OmahaStatus::Error("error-unknownApplication".to_string()),
                ..App::default()
            }],
            ..Response::default()
        };

        assert_matches!(
            new_installer().try_create_install_plan(&request_params, &response).await,
            Err(FuchsiaInstallError::Failure(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_update() {
        let request_params = RequestParams::default();
        let response = Response {
            apps: vec![App { update_check: Some(UpdateCheck::no_update()), ..App::default() }],
            ..Response::default()
        };

        assert_matches!(
            new_installer().try_create_install_plan(&request_params, &response).await,
            Err(FuchsiaInstallError::Failure(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_invalid_url() {
        let request_params = RequestParams::default();
        let response = Response {
            apps: vec![App {
                update_check: Some(UpdateCheck::ok(vec!["invalid-url".to_string()])),
                id: "system_id".into(),
                ..App::default()
            }],
            ..Response::default()
        };

        assert_matches!(
            new_installer().try_create_install_plan(&request_params, &response).await,
            Err(FuchsiaInstallError::Failure(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_manifest() {
        let request_params = RequestParams::default();
        let response = Response {
            apps: vec![App {
                update_check: Some(UpdateCheck::ok(vec![TEST_URL_BASE.to_string()])),
                id: "system_id".into(),
                ..App::default()
            }],
            ..Response::default()
        };

        assert_matches!(
            new_installer().try_create_install_plan(&request_params, &response).await,
            Err(FuchsiaInstallError::Failure(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_urgent_update_attribute_true() {
        let request_params = RequestParams::default();
        let mut update_check = UpdateCheck::ok(vec![TEST_URL_BASE.to_string()]);
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

        let install_plan =
            new_installer().try_create_install_plan(&request_params, &response).await.unwrap();

        assert_eq!(install_plan.urgent_update, true);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_urgent_update_attribute_false() {
        let request_params = RequestParams::default();
        let mut update_check = UpdateCheck::ok(vec![TEST_URL_BASE.to_string()]);
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

        let install_plan =
            new_installer().try_create_install_plan(&request_params, &response).await.unwrap();

        assert_eq!(install_plan.urgent_update, false);
    }
}
