// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::Error as ErrorKind;
use anyhow::{Context as _, Error};
use fidl_fuchsia_sys::{LauncherMarker, LauncherProxy};
use fuchsia_async::futures::{future::BoxFuture, FutureExt};
use fuchsia_component::client::{connect_to_service, launch};
use fuchsia_merkle::Hash;
use fuchsia_syslog::{fx_log_info, fx_log_warn};
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
) -> Result<(), anyhow::Error> {
    let launcher =
        connect_to_service::<LauncherMarker>().map_err(|_| ErrorKind::ConnectToLauncher)?;
    let mut component_runner = RealComponentRunner { launcher_proxy: launcher };
    apply_system_update_impl(
        current_system_image,
        latest_system_image,
        &mut component_runner,
        initiator,
        &RealTimeSource,
        &RealFileSystem,
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

trait FileSystem {
    fn read_to_string(&self, path: &str) -> std::io::Result<String>;
}

struct RealFileSystem;

impl FileSystem for RealFileSystem {
    fn read_to_string(&self, path: &str) -> std::io::Result<String> {
        std::fs::read_to_string(path)
    }
}

// TODO(fxb/22779): Undo this once we do this for all base packages by default.
fn get_system_updater_resource_url(
    file_system: &impl FileSystem,
) -> Result<String, crate::errors::Error> {
    // Attempt to find pinned version.
    let file = file_system
        .read_to_string("/system/data/static_packages")
        .map_err(|_| ErrorKind::ReadStaticPackages)?;

    for line in file.lines() {
        let line = line.trim();
        // Line format to parse: `<NAME>/<VERSION>=<MERKLE>`.
        let parts: Vec<_> = line.split("=").collect();
        if parts.len() != 2 {
            fx_log_warn!("invalid line in static manifest: {}", line);
            continue;
        }
        let name_version = parts[0];
        let merkle = parts[1];

        if merkle.len() != 64 {
            fx_log_warn!("invalid merkleroot in static manifest: {}", line);
            continue;
        }

        let parts: Vec<_> = name_version.split("/").collect();
        if parts.len() != 2 {
            fx_log_warn!("invalid name/version pair in static manifest: {}", line);
            continue;
        }
        let name = parts[0];

        if name != "amber" {
            continue;
        }

        return Ok(format!(
            "fuchsia-pkg://fuchsia.com/amber?hash={}#meta/system_updater.cmx",
            merkle
        ));
    }
    fx_log_warn!("Unable to find 'amber' in static manifest");

    // Backup is to just use unpinned version.
    Ok(SYSTEM_UPDATER_RESOURCE_URL.to_string())
}

async fn apply_system_update_impl<'a>(
    current_system_image: Hash,
    latest_system_image: Hash,
    component_runner: &'a mut impl ComponentRunner,
    initiator: Initiator,
    time_source: &'a impl TimeSource,
    file_system: &'a impl FileSystem,
) -> Result<(), anyhow::Error> {
    fx_log_info!("starting system_updater");
    let fut = component_runner.run_until_exit(
        get_system_updater_resource_url(file_system)?,
        Some(vec![
            format!("-initiator={}", initiator),
            format!("-start={}", time_source.get_nanos()),
            format!("-source={}", current_system_image),
            format!("-target={}", latest_system_image),
        ]),
    );
    fut.await?;
    Err(ErrorKind::SystemUpdaterFinished)?
}

#[cfg(test)]
mod test_apply_system_update_impl {
    use super::*;
    use fuchsia_async::{self as fasync, futures::future};
    use proptest::prelude::*;

    const ACTIVE_SYSTEM_IMAGE_MERKLE: [u8; 32] = [0u8; 32];
    const NEW_SYSTEM_IMAGE_MERKLE: [u8; 32] = [1u8; 32];

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

    const HAS_AMBER: &str =
        "amber/0=6b8f5baf0eff6379701cedd3a86ab0fde5dfd8d73c6cf488926b2c94cdf63af0 \n\
         pkgfs/0=1d3c71e2124dc84263a56559ab72bccc840679fe95c91efe0b1a49b2bc0d9f62 ";

    const NO_AMBER: &str =
        "pkgfs/0=1d3c71e2124dc84263a56559ab72bccc840679fe95c91efe0b1a49b2bc0d9f62 ";

    struct FakeFileSystem {
        has_amber: bool,
    }

    impl FileSystem for FakeFileSystem {
        fn read_to_string(&self, path: &str) -> std::io::Result<String> {
            if path != "/system/data/static_packages" {
                return Err(std::io::Error::new(std::io::ErrorKind::NotFound, "invalid path"));
            }
            if self.has_amber {
                Ok(HAS_AMBER.to_string())
            } else {
                Ok(NO_AMBER.to_string())
            }
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_trigger_pkgfs_gc_if_update_available() {
        let mut component_runner = DoNothingComponentRunner;
        let time_source = FakeTimeSource { now: 0 };
        let filesystem = FakeFileSystem { has_amber: true };

        let result = apply_system_update_impl(
            ACTIVE_SYSTEM_IMAGE_MERKLE.into(),
            NEW_SYSTEM_IMAGE_MERKLE.into(),
            &mut component_runner,
            Initiator::Manual,
            &time_source,
            &filesystem,
        )
        .await;

        assert_eq!(
            result.unwrap_err().downcast::<ErrorKind>().unwrap(),
            ErrorKind::SystemUpdaterFinished
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_launch_system_updater_if_update_available() {
        let mut component_runner = WasCalledComponentRunner { was_called: false };
        let time_source = FakeTimeSource { now: 0 };
        let filesystem = FakeFileSystem { has_amber: true };

        let result = apply_system_update_impl(
            ACTIVE_SYSTEM_IMAGE_MERKLE.into(),
            NEW_SYSTEM_IMAGE_MERKLE.into(),
            &mut component_runner,
            Initiator::Manual,
            &time_source,
            &filesystem,
        )
        .await;

        assert_eq!(
            result.unwrap_err().downcast::<ErrorKind>().unwrap(),
            ErrorKind::SystemUpdaterFinished
        );
        assert!(component_runner.was_called);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_launch_system_updater_even_if_gc_fails() {
        let mut component_runner = WasCalledComponentRunner { was_called: false };
        let time_source = FakeTimeSource { now: 0 };
        let filesystem = FakeFileSystem { has_amber: true };

        let result = apply_system_update_impl(
            ACTIVE_SYSTEM_IMAGE_MERKLE.into(),
            NEW_SYSTEM_IMAGE_MERKLE.into(),
            &mut component_runner,
            Initiator::Manual,
            &time_source,
            &filesystem,
        )
        .await;

        assert_eq!(
            result.unwrap_err().downcast::<ErrorKind>().unwrap(),
            ErrorKind::SystemUpdaterFinished
        );
        assert!(component_runner.was_called);
    }

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

    #[fasync::run_singlethreaded(test)]
    async fn test_launch_system_updater_url_obtained_from_static_packages() {
        let mut component_runner = ArgumentCapturingComponentRunner { captured_args: vec![] };
        let time_source = FakeTimeSource { now: 0 };
        let filesystem = FakeFileSystem { has_amber: true };

        let result = apply_system_update_impl(
            ACTIVE_SYSTEM_IMAGE_MERKLE.into(),
            NEW_SYSTEM_IMAGE_MERKLE.into(),
            &mut component_runner,
            Initiator::Manual,
            &time_source,
            &filesystem,
        )
        .await;

        let expected_url = "fuchsia-pkg://fuchsia.com/amber?hash=\
                            6b8f5baf0eff6379701cedd3a86ab0fde5dfd8d73c6cf488926b2c94cdf63af0\
                            #meta/system_updater.cmx"
            .to_string();

        assert_eq!(
            result.unwrap_err().downcast::<ErrorKind>().unwrap(),
            ErrorKind::SystemUpdaterFinished
        );
        assert_eq!(
            component_runner.captured_args,
            vec![Args {
                url: expected_url,
                arguments: Some(vec![
                    format!("-initiator={}", Initiator::Manual),
                    format!("-start={}", 0),
                    format!("-source={}", std::iter::repeat('0').take(64).collect::<String>()),
                    format!("-target={}", std::iter::repeat("01").take(32).collect::<String>()),
                ])
            }]
        );
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

            let mut component_runner = ArgumentCapturingComponentRunner { captured_args: vec![] };
            let time_source = FakeTimeSource { now: start_time };
            let filesystem = FakeFileSystem { has_amber: false };

            let mut executor =
                fasync::Executor::new().expect("create executor in test");
            let result = executor.run_singlethreaded(apply_system_update_impl(
                source_merkle.parse().expect("source merkle string literal"),
                target_merkle.parse().expect("target merkle string literal"),
                &mut component_runner,
                initiator,
                &time_source,
                &filesystem,
            ));

            prop_assert!(result.is_err());
            prop_assert_eq!(result.unwrap_err().downcast::<ErrorKind>().unwrap(), ErrorKind::SystemUpdaterFinished);
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
    use fuchsia_async as fasync;
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
            Err(e) if e.is_closed()
        );
    }
}

#[cfg(test)]
mod test_real_component_runner {
    use super::*;
    use fuchsia_async as fasync;

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
        assert_eq!(
            run_res.err().expect("run should fail").downcast::<ErrorKind>().unwrap(),
            ErrorKind::SystemUpdaterFailed
        );
    }
}
