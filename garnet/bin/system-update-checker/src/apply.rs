// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::{Error, ErrorKind};
use failure::ResultExt;
use fidl_fuchsia_io;
use fidl_fuchsia_sys::{LauncherMarker, LauncherProxy};
use fuchsia_async::{
    self as fasync,
    futures::{future::BoxFuture, FutureExt},
};
use fuchsia_component::client::{connect_to_service, launch};
use fuchsia_merkle::Hash;
use fuchsia_syslog::{fx_log_err, fx_log_info};
use fuchsia_zircon as zx;

#[cfg(test)]
use proptest_derive::Arbitrary;

const SYSTEM_UPDATER_RESOURCE_URL: &str = "fuchsia-pkg://fuchsia.com/amber#meta/system_updater.cmx";

#[derive(Debug, Clone, Copy)]
#[cfg_attr(test, derive(Arbitrary))]
pub enum Initiator {
    Manual,
    Automatic,
}

impl std::fmt::Display for Initiator {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match *self {
            Initiator::Manual => write!(f, "manual"),
            Initiator::Automatic => write!(f, "automatic"),
        }
    }
}

// On success, system will reboot before this function returns
pub async fn apply_system_update(
    current_system_image: Hash,
    latest_system_image: Hash,
    initiator: Initiator,
) -> Result<(), Error> {
    let launcher = connect_to_service::<LauncherMarker>().context(ErrorKind::ConnectToLauncher)?;
    let mut component_runner = RealComponentRunner { launcher_proxy: launcher };
    apply_system_update_impl(
        current_system_image,
        latest_system_image,
        &RealServiceConnector,
        &mut component_runner,
        initiator,
        &RealTimeSource,
    )
    .await
}

// For mocking
trait ServiceConnector {
    fn service_connect(&self, service_path: &str, channel: zx::Channel) -> Result<(), zx::Status>;
}

struct RealServiceConnector;

impl ServiceConnector for RealServiceConnector {
    fn service_connect(&self, service_path: &str, channel: zx::Channel) -> Result<(), zx::Status> {
        fdio::service_connect(service_path, channel)
    }
}

// For mocking
trait ComponentRunner {
    fn run_until_exit(
        &mut self,
        url: String,
        arguments: Option<Vec<String>>,
    ) -> BoxFuture<'_, Result<(), Error>>;
}

struct RealComponentRunner {
    launcher_proxy: LauncherProxy,
}

impl ComponentRunner for RealComponentRunner {
    fn run_until_exit(
        &mut self,
        url: String,
        arguments: Option<Vec<String>>,
    ) -> BoxFuture<'_, Result<(), Error>> {
        let app_res = launch(&self.launcher_proxy, url, arguments);
        async move {
            let mut app = app_res.context(ErrorKind::LaunchSystemUpdater)?;
            let exit_status = app.wait().await.context(ErrorKind::WaitForSystemUpdater)?;
            exit_status.ok().context(ErrorKind::SystemUpdaterFailed)?;
            Ok(())
        }
            .boxed()
    }
}

// For mocking
trait TimeSource {
    fn get_nanos(&self) -> i64;
}

struct RealTimeSource;

impl TimeSource for RealTimeSource {
    fn get_nanos(&self) -> i64 {
        zx::Time::get(zx::ClockId::UTC).into_nanos()
    }
}

async fn apply_system_update_impl<'a>(
    current_system_image: Hash,
    latest_system_image: Hash,
    service_connector: &'a impl ServiceConnector,
    component_runner: &'a mut impl ComponentRunner,
    initiator: Initiator,
    time_source: &'a impl TimeSource,
) -> Result<(), Error> {
    if let Err(err) = pkgfs_gc(service_connector).await {
        fx_log_err!("failed to garbage collect pkgfs, will still attempt system update: {}", err);
    }
    fx_log_info!("starting system_updater");
    component_runner
        .run_until_exit(
            SYSTEM_UPDATER_RESOURCE_URL.to_string(),
            Some(vec![
                format!("-initiator={}", initiator),
                format!("-start={}", time_source.get_nanos()),
                format!("-source={}", current_system_image),
                format!("-target={}", latest_system_image),
            ]),
        )
        .await?;
    Err(ErrorKind::SystemUpdaterFinished)?
}

async fn pkgfs_gc(service_connector: &impl ServiceConnector) -> Result<(), Error> {
    fx_log_info!("triggering pkgfs GC");
    let (dir_end, dir_server_end) =
        fidl::endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>()
            .context(ErrorKind::PkgfsGc)?;
    service_connector
        .service_connect("/pkgfs/ctl", dir_server_end.into_channel())
        .context(ErrorKind::PkgfsGc)?;
    let dir_proxy = fidl_fuchsia_io::DirectoryProxy::new(
        fasync::Channel::from_channel(dir_end.into_channel()).context(ErrorKind::PkgfsGc)?,
    );
    let status = dir_proxy.unlink("garbage").await.context(ErrorKind::PkgfsGc)?;
    zx::Status::ok(status).context(ErrorKind::PkgfsGc)?;
    Ok(())
}

#[cfg(test)]
mod test_apply_system_update_impl {
    use super::*;
    use fuchsia_async::futures::future;
    use matches::assert_matches;
    use proptest::prelude::*;
    use std::fs;

    const ACTIVE_SYSTEM_IMAGE_MERKLE: [u8; 32] = [0u8; 32];
    const NEW_SYSTEM_IMAGE_MERKLE: [u8; 32] = [1u8; 32];

    struct TempDirServiceConnector {
        temp_dir: tempfile::TempDir,
    }
    impl TempDirServiceConnector {
        fn new() -> TempDirServiceConnector {
            TempDirServiceConnector { temp_dir: tempfile::tempdir().expect("create temp dir") }
        }
        fn new_with_pkgfs_garbage() -> TempDirServiceConnector {
            let service_connector = Self::new();
            let pkgfs = service_connector.temp_dir.path().join("pkgfs");
            fs::create_dir(&pkgfs).expect("create pkgfs dir");
            fs::create_dir(pkgfs.join("ctl")).expect("create pkgfs/ctl dir");
            fs::File::create(pkgfs.join("ctl/garbage")).expect("create garbage file");
            service_connector
        }
    }
    impl TempDirServiceConnector {
        fn has_garbage_file(&self) -> bool {
            self.temp_dir.path().join("pkgfs/ctl/garbage").exists()
        }
    }
    impl ServiceConnector for TempDirServiceConnector {
        fn service_connect(
            &self,
            service_path: &str,
            channel: zx::Channel,
        ) -> Result<(), zx::Status> {
            fdio::service_connect(
                self.temp_dir.path().join(&service_path[1..]).to_str().expect("paths are utf8"),
                channel,
            )
        }
    }

    struct DoNothingComponentRunner;
    impl ComponentRunner for DoNothingComponentRunner {
        fn run_until_exit(
            &mut self,
            _url: String,
            _arguments: Option<Vec<String>>,
        ) -> BoxFuture<'_, Result<(), Error>> {
            future::ok(()).boxed()
        }
    }

    struct WasCalledComponentRunner {
        was_called: bool,
    }
    impl ComponentRunner for WasCalledComponentRunner {
        fn run_until_exit(
            &mut self,
            _url: String,
            _arguments: Option<Vec<String>>,
        ) -> BoxFuture<'_, Result<(), Error>> {
            self.was_called = true;
            future::ok(()).boxed()
        }
    }

    struct FakeTimeSource {
        now: i64,
    }
    impl TimeSource for FakeTimeSource {
        fn get_nanos(&self) -> i64 {
            self.now
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_trigger_pkgfs_gc_if_update_available() {
        let service_connector = TempDirServiceConnector::new_with_pkgfs_garbage();
        let mut component_runner = DoNothingComponentRunner;
        let time_source = FakeTimeSource { now: 0 };
        assert!(service_connector.has_garbage_file());

        let result = apply_system_update_impl(
            ACTIVE_SYSTEM_IMAGE_MERKLE.into(),
            NEW_SYSTEM_IMAGE_MERKLE.into(),
            &service_connector,
            &mut component_runner,
            Initiator::Manual,
            &time_source,
        )
        .await;

        assert_matches!(result.map_err(|e| e.kind()), Err(ErrorKind::SystemUpdaterFinished));
        assert!(!service_connector.has_garbage_file());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_launch_system_updater_if_update_available() {
        let service_connector = TempDirServiceConnector::new();
        let mut component_runner = WasCalledComponentRunner { was_called: false };
        let time_source = FakeTimeSource { now: 0 };

        let result = apply_system_update_impl(
            ACTIVE_SYSTEM_IMAGE_MERKLE.into(),
            NEW_SYSTEM_IMAGE_MERKLE.into(),
            &service_connector,
            &mut component_runner,
            Initiator::Manual,
            &time_source,
        )
        .await;

        assert_matches!(result.map_err(|e| e.kind()), Err(ErrorKind::SystemUpdaterFinished));
        assert!(component_runner.was_called);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_launch_system_updater_even_if_gc_fails() {
        let service_connector = TempDirServiceConnector::new();
        let mut component_runner = WasCalledComponentRunner { was_called: false };
        let time_source = FakeTimeSource { now: 0 };

        let result = apply_system_update_impl(
            ACTIVE_SYSTEM_IMAGE_MERKLE.into(),
            NEW_SYSTEM_IMAGE_MERKLE.into(),
            &service_connector,
            &mut component_runner,
            Initiator::Manual,
            &time_source,
        )
        .await;

        assert_matches!(result.map_err(|e| e.kind()), Err(ErrorKind::SystemUpdaterFinished));
        assert!(component_runner.was_called);
    }

    proptest! {
        #[test]
        fn test_values_passed_through_to_component_launcher(
            initiator: Initiator,
            start_time in proptest::num::i64::ANY,
            source_merkle in "[A-Fa-f0-9]{64}",
            target_merkle in "[A-Fa-f0-9]{64}")
        {
            prop_assume!(source_merkle != target_merkle);
            #[derive(Debug, PartialEq, Eq)]
            struct Args {
                url: String,
                arguments: Option<Vec<String>>,
            }
            struct ArgumentCapturingComponentRunner {
                captured_args: Vec<Args>,
            }
            impl ComponentRunner for ArgumentCapturingComponentRunner {
                fn run_until_exit(
                    &mut self,
                    url: String,
                    arguments: Option<Vec<String>>,
                ) -> BoxFuture<'_, Result<(), Error>> {
                    self.captured_args.push(Args { url, arguments });
                    future::ok(()).boxed()
                }
            }

            let service_connector = TempDirServiceConnector::new();
            let mut component_runner = ArgumentCapturingComponentRunner { captured_args: vec![] };
            let time_source = FakeTimeSource { now: start_time };

            let mut executor =
                fasync::Executor::new().expect("create executor in test");
            let result = executor.run_singlethreaded(apply_system_update_impl(
                source_merkle.parse().expect("source merkle string literal"),
                target_merkle.parse().expect("target merkle string literal"),
                &service_connector,
                &mut component_runner,
                initiator,
                &time_source,
            ));


            prop_assert!(result.is_err());
            prop_assert_eq!(
                result.err().unwrap().kind(),
                ErrorKind::SystemUpdaterFinished
            );
            prop_assert_eq!(
                component_runner.captured_args,
                vec![Args {
                    url: SYSTEM_UPDATER_RESOURCE_URL.to_string(),
                    arguments: Some(vec![
                        format!("-initiator={}", initiator),
                        format!("-start={}", start_time),
                        format!("-source={}", source_merkle.to_lowercase()),
                        format!("-target={}", target_merkle.to_lowercase()),
                    ])
                }]
            );
        }
    }
}

#[cfg(test)]
mod test_real_service_connector {
    use super::*;
    use matches::assert_matches;
    use std::fs;

    #[fasync::run_singlethreaded(test)]
    async fn test_connect_to_directory_and_unlink_file() {
        let dir = tempfile::tempdir().expect("create temp dir");
        let file_name = "the-file";
        let file_path = dir.path().join(file_name);
        fs::File::create(&file_path).expect("create file");
        let (dir_end, dir_server_end) =
            fidl::endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>()
                .expect("create endpoints");
        RealServiceConnector
            .service_connect(
                dir.path().to_str().expect("paths are utf8"),
                dir_server_end.into_channel(),
            )
            .expect("service_connect");
        let dir_proxy = fidl_fuchsia_io::DirectoryProxy::new(
            fasync::Channel::from_channel(dir_end.into_channel()).expect("create async channel"),
        );

        assert!(file_path.exists());
        let status = dir_proxy.unlink(file_name).await.expect("unlink the file fidl");
        zx::Status::ok(status).expect("unlink the file");
        assert!(!file_path.exists());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_connect_to_missing_directory_errors() {
        let dir = tempfile::tempdir().expect("create temp dir");
        let (dir_end, dir_server_end) =
            fidl::endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>()
                .expect("create endpoints");
        RealServiceConnector
            .service_connect(
                dir.path().join("non-existent-directory").to_str().expect("paths are utf8"),
                dir_server_end.into_channel(),
            )
            .expect("service_connect");
        let dir_proxy = fidl_fuchsia_io::DirectoryProxy::new(
            fasync::Channel::from_channel(dir_end.into_channel()).expect("create async channel"),
        );

        let read_dirents_res = dir_proxy
            .read_dirents(1000 /*size shouldn't matter, as this should immediately fail*/)
            .await;

        assert_matches!(
            read_dirents_res,
            Err(fidl::Error::ClientRead(zx::Status::PEER_CLOSED))
                | Err(fidl::Error::ClientWrite(zx::Status::PEER_CLOSED))
        );
    }
}

#[cfg(test)]
mod test_real_component_runner {
    use super::*;

    const TEST_SHELL_COMMAND_RESOURCE_URL: &str =
        "fuchsia-pkg://fuchsia.com/system-update-checker-tests/0#meta/test-shell-command.cmx";

    #[fasync::run_singlethreaded(test)]
    async fn test_run_a_component_that_exits_0() {
        let launcher_proxy = connect_to_service::<LauncherMarker>().expect("connect to launcher");
        let mut runner = RealComponentRunner { launcher_proxy };
        let run_res = runner
            .run_until_exit(
                TEST_SHELL_COMMAND_RESOURCE_URL.to_string(),
                Some(vec!["!".to_string()]),
            )
            .await;
        assert!(run_res.is_ok(), "{:?}", run_res.err().unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_run_a_component_that_exits_1() {
        let launcher_proxy = connect_to_service::<LauncherMarker>().expect("connect to launcher");
        let mut runner = RealComponentRunner { launcher_proxy };
        let run_res =
            runner.run_until_exit(TEST_SHELL_COMMAND_RESOURCE_URL.to_string(), Some(vec![])).await;
        assert_eq!(run_res.err().expect("run should fail").kind(), ErrorKind::SystemUpdaterFailed);
    }
}
