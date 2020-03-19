// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is a temporary Installer implementation that launches system updater directly, we will
//! delete this and switch to the one in mod installer once the FIDL API is ready.

use crate::install_plan::FuchsiaInstallPlan;
use anyhow::anyhow;
use fidl_fuchsia_sys::LauncherProxy;
use fuchsia_async as fasync;
use fuchsia_component::client::{connect_to_service, launcher, AppBuilder, ExitStatus};
use fuchsia_zircon::{self as zx, Duration};
use futures::future::{select, BoxFuture, Either};
use futures::prelude::*;
use log::info;
use omaha_client::{
    installer::{Installer, ProgressObserver},
    protocol::request::InstallSource,
};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum FuchsiaInstallError {
    #[error("generic error: {}", _0)]
    Failure(anyhow::Error),

    #[error("System update installer failed: {}", _0)]
    Installer(ExitStatus),
}

impl From<anyhow::Error> for FuchsiaInstallError {
    fn from(e: anyhow::Error) -> FuchsiaInstallError {
        FuchsiaInstallError::Failure(e)
    }
}

#[derive(Debug)]
pub struct FuchsiaInstaller {
    launcher: LauncherProxy,
}

impl FuchsiaInstaller {
    pub fn new() -> Result<Self, FuchsiaInstallError> {
        Ok(FuchsiaInstaller { launcher: launcher()? })
    }

    #[cfg(test)]
    fn new_mock() -> (Self, fidl_fuchsia_sys::LauncherRequestStream) {
        let (launcher, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_sys::LauncherMarker>().unwrap();
        (FuchsiaInstaller { launcher }, stream)
    }
}

impl Installer for FuchsiaInstaller {
    type InstallPlan = FuchsiaInstallPlan;
    type Error = FuchsiaInstallError;

    fn perform_install<'a>(
        &'a mut self,
        install_plan: &FuchsiaInstallPlan,
        observer: Option<&'a dyn ProgressObserver>,
    ) -> BoxFuture<'a, Result<(), FuchsiaInstallError>> {
        let url = install_plan.url.to_string();
        let initiator = match install_plan.install_source {
            InstallSource::ScheduledTask => "automatic",
            InstallSource::OnDemand => "manual",
        };
        info!("starting system_updater");
        let system_updater = async move {
            AppBuilder::new("fuchsia-pkg://fuchsia.com/amber#meta/system_updater.cmx")
                .arg("-initiator")
                .arg(initiator)
                .arg("-update")
                .arg(url)
                .arg("-reboot=false")
                .status(&self.launcher)?
                .await?
                .ok()
                .map_err(FuchsiaInstallError::Installer)
        }
        .boxed();
        let progress_timer = make_progress_timer(observer.clone()).boxed();
        async move {
            // Send 0 outside of timer, so that 0 is always sent even if timer is never
            // scheduled. Allows unit tests to be deterministic.
            if let Some(observer) = observer {
                observer.receive_progress(None, 0., None, None).await;
            }
            let update_res = match select(system_updater, progress_timer).await {
                Either::Left((update_res, _)) => update_res,
                Either::Right(((), system_updater)) => system_updater.await,
            };
            if update_res.is_ok() {
                if let Some(observer) = observer {
                    observer.receive_progress(None, 1., None, None).await;
                }
            }
            update_res
        }
        .boxed()
    }

    fn perform_reboot(&mut self) -> BoxFuture<'_, Result<(), anyhow::Error>> {
        async move {
            zx::Status::ok(
                connect_to_service::<fidl_fuchsia_device_manager::AdministratorMarker>()?
                    .suspend(fidl_fuchsia_device_manager::SUSPEND_FLAG_REBOOT)
                    .await?,
            )
            .map_err(|e| anyhow!("Suspend error: {}", e))
        }
        .boxed()
    }
}

const EXPECTED_UPDATE_DURATION: Duration = Duration::from_minutes(4);
const TIMER_CHUNKS: u16 = 90;
const TIMER_MAX_PROGRESS: f32 = 0.9;

async fn make_progress_timer(observer: Option<&dyn ProgressObserver>) {
    let observer = if let Some(observer) = observer {
        observer
    } else {
        return ();
    };

    fasync::Interval::new(EXPECTED_UPDATE_DURATION / TIMER_CHUNKS)
        .enumerate()
        .take(TIMER_CHUNKS as usize)
        .for_each(|(i, ())| {
            observer.receive_progress(
                None,
                (i + 1) as f32 / (TIMER_CHUNKS as f32 / TIMER_MAX_PROGRESS),
                None,
                None,
            )
        })
        .await;
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_sys::{LaunchInfo, LauncherRequest, LauncherRequestStream, TerminationReason};
    use matches::assert_matches;
    use parking_lot::Mutex;
    use std::task::Poll;

    const TEST_URL: &str = "fuchsia-pkg://fuchsia.com/update/0";

    fn install_plan() -> FuchsiaInstallPlan {
        FuchsiaInstallPlan {
            url: TEST_URL.parse().unwrap(),
            install_source: InstallSource::OnDemand,
        }
    }

    async fn mock_launcher(mut reqs: LauncherRequestStream, exit_code: i64) {
        match reqs.next().await.unwrap() {
            Ok(LauncherRequest::CreateComponent {
                launch_info: LaunchInfo { url, arguments, .. },
                controller: Some(controller),
                ..
            }) => {
                assert_eq!(url, "fuchsia-pkg://fuchsia.com/amber#meta/system_updater.cmx");
                assert_eq!(
                    arguments,
                    Some(vec![
                        "-initiator".to_string(),
                        "manual".to_string(),
                        "-update".to_string(),
                        TEST_URL.to_string(),
                        "-reboot=false".to_string()
                    ])
                );
                let (_stream, handle) = controller.into_stream_and_control_handle().unwrap();
                handle.send_on_terminated(exit_code, TerminationReason::Exited).unwrap();
            }
            request => panic!("Unexpected request: {:?}", request),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_perform_install() {
        let (mut installer, stream) = FuchsiaInstaller::new_mock();
        let installer_fut = async move {
            installer.perform_install(&install_plan(), None).await.unwrap();
        };
        let launcher_fut = mock_launcher(stream, 0);
        future::join(installer_fut, launcher_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_install_error() {
        let (mut installer, stream) = FuchsiaInstaller::new_mock();
        let installer_fut = async move {
            match installer.perform_install(&install_plan(), None).await {
                Err(FuchsiaInstallError::Installer(_)) => {} // expected
                result => panic!("Unexpected result: {:?}", result),
            }
        };
        let launcher_fut = mock_launcher(stream, 1);
        future::join(installer_fut, launcher_fut).await;
    }

    struct MockProgressObserver {
        progresses: Mutex<Vec<f32>>,
    }
    impl MockProgressObserver {
        fn new() -> Self {
            Self { progresses: Mutex::new(vec![]) }
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
            assert_eq!(operation, None);
            assert_eq!(total_size, None);
            assert_eq!(size_so_far, None);
            self.progresses.lock().push(progress);
            future::ready(()).boxed()
        }
    }

    #[test]
    fn test_progress_timer_sends_all_updates() {
        let mut exec = fasync::Executor::new_with_fake_time().unwrap();
        let (mut installer, _stream) = FuchsiaInstaller::new_mock();
        let observer = MockProgressObserver::new();

        let mut installer_fut = installer.perform_install(&install_plan(), Some(&observer));

        // Launcher's `_stream` is never handled, so installation will never complete.
        assert_matches!(exec.run_until_stalled(&mut installer_fut), Poll::Pending);
        while exec.wake_next_timer().is_some() {
            assert_matches!(exec.run_until_stalled(&mut installer_fut), Poll::Pending);
        }
        assert_eq!(
            *observer.progresses.lock(),
            (0..=90).map(|i| i as f32 / 100.).collect::<Vec<_>>()
        );
    }

    #[test]
    fn test_progress_if_install_finishes_before_progress_timer_starts() {
        let mut exec = fasync::Executor::new_with_fake_time().unwrap();
        let (mut installer, stream) = FuchsiaInstaller::new_mock();
        let observer = MockProgressObserver::new();
        let launcher_fut = mock_launcher(stream, 0);

        let installer_fut = installer.perform_install(&install_plan(), Some(&observer));

        // Executor's fake time is never changed and timers are never manually awoken, so
        // progress timer will never send progress updates.
        assert_matches!(
            exec.run_until_stalled(&mut future::join(installer_fut, launcher_fut).boxed()),
            Poll::Ready((Ok(()), ()))
        );
        assert_eq!(*observer.progresses.lock(), vec![0., 1.]);
    }

    #[test]
    fn test_progress_if_install_finishes_before_progress_timer_finishes() {
        let mut exec = fasync::Executor::new_with_fake_time().unwrap();
        let (mut installer, stream) = FuchsiaInstaller::new_mock();
        let observer = MockProgressObserver::new();
        let launcher_fut = mock_launcher(stream, 0);

        let mut installer_fut = installer.perform_install(&install_plan(), Some(&observer));

        assert_matches!(exec.run_until_stalled(&mut installer_fut), Poll::Pending);
        exec.set_fake_time(exec.now() + (EXPECTED_UPDATE_DURATION / 10));
        while exec.wake_expired_timers() {
            assert_matches!(exec.run_until_stalled(&mut installer_fut), Poll::Pending);
        }

        assert_matches!(
            exec.run_until_stalled(&mut future::join(installer_fut, launcher_fut).boxed()),
            Poll::Ready((Ok(()), ()))
        );
        assert_eq!(
            *observer.progresses.lock(),
            vec![0., 0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.07, 0.08, 0.09, 1.]
        );
    }
}
