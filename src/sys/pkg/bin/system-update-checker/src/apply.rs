// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::Error;
use anyhow::Context as _;
use fidl_fuchsia_hardware_power_statecontrol::{AdminProxy, RebootReason};
use fidl_fuchsia_sys::{LauncherMarker, LauncherProxy};
use fidl_fuchsia_update_ext::Initiator;
use fuchsia_async::futures::{future::BoxFuture, FutureExt};
use fuchsia_component::client::{connect_to_service, launch};
use fuchsia_hash::Hash;
use fuchsia_syslog::{fx_log_info, fx_log_warn};
use fuchsia_zircon as zx;

const SYSTEM_UPDATER_RESOURCE_URL: &str =
    "fuchsia-pkg://fuchsia.com/system-updater#meta/system-updater.cmx";

fn initiator_cli_arg(initiator: Initiator) -> &'static str {
    match initiator {
        Initiator::User => "manual",
        Initiator::Service => "automatic",
    }
}

// On success, system will reboot before this function returns
pub async fn apply_system_update(
    current_system_image: Hash,
    latest_system_image: Hash,
    initiator: Initiator,
) -> Result<(), anyhow::Error> {
    let launcher =
        connect_to_service::<LauncherMarker>().context("connecting to component Launcher")?;
    let mut component_runner = RealComponentRunner { launcher_proxy: launcher };

    let reboot_service =
        connect_to_service::<fidl_fuchsia_hardware_power_statecontrol::AdminMarker>()
            .context("connecting to power state control")?;

    apply_system_update_and_reboot(
        current_system_image,
        latest_system_image,
        &mut component_runner,
        initiator,
        &RealTimeSource,
        &RealFileSystem,
        reboot_service,
    )
    .await
}

async fn apply_system_update_and_reboot<'a>(
    current_system_image: Hash,
    latest_system_image: Hash,
    component_runner: &'a mut impl ComponentRunner,
    initiator: Initiator,
    time_source: &'a impl TimeSource,
    file_system: &'a impl FileSystem,
    reboot_service: AdminProxy,
) -> Result<(), anyhow::Error> {
    apply_system_update_impl(
        current_system_image,
        latest_system_image,
        component_runner,
        initiator,
        time_source,
        file_system,
    )
    .await?;

    fx_log_info!("Successful update, rebooting...");

    reboot_service
        .reboot(RebootReason::SystemUpdate)
        .await
        .context("while performing reboot call")?
        .map_err(|e| Error::RebootFailed(zx::Status::from_raw(e)))
        .context("reboot responded with")
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
    ) -> BoxFuture<'_, Result<(), anyhow::Error>>;
}

struct RealComponentRunner {
    launcher_proxy: LauncherProxy,
}

impl ComponentRunner for RealComponentRunner {
    fn run_until_exit(
        &mut self,
        url: String,
        arguments: Option<Vec<String>>,
    ) -> BoxFuture<'_, Result<(), anyhow::Error>> {
        let app_res = launch(&self.launcher_proxy, url, arguments);
        async move {
            let mut app = app_res.context("launching system updater component")?;
            let exit_status = app.wait().await.context("waiting for system updater")?;
            exit_status.ok().context(Error::SystemUpdaterFailed)?;
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
fn get_system_updater_resource_url(file_system: &impl FileSystem) -> Result<String, Error> {
    // Attempt to find pinned version.
    let file = file_system
        .read_to_string("/pkgfs/system/data/static_packages")
        .map_err(Error::ReadStaticPackages)?;

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

        if name != "system-updater" {
            continue;
        }

        return Ok(format!(
            "fuchsia-pkg://fuchsia.com/system-updater?hash={}#meta/system-updater.cmx",
            merkle
        ));
    }
    fx_log_warn!("Unable to find 'system-updater' in static manifest");

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
    fx_log_info!("starting system updater");
    let fut = component_runner.run_until_exit(
        get_system_updater_resource_url(file_system)?,
        Some(
            vec![
                "--initiator",
                initiator_cli_arg(initiator),
                "--start",
                &format!("{}", time_source.get_nanos()),
                "--source",
                &format!("{}", current_system_image),
                "--target",
                &format!("{}", latest_system_image),
                "--reboot",
                "false",
                "--oneshot",
                "true",
            ]
            .iter()
            .map(|s| s.to_string())
            .collect(),
        ),
    );
    fut.await?;
    Ok(())
}

#[cfg(test)]
mod test_apply_system_update_impl {
    use super::*;
    use anyhow::anyhow;
    use fuchsia_async::{self as fasync, futures::future};
    use matches::assert_matches;
    use mock_reboot::MockRebootService;
    use proptest::prelude::*;
    use std::sync::{atomic::AtomicU32, atomic::Ordering, Arc};

    const ACTIVE_SYSTEM_IMAGE_MERKLE: [u8; 32] = [0u8; 32];
    const NEW_SYSTEM_IMAGE_MERKLE: [u8; 32] = [1u8; 32];

    struct DoNothingComponentRunner;
    impl ComponentRunner for DoNothingComponentRunner {
        fn run_until_exit(
            &mut self,
            _url: String,
            _arguments: Option<Vec<String>>,
        ) -> BoxFuture<'_, Result<(), anyhow::Error>> {
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
        ) -> BoxFuture<'_, Result<(), anyhow::Error>> {
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

    const HAS_SYSTEM_UPDATER: &str =
        "system-updater/0=6b8f5baf0eff6379701cedd3a86ab0fde5dfd8d73c6cf488926b2c94cdf63af0 \n\
         pkgfs/0=1d3c71e2124dc84263a56559ab72bccc840679fe95c91efe0b1a49b2bc0d9f62 ";

    const NO_SYSTEM_UPDATER: &str =
        "pkgfs/0=1d3c71e2124dc84263a56559ab72bccc840679fe95c91efe0b1a49b2bc0d9f62 ";

    struct FakeFileSystem {
        has_system_updater: bool,
    }

    impl FileSystem for FakeFileSystem {
        fn read_to_string(&self, path: &str) -> std::io::Result<String> {
            if path != "/pkgfs/system/data/static_packages" {
                return Err(std::io::Error::new(std::io::ErrorKind::NotFound, "invalid path"));
            }
            if self.has_system_updater {
                Ok(HAS_SYSTEM_UPDATER.to_string())
            } else {
                Ok(NO_SYSTEM_UPDATER.to_string())
            }
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_trigger_pkgfs_gc_if_update_available() {
        let mut component_runner = DoNothingComponentRunner;
        let time_source = FakeTimeSource { now: 0 };
        let filesystem = FakeFileSystem { has_system_updater: true };

        apply_system_update_impl(
            ACTIVE_SYSTEM_IMAGE_MERKLE.into(),
            NEW_SYSTEM_IMAGE_MERKLE.into(),
            &mut component_runner,
            Initiator::User,
            &time_source,
            &filesystem,
        )
        .await
        .unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_launch_system_updater_if_update_available() {
        let mut component_runner = WasCalledComponentRunner { was_called: false };
        let time_source = FakeTimeSource { now: 0 };
        let filesystem = FakeFileSystem { has_system_updater: true };

        apply_system_update_impl(
            ACTIVE_SYSTEM_IMAGE_MERKLE.into(),
            NEW_SYSTEM_IMAGE_MERKLE.into(),
            &mut component_runner,
            Initiator::User,
            &time_source,
            &filesystem,
        )
        .await
        .unwrap();

        assert!(component_runner.was_called);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_launch_system_updater_even_if_gc_fails() {
        let mut component_runner = WasCalledComponentRunner { was_called: false };
        let time_source = FakeTimeSource { now: 0 };
        let filesystem = FakeFileSystem { has_system_updater: true };

        apply_system_update_impl(
            ACTIVE_SYSTEM_IMAGE_MERKLE.into(),
            NEW_SYSTEM_IMAGE_MERKLE.into(),
            &mut component_runner,
            Initiator::User,
            &time_source,
            &filesystem,
        )
        .await
        .unwrap();

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
        ) -> BoxFuture<'_, Result<(), anyhow::Error>> {
            self.captured_args.push(Args { url, arguments });
            future::ok(()).boxed()
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_launch_system_updater_url_obtained_from_static_packages() {
        let mut component_runner = ArgumentCapturingComponentRunner { captured_args: vec![] };
        let time_source = FakeTimeSource { now: 0 };
        let filesystem = FakeFileSystem { has_system_updater: true };

        apply_system_update_impl(
            ACTIVE_SYSTEM_IMAGE_MERKLE.into(),
            NEW_SYSTEM_IMAGE_MERKLE.into(),
            &mut component_runner,
            Initiator::User,
            &time_source,
            &filesystem,
        )
        .await
        .unwrap();

        let expected_url = "fuchsia-pkg://fuchsia.com/system-updater?hash=\
                            6b8f5baf0eff6379701cedd3a86ab0fde5dfd8d73c6cf488926b2c94cdf63af0\
                            #meta/system-updater.cmx"
            .to_string();

        assert_eq!(
            component_runner.captured_args,
            vec![Args {
                url: expected_url,
                arguments: Some(
                    vec![
                        "--initiator",
                        "manual",
                        "--start",
                        "0",
                        "--source",
                        "0000000000000000000000000000000000000000000000000000000000000000",
                        "--target",
                        "0101010101010101010101010101010101010101010101010101010101010101",
                        "--reboot",
                        "false",
                        "--oneshot",
                        "true",
                    ]
                    .iter()
                    .map(|s| s.to_string())
                    .collect()
                )
            }]
        );
    }

    // Many implementations of a reboot test will do it in a full TestEnv,
    // but if you find yourself copying this struct, consider refactoring it into
    // the mock-reboot crate
    struct RebootTestState {
        _reboot_service: Arc<MockRebootService>,
        reboot_service_proxy: AdminProxy,
        reboot_service_call_count: Arc<AtomicU32>,
    }

    fn setup_reboot_test(reboot_should_fail: bool) -> RebootTestState {
        let call_count = Arc::new(AtomicU32::new(0));
        let call_count_clone = Arc::clone(&call_count);
        let reboot_service_callback = Box::new(move || {
            call_count_clone.fetch_add(1, Ordering::SeqCst);
            if reboot_should_fail {
                Err(zx::Status::PEER_CLOSED.into_raw())
            } else {
                Ok(())
            }
        });
        let reboot_service = Arc::new(MockRebootService::new(reboot_service_callback));
        let reboot_service_clone = Arc::clone(&reboot_service);

        let proxy = reboot_service_clone.spawn_reboot_service();

        RebootTestState {
            _reboot_service: reboot_service,
            reboot_service_proxy: proxy,
            reboot_service_call_count: call_count,
        }
    }

    // Test that if system updater succeeds, system-update-checker calls the reboot service.
    #[fasync::run_singlethreaded(test)]
    async fn test_reboot_on_success() {
        let reboot_test_state = setup_reboot_test(false);

        let mut component_runner = DoNothingComponentRunner;
        let time_source = FakeTimeSource { now: 0 };
        let filesystem = FakeFileSystem { has_system_updater: true };

        apply_system_update_and_reboot(
            ACTIVE_SYSTEM_IMAGE_MERKLE.into(),
            NEW_SYSTEM_IMAGE_MERKLE.into(),
            &mut component_runner,
            Initiator::User,
            &time_source,
            &filesystem,
            reboot_test_state.reboot_service_proxy,
        )
        .await
        .unwrap();

        assert_eq!(reboot_test_state.reboot_service_call_count.load(Ordering::SeqCst), 1);
    }

    // A component runner which fails every call made to it.
    // Useful for making the "run system updater" step fail.
    struct FailingComponentRunner;
    impl ComponentRunner for FailingComponentRunner {
        fn run_until_exit(
            &mut self,
            _url: String,
            _arguments: Option<Vec<String>>,
        ) -> BoxFuture<'_, Result<(), anyhow::Error>> {
            Box::pin(future::err(anyhow!(Error::SystemUpdaterFailed)).boxed())
        }
    }

    // Test that if system updater fails, we don't reboot the system.
    #[fasync::run_singlethreaded(test)]
    async fn test_does_not_reboot_on_failure() {
        let reboot_test_state = setup_reboot_test(false);

        let mut component_runner = FailingComponentRunner;
        let time_source = FakeTimeSource { now: 0 };
        let filesystem = FakeFileSystem { has_system_updater: true };

        let update_result = apply_system_update_and_reboot(
            ACTIVE_SYSTEM_IMAGE_MERKLE.into(),
            NEW_SYSTEM_IMAGE_MERKLE.into(),
            &mut component_runner,
            Initiator::User,
            &time_source,
            &filesystem,
            reboot_test_state.reboot_service_proxy,
        )
        .await;

        assert_matches!(
            update_result.unwrap_err().downcast::<Error>().unwrap(),
            Error::SystemUpdaterFailed
        );
        assert_eq!(reboot_test_state.reboot_service_call_count.load(Ordering::SeqCst), 0);
    }

    // Test that if the reboot service isn't working, we surface the appropriate error after updating.
    // This would be a bad state to be in, but at least a user would get output.
    #[fasync::run_singlethreaded(test)]
    async fn test_reboot_errors_on_no_service() {
        let reboot_test_state = setup_reboot_test(true);

        let mut component_runner = DoNothingComponentRunner;
        let time_source = FakeTimeSource { now: 0 };
        let filesystem = FakeFileSystem { has_system_updater: true };

        let update_result = apply_system_update_and_reboot(
            ACTIVE_SYSTEM_IMAGE_MERKLE.into(),
            NEW_SYSTEM_IMAGE_MERKLE.into(),
            &mut component_runner,
            Initiator::User,
            &time_source,
            &filesystem,
            reboot_test_state.reboot_service_proxy,
        )
        .await;

        // We should have errored out on calling system_updater, but should have
        // called the reboot API.
        assert_matches!(
            update_result.err().expect("system update should fail").downcast::<Error>().unwrap(),
            Error::RebootFailed(zx::Status::PEER_CLOSED)
        );
        assert_eq!(reboot_test_state.reboot_service_call_count.load(Ordering::SeqCst), 1);
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
            let filesystem = FakeFileSystem { has_system_updater: false };

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

            prop_assert!(result.is_ok(), "apply_system_update_impl failed: {:?}", result);
            prop_assert_eq!(
                component_runner.captured_args,
                vec![Args {
                    url: SYSTEM_UPDATER_RESOURCE_URL.to_string(),
                    arguments: Some(vec![
                        "--initiator",
                        initiator_cli_arg(initiator),
                        "--start",
                        &format!("{}",start_time),
                        "--source",
                        &source_merkle.to_lowercase(),
                        "--target",
                        &target_merkle.to_lowercase(),
                        "--reboot",
                        "false",
                        "--oneshot",
                        "true",
                    ]
                    .iter()
                    .map(|s| s.to_string())
                    .collect())
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
    use matches::assert_matches;

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
        assert_matches!(
            run_res.err().expect("run should fail").downcast::<Error>().unwrap(),
            Error::SystemUpdaterFailed
        );
    }
}
