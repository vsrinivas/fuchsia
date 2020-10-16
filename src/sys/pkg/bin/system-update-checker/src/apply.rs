// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::Error;
use anyhow::{anyhow, Context as _};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_update_ext::Initiator;
use fidl_fuchsia_update_installer::{InstallerMarker, InstallerProxy, RebootControllerMarker};
use fidl_fuchsia_update_installer_ext::{
    self as installer, start_update, MonitorUpdateAttemptError, Options, State, StateId,
    UpdateAttemptError,
};
use fuchsia_component::client::connect_to_service;
use fuchsia_syslog::fx_log_info;
use fuchsia_url::pkg_url::PkgUrl;
use futures::{future::BoxFuture, prelude::*, FutureExt};

const UPDATE_URL: &str = "fuchsia-pkg://fuchsia.com/update";

// On success, system may reboot before this function returns
pub async fn apply_system_update(initiator: Initiator) -> Result<(), anyhow::Error> {
    let installer_proxy =
        connect_to_service::<InstallerMarker>().context("connecting to component Installer")?;
    let mut update_installer = RealUpdateInstaller { installer_proxy };

    apply_system_update_and_reboot(&mut update_installer, initiator).await
}

async fn apply_system_update_and_reboot<'a>(
    update_installer: &'a mut impl UpdateInstaller,
    initiator: Initiator,
) -> Result<(), anyhow::Error> {
    let (reboot_controller, reboot_controller_server_end) =
        fidl::endpoints::create_proxy::<RebootControllerMarker>()
            .context("creating reboot controller")?;

    apply_system_update_impl(update_installer, initiator, Some(reboot_controller_server_end))
        .await?;

    fx_log_info!("Successful update, rebooting...");

    reboot_controller
        .unblock()
        .map_err(|e| Error::RebootFailed(e))
        .context("notify installer it can reboot when ready")
}

// For mocking
trait UpdateInstaller {
    type UpdateAttempt: Stream<Item = Result<State, MonitorUpdateAttemptError>> + Unpin;

    fn start_update(
        &mut self,
        update_url: PkgUrl,
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
        update_url: PkgUrl,
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

async fn apply_system_update_impl<'a>(
    update_installer: &'a mut impl UpdateInstaller,
    initiator: Initiator,
    reboot_controller_server_end: Option<ServerEnd<RebootControllerMarker>>,
) -> Result<(), anyhow::Error> {
    fx_log_info!("starting system updater");
    let options = Options {
        initiator: match initiator {
            Initiator::Service => installer::Initiator::Service,
            Initiator::User => installer::Initiator::User,
        },
        should_write_recovery: true,
        allow_attach_to_existing_attempt: true,
    };
    let update_url = PkgUrl::parse(UPDATE_URL)?;
    let mut update_attempt = update_installer
        .start_update(update_url, options, reboot_controller_server_end)
        .await
        .context(Error::SystemUpdaterFailed)?;
    while let Some(state) = update_attempt.try_next().await.context(Error::SystemUpdaterFailed)? {
        fx_log_info!("Installer entered state: {}", state.name());
        if state.id() == StateId::WaitToReboot || state.is_success() {
            return Ok(());
        } else if state.is_failure() {
            return Err(anyhow!(Error::SystemUpdaterFailed));
        }
    }
    Err(anyhow!(Error::InstallationEndedUnexpectedly))
}

#[cfg(test)]
mod test_apply_system_update_impl {
    use super::*;
    use fidl_fuchsia_update_installer::RebootControllerRequest;
    use fidl_fuchsia_update_installer_ext::{UpdateInfo, UpdateInfoAndProgress};
    use fuchsia_async as fasync;
    use matches::assert_matches;
    use proptest::prelude::*;

    struct DoNothingUpdateInstaller;
    impl UpdateInstaller for DoNothingUpdateInstaller {
        type UpdateAttempt =
            futures::stream::Once<future::Ready<Result<State, MonitorUpdateAttemptError>>>;

        fn start_update(
            &mut self,
            _update_url: PkgUrl,
            _options: Options,
            _reboot_controller_server_end: Option<ServerEnd<RebootControllerMarker>>,
        ) -> BoxFuture<'_, Result<Self::UpdateAttempt, UpdateAttemptError>> {
            let info = UpdateInfo::builder().download_size(0).build();
            let state = State::WaitToReboot(UpdateInfoAndProgress::done(info));
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
            _update_url: PkgUrl,
            _options: Options,
            _reboot_controller_server_end: Option<ServerEnd<RebootControllerMarker>>,
        ) -> BoxFuture<'_, Result<Self::UpdateAttempt, UpdateAttemptError>> {
            self.was_called = true;
            let info = UpdateInfo::builder().download_size(0).build();
            let state = State::WaitToReboot(UpdateInfoAndProgress::done(info));
            future::ok(futures::stream::once(future::ok(state))).boxed()
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_call_installer() {
        let mut update_installer = WasCalledUpdateInstaller { was_called: false };

        apply_system_update_impl(&mut update_installer, Initiator::User, None).await.unwrap();

        assert!(update_installer.was_called);
    }

    #[derive(Default)]
    struct ArgumentCapturingUpdateInstaller {
        update_url: Option<PkgUrl>,
        options: Option<Options>,
        reboot_controller_server_end: Option<Option<ServerEnd<RebootControllerMarker>>>,
    }
    impl UpdateInstaller for ArgumentCapturingUpdateInstaller {
        type UpdateAttempt =
            futures::stream::Once<future::Ready<Result<State, MonitorUpdateAttemptError>>>;

        fn start_update(
            &mut self,
            update_url: PkgUrl,
            options: Options,
            reboot_controller_server_end: Option<ServerEnd<RebootControllerMarker>>,
        ) -> BoxFuture<'_, Result<Self::UpdateAttempt, UpdateAttemptError>> {
            self.update_url = Some(update_url);
            self.options = Some(options);
            self.reboot_controller_server_end = Some(reboot_controller_server_end);
            let info = UpdateInfo::builder().download_size(0).build();
            let state = State::WaitToReboot(UpdateInfoAndProgress::done(info));
            future::ok(futures::stream::once(future::ok(state))).boxed()
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_call_install_with_right_arguments() {
        let mut update_installer = ArgumentCapturingUpdateInstaller::default();

        let (_, reboot_controller_server_end) =
            fidl::endpoints::create_proxy::<RebootControllerMarker>()
                .expect("creating reboot controller");
        apply_system_update_impl(
            &mut update_installer,
            Initiator::User,
            Some(reboot_controller_server_end),
        )
        .await
        .unwrap();

        assert_eq!(update_installer.update_url, Some(PkgUrl::parse(UPDATE_URL).unwrap()));
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
        let mut update_installer = ArgumentCapturingUpdateInstaller::default();

        apply_system_update_and_reboot(&mut update_installer, Initiator::User).await.unwrap();

        let mut stream =
            update_installer.reboot_controller_server_end.unwrap().unwrap().into_stream().unwrap();
        assert_matches!(
            stream.try_next().await.unwrap(),
            Some(RebootControllerRequest::Unblock { .. })
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
            _update_url: PkgUrl,
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
        let update_result =
            apply_system_update_and_reboot(&mut update_installer, Initiator::User).await;

        assert_matches!(
            update_result.unwrap_err().downcast::<Error>().unwrap(),
            Error::SystemUpdaterFailed
        );
    }

    // Test that if the reboot controller isn't working, we surface the appropriate error after
    // updating. This would be a bad state to be in, but at least a user would get output.
    #[fasync::run_singlethreaded(test)]
    async fn test_reboot_errors_on_no_service() {
        let mut update_installer = DoNothingUpdateInstaller;

        let update_result =
            apply_system_update_and_reboot(&mut update_installer, Initiator::User).await;

        // We should have errored out on calling reboot_controller.
        assert_matches!(
            update_result.err().expect("system update should fail").downcast::<Error>().unwrap(),
            Error::RebootFailed(_)
        );
    }

    proptest! {
        #[test]
        fn test_options_passed_to_installer(
            initiator: Initiator)
        {
            let mut update_installer = ArgumentCapturingUpdateInstaller::default();

            let mut executor =
                fasync::Executor::new().expect("create executor in test");
                let (_, reboot_controller_server_end) =
                fidl::endpoints::create_proxy::<RebootControllerMarker>()
                .expect("creating reboot controller");
            let result = executor.run_singlethreaded(apply_system_update_impl(
                &mut update_installer,
                initiator,Some(reboot_controller_server_end)
            ));

            prop_assert!(result.is_ok(), "apply_system_update_impl failed: {:?}", result);
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
        }
    }
}
