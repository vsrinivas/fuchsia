// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{errors::Error, update_manager::TargetChannelUpdater, DEFAULT_UPDATE_PACKAGE_URL};
use anyhow::{anyhow, Context as _};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_update_ext::Initiator;
use fidl_fuchsia_update_installer::{
    InstallerMarker, InstallerProxy, RebootControllerMarker, RebootControllerProxy,
};
use fidl_fuchsia_update_installer_ext::{
    self as installer, start_update, MonitorUpdateAttemptError, Options, State, StateId,
    UpdateAttemptError,
};
use fuchsia_component::client::connect_to_protocol;
use fuchsia_url::AbsolutePackageUrl;
use futures::{future::BoxFuture, prelude::*, stream::BoxStream};
use tracing::info;

// On success, system will reboot before this function returns
pub async fn apply_system_update(
    initiator: Initiator,
    target_channel_updater: &dyn TargetChannelUpdater,
) -> Result<BoxStream<'_, Result<ApplyState, (ApplyProgress, anyhow::Error)>>, anyhow::Error> {
    let installer_proxy =
        connect_to_protocol::<InstallerMarker>().context("connecting to component Installer")?;
    let mut update_installer = RealUpdateInstaller { installer_proxy };

    apply_system_update_impl(
        DEFAULT_UPDATE_PACKAGE_URL,
        &mut update_installer,
        initiator,
        target_channel_updater,
    )
    .await
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct ApplyProgress {
    pub download_size: Option<u64>,
    pub fraction_completed: Option<f32>,
}

impl ApplyProgress {
    #[cfg(test)]
    pub fn new(download_size: u64, fraction_completed: f32) -> Self {
        ApplyProgress {
            download_size: Some(download_size),
            fraction_completed: Some(fraction_completed),
        }
    }

    pub fn none() -> Self {
        ApplyProgress::default()
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum ApplyState {
    InstallingUpdate(ApplyProgress),
    WaitingForReboot(ApplyProgress),
}

// For mocking
trait UpdateInstaller {
    type UpdateAttempt: Stream<Item = Result<State, MonitorUpdateAttemptError>>
        + Unpin
        + Send
        + 'static;

    fn start_update(
        &mut self,
        update_url: AbsolutePackageUrl,
        options: Options,
        reboot_controller_server_end: Option<ServerEnd<RebootControllerMarker>>,
    ) -> BoxFuture<'_, Result<Self::UpdateAttempt, UpdateAttemptError>>;
}

struct RealUpdateInstaller {
    installer_proxy: InstallerProxy,
}

impl UpdateInstaller for RealUpdateInstaller {
    type UpdateAttempt = installer::UpdateAttempt;

    fn start_update(
        &mut self,
        update_url: AbsolutePackageUrl,
        options: Options,
        reboot_controller_server_end: Option<ServerEnd<RebootControllerMarker>>,
    ) -> BoxFuture<'_, Result<Self::UpdateAttempt, UpdateAttemptError>> {
        async move {
            start_update(&update_url, options, &self.installer_proxy, reboot_controller_server_end)
                .await
        }
        .boxed()
    }
}

async fn apply_system_update_impl(
    default_update_url: &str,
    update_installer: &mut impl UpdateInstaller,
    initiator: Initiator,
    target_channel_updater: &dyn TargetChannelUpdater,
) -> Result<BoxStream<'static, Result<ApplyState, (ApplyProgress, anyhow::Error)>>, anyhow::Error> {
    info!("starting system updater");
    let options = Options {
        initiator: match initiator {
            Initiator::Service => installer::Initiator::Service,
            Initiator::User => installer::Initiator::User,
        },
        should_write_recovery: true,
        allow_attach_to_existing_attempt: true,
    };
    let update_url = AbsolutePackageUrl::parse(
        &target_channel_updater
            .get_target_channel_update_url()
            .unwrap_or_else(|| default_update_url.to_owned()),
    )?;
    let (reboot_controller, reboot_controller_server_end) =
        fidl::endpoints::create_proxy::<RebootControllerMarker>()
            .context("creating reboot controller")?;

    let update_attempt = update_installer
        .start_update(update_url, options, Some(reboot_controller_server_end))
        .await
        .context(Error::SystemUpdaterFailed)?;

    Ok(async_generator::generate(|mut co| async move {
        monitor_update_progress(update_attempt, reboot_controller, &mut co).await
    })
    .into_try_stream()
    .boxed())
}

async fn monitor_update_progress(
    mut update_attempt: impl Stream<Item = Result<State, MonitorUpdateAttemptError>> + Unpin,
    reboot_controller: RebootControllerProxy,
    co: &mut async_generator::Yield<ApplyState>,
) -> Result<(), (ApplyProgress, anyhow::Error)> {
    let mut apply_progress = ApplyProgress::none();
    while let Some(state) = update_attempt
        .try_next()
        .await
        .context(Error::SystemUpdaterFailed)
        .map_err(|e| (apply_progress.clone(), e))?
    {
        info!("Installer entered state: {}", state.name());
        apply_progress.download_size = state.download_size();
        if let Some(progress) = state.progress() {
            apply_progress.fraction_completed = Some(progress.fraction_completed());
        }
        if state.is_failure() {
            return Err((apply_progress, anyhow!(Error::SystemUpdaterFailed)));
        }
        if state.id() == StateId::WaitToReboot {
            co.yield_(ApplyState::WaitingForReboot(apply_progress.clone())).await;

            info!("Successful update, rebooting...");

            reboot_controller
                .unblock()
                .map_err(Error::RebootFailed)
                .context("notify installer it can reboot when ready")
                .map_err(|e| (apply_progress, e))?;
            // On success, wait for reboot to happen.

            info!("Reboot contoller unblocked, waiting for reboot");
            let () = future::pending().await;
            unreachable!();
        }
        co.yield_(ApplyState::InstallingUpdate(apply_progress.clone())).await;
        if state.is_success() {
            return Ok(());
        }
    }
    Err((apply_progress, anyhow!(Error::InstallationEndedUnexpectedly)))
}

#[cfg(test)]
mod test_apply_system_update_impl {
    use super::*;
    use crate::update_manager::tests::FakeTargetChannelUpdater;
    use assert_matches::assert_matches;
    use fidl_fuchsia_update_installer::RebootControllerRequest;
    use fidl_fuchsia_update_installer_ext::{
        PrepareFailureReason, Progress, UpdateInfo, UpdateInfoAndProgress,
    };
    use fuchsia_async as fasync;
    use proptest::prelude::*;

    const TEST_DEFAULT_UPDATE_URL: &str = "fuchsia-pkg://fuchsia.test/update";

    struct DoNothingUpdateInstaller;
    impl UpdateInstaller for DoNothingUpdateInstaller {
        type UpdateAttempt =
            futures::stream::Once<future::Ready<Result<State, MonitorUpdateAttemptError>>>;

        fn start_update(
            &mut self,
            _update_url: AbsolutePackageUrl,
            _options: Options,
            _reboot_controller_server_end: Option<ServerEnd<RebootControllerMarker>>,
        ) -> BoxFuture<'_, Result<Self::UpdateAttempt, UpdateAttemptError>> {
            let info = UpdateInfo::builder().download_size(0).build();
            let state = State::Complete(UpdateInfoAndProgress::done(info));
            future::ok(futures::stream::once(future::ok(state))).boxed()
        }
    }

    struct WasCalledUpdateInstaller {
        was_called: bool,
    }
    impl UpdateInstaller for WasCalledUpdateInstaller {
        type UpdateAttempt =
            futures::stream::Once<future::Ready<Result<State, MonitorUpdateAttemptError>>>;

        fn start_update(
            &mut self,
            _update_url: AbsolutePackageUrl,
            _options: Options,
            _reboot_controller_server_end: Option<ServerEnd<RebootControllerMarker>>,
        ) -> BoxFuture<'_, Result<Self::UpdateAttempt, UpdateAttemptError>> {
            self.was_called = true;
            let info = UpdateInfo::builder().download_size(0).build();
            let state = State::Complete(UpdateInfoAndProgress::done(info));
            future::ok(futures::stream::once(future::ok(state))).boxed()
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_call_installer() {
        let mut update_installer = WasCalledUpdateInstaller { was_called: false };

        apply_system_update_impl(
            TEST_DEFAULT_UPDATE_URL,
            &mut update_installer,
            Initiator::User,
            &FakeTargetChannelUpdater::new(),
        )
        .await
        .unwrap();
        assert!(update_installer.was_called);
    }

    #[derive(Default)]
    struct ArgumentCapturingUpdateInstaller {
        update_url: Option<AbsolutePackageUrl>,
        options: Option<Options>,
        reboot_controller_server_end: Option<Option<ServerEnd<RebootControllerMarker>>>,
        state: Option<State>,
    }
    impl UpdateInstaller for ArgumentCapturingUpdateInstaller {
        type UpdateAttempt =
            futures::stream::Once<future::Ready<Result<State, MonitorUpdateAttemptError>>>;

        fn start_update(
            &mut self,
            update_url: AbsolutePackageUrl,
            options: Options,
            reboot_controller_server_end: Option<ServerEnd<RebootControllerMarker>>,
        ) -> BoxFuture<'_, Result<Self::UpdateAttempt, UpdateAttemptError>> {
            self.update_url = Some(update_url);
            self.options = Some(options);
            self.reboot_controller_server_end = Some(reboot_controller_server_end);
            let state = self.state.clone().unwrap_or(State::Complete(UpdateInfoAndProgress::done(
                UpdateInfo::builder().download_size(0).build(),
            )));
            future::ok(futures::stream::once(future::ok(state))).boxed()
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_call_install_with_right_arguments() {
        let mut update_installer = ArgumentCapturingUpdateInstaller::default();

        apply_system_update_impl(
            TEST_DEFAULT_UPDATE_URL,
            &mut update_installer,
            Initiator::User,
            &FakeTargetChannelUpdater::new(),
        )
        .await
        .unwrap();

        assert_eq!(
            update_installer.update_url,
            Some(AbsolutePackageUrl::parse(TEST_DEFAULT_UPDATE_URL).unwrap())
        );
        assert_eq!(
            update_installer.options,
            Some(Options {
                initiator: installer::Initiator::User,
                should_write_recovery: true,
                allow_attach_to_existing_attempt: true,
            })
        );
        assert_matches!(update_installer.reboot_controller_server_end, Some(Some(_)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_call_install_with_right_arguments_and_target_channel() {
        let mut update_installer = ArgumentCapturingUpdateInstaller::default();

        let target_url = "fuchsia-pkg://fuchsia.test/my-update";
        apply_system_update_impl(
            TEST_DEFAULT_UPDATE_URL,
            &mut update_installer,
            Initiator::User,
            &FakeTargetChannelUpdater::new_with_update_url(target_url),
        )
        .await
        .unwrap();

        assert_eq!(
            update_installer.update_url,
            Some(AbsolutePackageUrl::parse(target_url).unwrap())
        );
        assert_eq!(
            update_installer.options,
            Some(Options {
                initiator: installer::Initiator::User,
                should_write_recovery: true,
                allow_attach_to_existing_attempt: true,
            })
        );
        assert_matches!(update_installer.reboot_controller_server_end, Some(Some(_)));
    }

    // Test that if system updater succeeds, system-update-checker calls the reboot service.
    #[fasync::run_singlethreaded(test)]
    async fn test_reboot_on_success() {
        let info = UpdateInfo::builder().download_size(0).build();
        let state = State::WaitToReboot(UpdateInfoAndProgress::done(info));
        let mut update_installer =
            ArgumentCapturingUpdateInstaller { state: Some(state), ..Default::default() };
        let mut stream = apply_system_update_impl(
            TEST_DEFAULT_UPDATE_URL,
            &mut update_installer,
            Initiator::User,
            &FakeTargetChannelUpdater::new(),
        )
        .await
        .unwrap();
        assert_matches!(stream.next().await, Some(Ok(ApplyState::WaitingForReboot(_))));
        assert_matches!(stream.next().now_or_never(), None);

        let mut reboot_stream =
            update_installer.reboot_controller_server_end.unwrap().unwrap().into_stream().unwrap();
        assert_matches!(
            reboot_stream.next().await,
            Some(Ok(RebootControllerRequest::Unblock { .. }))
        );
    }

    // An update installer which fails every call made to it.
    // Useful for making the "run system updater" step fail.
    #[derive(Default)]
    struct FailingUpdateInstaller {}
    impl UpdateInstaller for FailingUpdateInstaller {
        type UpdateAttempt =
            futures::stream::Once<future::Ready<Result<State, MonitorUpdateAttemptError>>>;

        fn start_update(
            &mut self,
            _update_url: AbsolutePackageUrl,
            _options: Options,
            reboot_controller_server_end: Option<ServerEnd<RebootControllerMarker>>,
        ) -> BoxFuture<'_, Result<Self::UpdateAttempt, UpdateAttemptError>> {
            let mut stream = reboot_controller_server_end.unwrap().into_stream().unwrap();
            // Assert that reboot controller isn't dropped or called.
            assert_matches!(stream.next().now_or_never(), None);
            future::ok(futures::stream::once(future::err(MonitorUpdateAttemptError::FIDL(
                fidl::Error::Invalid,
            ))))
            .boxed()
        }
    }

    // Test that if system updater fails, we don't reboot the system.
    #[fasync::run_singlethreaded(test)]
    async fn test_does_not_reboot_on_failure() {
        let mut update_installer = FailingUpdateInstaller::default();
        let (_, error) = apply_system_update_impl(
            TEST_DEFAULT_UPDATE_URL,
            &mut update_installer,
            Initiator::User,
            &FakeTargetChannelUpdater::new(),
        )
        .await
        .unwrap()
        .next()
        .await
        .unwrap()
        .unwrap_err();
        assert_matches!(error.downcast::<Error>().unwrap(), Error::SystemUpdaterFailed);
    }

    struct RebootUpdateInstaller;
    impl UpdateInstaller for RebootUpdateInstaller {
        type UpdateAttempt =
            futures::stream::Once<future::Ready<Result<State, MonitorUpdateAttemptError>>>;

        fn start_update(
            &mut self,
            _update_url: AbsolutePackageUrl,
            _options: Options,
            _reboot_controller_server_end: Option<ServerEnd<RebootControllerMarker>>,
        ) -> BoxFuture<'_, Result<Self::UpdateAttempt, UpdateAttemptError>> {
            let info = UpdateInfo::builder().download_size(0).build();
            let state = State::WaitToReboot(UpdateInfoAndProgress::done(info));
            future::ok(futures::stream::once(future::ok(state))).boxed()
        }
    }

    // Test that if the reboot controller isn't working, we surface the appropriate error after
    // updating. This would be a bad state to be in, but at least a user would get output.
    #[fasync::run_singlethreaded(test)]
    async fn test_reboot_errors_on_no_service() {
        let mut update_installer = RebootUpdateInstaller;

        let mut results: Vec<_> = apply_system_update_impl(
            TEST_DEFAULT_UPDATE_URL,
            &mut update_installer,
            Initiator::User,
            &FakeTargetChannelUpdater::new(),
        )
        .await
        .unwrap()
        .collect()
        .await;

        assert_eq!(results.len(), 2);
        // We should have errored out on calling reboot_controller.
        assert_matches!(
            results
                .remove(1)
                .err()
                .expect("system update should fail")
                .1
                .downcast::<Error>()
                .unwrap(),
            Error::RebootFailed(_)
        );
    }

    proptest! {
        #[test]
        fn test_options_passed_to_installer(initiator: Initiator) {
            let mut update_installer = ArgumentCapturingUpdateInstaller::default();

            let mut executor =
                fasync::TestExecutor::new().expect("create executor in test");
            executor.run_singlethreaded(async move{
                let result = apply_system_update_impl(
                    TEST_DEFAULT_UPDATE_URL,
                    &mut update_installer,
                    initiator,
                    &FakeTargetChannelUpdater::new(),
                ).await;

                prop_assert!(result.is_ok(), "apply_system_update_impl failed: {:?}", result.err());
                prop_assert_eq!(
                    update_installer.options,
                    Some(Options {
                        initiator:  match initiator {
                            Initiator::Service => installer::Initiator::Service,
                            Initiator::User => installer::Initiator::User,
                        },
                        should_write_recovery: true,
                        allow_attach_to_existing_attempt: true,
                    })
                );
                Ok(())}
            ).unwrap();
        }
    }

    struct ProgressUpdateInstaller {
        states: Vec<State>,
        // TODO(fxbug.dev/69496): Remove this or explain why it's here.
        #[allow(dead_code)]
        reboot_controller_server_end: Option<ServerEnd<RebootControllerMarker>>,
    }
    impl ProgressUpdateInstaller {
        fn new(states: Vec<State>) -> Self {
            Self { states, reboot_controller_server_end: None }
        }
    }
    impl UpdateInstaller for ProgressUpdateInstaller {
        type UpdateAttempt =
            futures::stream::Iter<std::vec::IntoIter<Result<State, MonitorUpdateAttemptError>>>;

        fn start_update(
            &mut self,
            _update_url: AbsolutePackageUrl,
            _options: Options,
            reboot_controller_server_end: Option<ServerEnd<RebootControllerMarker>>,
        ) -> BoxFuture<'_, Result<Self::UpdateAttempt, UpdateAttemptError>> {
            self.reboot_controller_server_end = reboot_controller_server_end;
            let results: Vec<_> = self.states.clone().into_iter().map(Ok).collect();
            future::ok(futures::stream::iter(results)).boxed()
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_yield_progress_event() {
        let info = UpdateInfo::builder().download_size(1000).build();
        let mut update_installer = ProgressUpdateInstaller::new(vec![
            State::Prepare,
            State::Fetch(UpdateInfoAndProgress::new(info, Progress::none()).unwrap()),
            State::Stage(
                UpdateInfoAndProgress::new(
                    info,
                    Progress::builder().fraction_completed(0.5).bytes_downloaded(500).build(),
                )
                .unwrap(),
            ),
            State::Stage(
                UpdateInfoAndProgress::new(
                    info,
                    Progress::builder().fraction_completed(0.7).bytes_downloaded(1000).build(),
                )
                .unwrap(),
            ),
            State::WaitToReboot(UpdateInfoAndProgress::done(info)),
        ]);

        let mut stream = apply_system_update_impl(
            TEST_DEFAULT_UPDATE_URL,
            &mut update_installer,
            Initiator::User,
            &FakeTargetChannelUpdater::new(),
        )
        .await
        .unwrap();

        for state in &[
            ApplyState::InstallingUpdate(ApplyProgress::none()),
            ApplyState::InstallingUpdate(ApplyProgress::new(1000, 0.0)),
            ApplyState::InstallingUpdate(ApplyProgress::new(1000, 0.5)),
            ApplyState::InstallingUpdate(ApplyProgress::new(1000, 0.7)),
            ApplyState::WaitingForReboot(ApplyProgress::new(1000, 1.0)),
        ] {
            assert_eq!(&stream.next().await.unwrap().unwrap(), state);
        }
        assert_matches!(stream.next().now_or_never(), None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_installer_complete_state() {
        let info = UpdateInfo::builder().download_size(1000).build();
        let mut update_installer = ProgressUpdateInstaller::new(vec![
            State::Prepare,
            State::Fetch(UpdateInfoAndProgress::new(info, Progress::none()).unwrap()),
            State::Stage(
                UpdateInfoAndProgress::new(
                    info,
                    Progress::builder().fraction_completed(0.5).bytes_downloaded(500).build(),
                )
                .unwrap(),
            ),
            State::Complete(UpdateInfoAndProgress::done(info)),
        ]);

        let mut stream = apply_system_update_impl(
            TEST_DEFAULT_UPDATE_URL,
            &mut update_installer,
            Initiator::User,
            &FakeTargetChannelUpdater::new(),
        )
        .await
        .unwrap();

        for state in &[
            ApplyState::InstallingUpdate(ApplyProgress::none()),
            ApplyState::InstallingUpdate(ApplyProgress::new(1000, 0.0)),
            ApplyState::InstallingUpdate(ApplyProgress::new(1000, 0.5)),
            ApplyState::InstallingUpdate(ApplyProgress::new(1000, 1.0)),
        ] {
            assert_eq!(&stream.next().await.unwrap().unwrap(), state);
        }
        assert_matches!(stream.next().await, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_installer_failure_event() {
        let mut update_installer = ProgressUpdateInstaller::new(vec![
            State::Prepare,
            State::FailPrepare(PrepareFailureReason::Internal),
        ]);

        let mut stream = apply_system_update_impl(
            TEST_DEFAULT_UPDATE_URL,
            &mut update_installer,
            Initiator::User,
            &FakeTargetChannelUpdater::new(),
        )
        .await
        .unwrap();

        assert_matches!(
            stream.next().await,
            Some(Ok(ApplyState::InstallingUpdate(ApplyProgress {
                download_size: None,
                fraction_completed: None
            })))
        );
        assert_matches!(
            stream.next().await,
            Some(Err((ApplyProgress { download_size: None, fraction_completed: None }, _)))
        );
    }
}
