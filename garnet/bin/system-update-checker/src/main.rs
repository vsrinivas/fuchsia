// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

mod errors;

use crate::errors::{Error, ErrorKind};
use failure::ResultExt;
use fidl_fuchsia_io;
use fidl_fuchsia_pkg::{PackageResolverMarker, PackageResolverProxyInterface, UpdatePolicy};
use fidl_fuchsia_sys::{LauncherMarker, LauncherProxy};
use fuchsia_async::{
    self as fasync,
    futures::{future::BoxFuture, FutureExt},
};
use fuchsia_component::client::{connect_to_service, launch};
use fuchsia_merkle::Hash;
use fuchsia_syslog::{fx_log_err, fx_log_info};
use fuchsia_zircon as zx;
use std::io::{self, BufRead};
use std::str::FromStr;

const SYSTEM_UPDATER_RESOURCE_URL: &str = "fuchsia-pkg://fuchsia.com/amber#meta/system_updater.cmx";
const UPDATE_PACKAGE_URL: &str = "fuchsia-pkg://fuchsia.com/update/0";

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["system-update-checker"]).expect("can't init logger");
    check_for_and_apply_system_update("manual")
}

fn check_for_and_apply_system_update(initiator: impl AsRef<str>) -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context(ErrorKind::CreateExecutor)?;
    let package_resolver =
        connect_to_service::<PackageResolverMarker>().context(ErrorKind::ConnectPackageResolver)?;
    let launcher = connect_to_service::<LauncherMarker>().context(ErrorKind::ConnectToLauncher)?;
    executor.run_singlethreaded(check_for_and_apply_system_update_impl(
        &mut RealFileSystem,
        &package_resolver,
        &RealServiceConnector,
        &mut RealComponentRunner { launcher_proxy: launcher },
        initiator.as_ref(),
        &RealTimeSource,
    ))
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

// For mocking component launching
trait ComponentRunner {
    fn run_until_exit(
        &mut self,
        url: String,
        arguments: Option<Vec<String>>,
    ) -> BoxFuture<Result<(), Error>>;
}

struct RealComponentRunner {
    launcher_proxy: LauncherProxy,
}

impl ComponentRunner for RealComponentRunner {
    fn run_until_exit(
        &mut self,
        url: String,
        arguments: Option<Vec<String>>,
    ) -> BoxFuture<Result<(), Error>> {
        let app_res = launch(&self.launcher_proxy, url, arguments);
        async move {
            let mut app = app_res.context(ErrorKind::LaunchSystemUpdater)?;
            let exit_status = await!(app.wait()).context(ErrorKind::WaitForSystemUpdater)?;
            exit_status.ok().context(ErrorKind::SystemUpdaterFailed)?;
            Ok(())
        }
            .boxed()
    }
}

// For mocking
trait FileSystem {
    fn read_to_string(&self, path: &str) -> io::Result<String>;
    fn remove_file(&mut self, path: &str) -> io::Result<()>;
}

struct RealFileSystem;

impl FileSystem for RealFileSystem {
    fn read_to_string(&self, path: &str) -> io::Result<String> {
        std::fs::read_to_string(path)
    }
    fn remove_file(&mut self, path: &str) -> io::Result<()> {
        std::fs::remove_file(path)
    }
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

async fn check_for_and_apply_system_update_impl<'a>(
    file_system: &'a mut impl FileSystem,
    package_resolver: &'a impl PackageResolverProxyInterface,
    service_connector: &'a impl ServiceConnector,
    component_runner: &'a mut impl ComponentRunner,
    initiator: &'a str,
    time_source: &'a impl TimeSource,
) -> Result<(), Error> {
    let current = current_system_image_merkle(file_system)?;
    fx_log_info!("current system_image merkle: {}", current);
    let latest = await!(latest_system_image_merkle(package_resolver))?;
    if current == latest {
        fx_log_info!("system_image is already up-to-date");
        Ok(())
    } else {
        fx_log_info!("new system_image available: {}", latest);
        if let Err(err) = await!(pkgfs_gc(service_connector)) {
            fx_log_err!(
                "failed to garbage collect pkgfs, will still attempt system update: {}",
                err
            );
        }
        fx_log_info!("starting system_updater");
        await!(component_runner.run_until_exit(
            SYSTEM_UPDATER_RESOURCE_URL.to_string(),
            Some(vec![
                format!("-initiator={}", initiator),
                format!("-start={}", time_source.get_nanos()),
                format!("-source={}", current),
                format!("-target={}", latest)
            ])
        ))?;
        Err(ErrorKind::SystemUpdaterFinished)?
    }
}

fn current_system_image_merkle(file_system: &impl FileSystem) -> Result<Hash, Error> {
    Ok(Hash::from_str(
        &file_system.read_to_string("/system/meta").context(ErrorKind::ReadSystemMeta)?,
    )
    .context(ErrorKind::ParseSystemMeta)?)
}

async fn latest_system_image_merkle(
    package_resolver: &impl PackageResolverProxyInterface,
) -> Result<Hash, Error> {
    let (dir_proxy, dir_server_end) =
        fidl::endpoints::create_proxy().context(ErrorKind::CreateUpdatePackageDirectoryProxy)?;
    let status = await!(package_resolver.resolve(
        &UPDATE_PACKAGE_URL,
        &mut vec![].into_iter(),
        &mut UpdatePolicy { fetch_if_absent: true, allow_old_versions: false },
        dir_server_end
    ))
    .context(ErrorKind::ResolveUpdatePackageFidl)?;
    zx::Status::ok(status).context(ErrorKind::ResolveUpdatePackage)?;

    let (file_end, file_server_end) = fidl::endpoints::create_endpoints()
        .context(ErrorKind::CreateUpdatePackagePackagesEndpoint)?;
    dir_proxy
        .open(
            fidl_fuchsia_io::OPEN_FLAG_NOT_DIRECTORY | fidl_fuchsia_io::OPEN_RIGHT_READABLE,
            0,
            "packages",
            file_server_end,
        )
        .context(ErrorKind::OpenUpdatePackagePackages)?;

    // danger: synchronous io in async
    let packages_file =
        fdio::create_fd(file_end.into_channel().into()).context(ErrorKind::CreatePackagesFd)?;
    extract_system_image_merkle_from_update_packages(packages_file)
}

fn extract_system_image_merkle_from_update_packages(reader: impl io::Read) -> Result<Hash, Error> {
    for line in io::BufReader::new(reader).lines() {
        let line = line.context(ErrorKind::ReadPackages)?;
        if let Some(i) = line.rfind('=') {
            let (key, value) = line.split_at(i + 1);
            if key == "system_image/0=" {
                return Ok(Hash::from_str(value)
                    .context(ErrorKind::ParseLatestSystemImageMerkle { packages_entry: line })?);
            }
        }
    }
    Err(ErrorKind::MissingLatestSystemImageMerkle)?
}

async fn pkgfs_gc(service_connector: &impl ServiceConnector) -> Result<(), Error> {
    fx_log_info!("triggering pkgfs GC");
    let (dir_end, dir_server_end) =
        fidl::endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>()
            .context(ErrorKind::PkgfsGc)?;
    service_connector
        .service_connect("/pkgfs", dir_server_end.into_channel())
        .context(ErrorKind::PkgfsGc)?;
    let dir_proxy = fidl_fuchsia_io::DirectoryProxy::new(
        fasync::Channel::from_channel(dir_end.into_channel()).context(ErrorKind::PkgfsGc)?,
    );
    let status = await!(dir_proxy.unlink("garbage")).context(ErrorKind::PkgfsGc)?;
    zx::Status::ok(status).context(ErrorKind::PkgfsGc)?;
    Ok(())
}

#[cfg(test)]
mod test_check_for_and_apply_system_update_impl {
    use super::*;
    use fuchsia_async::futures::future;
    use maplit::hashmap;
    use proptest::prelude::*;
    use std::collections::hash_map::HashMap;
    use std::fs;
    use std::io::Write;

    const ACTIVE_SYSTEM_IMAGE_MERKLE: &str =
        "0000000000000000000000000000000000000000000000000000000000000000";
    const NEW_SYSTEM_IMAGE_MERKLE: &str =
        "1111111111111111111111111111111111111111111111111111111111111111";

    struct FakeFileSystem {
        contents: HashMap<String, String>,
    }
    impl FakeFileSystem {
        fn new_with_valid_system_meta() -> FakeFileSystem {
            FakeFileSystem {
                contents: hashmap![
                    "/system/meta".to_string() => ACTIVE_SYSTEM_IMAGE_MERKLE.to_string()
                ],
            }
        }
        fn new_with_valid_system_meta_and_active_merkle(active_merkle: &str) -> FakeFileSystem {
            FakeFileSystem {
                contents: hashmap![
                    "/system/meta".to_string() => active_merkle.to_string(),
                ],
            }
        }
    }
    impl FileSystem for FakeFileSystem {
        fn read_to_string(&self, path: &str) -> io::Result<String> {
            self.contents
                .get(path)
                .ok_or(io::Error::new(
                    io::ErrorKind::NotFound,
                    format!("not present in fake file system: {}", path),
                ))
                .map(|s| s.to_string())
        }
        fn remove_file(&mut self, path: &str) -> io::Result<()> {
            self.contents.remove(path).and(Some(())).ok_or(io::Error::new(
                io::ErrorKind::NotFound,
                format!("fake file system cannot remove non-existent file: {}", path),
            ))
        }
    }

    struct PackageResolverProxyTempDir {
        temp_dir: tempfile::TempDir,
    }
    impl PackageResolverProxyTempDir {
        fn new_with_empty_dir() -> PackageResolverProxyTempDir {
            PackageResolverProxyTempDir { temp_dir: tempfile::tempdir().expect("create temp dir") }
        }
        fn new_with_empty_packages_file() -> PackageResolverProxyTempDir {
            let temp_dir = tempfile::tempdir().expect("create temp dir");
            fs::File::create(format!(
                "{}/packages",
                temp_dir.path().to_str().expect("path is utf8")
            ))
            .expect("create empty packages file");
            PackageResolverProxyTempDir { temp_dir }
        }
        fn new_with_latest_system_image_merkle(merkle: &str) -> PackageResolverProxyTempDir {
            let temp_dir = tempfile::tempdir().expect("create temp dir");
            let mut packages_file = fs::File::create(format!(
                "{}/packages",
                temp_dir.path().to_str().expect("path is utf8")
            ))
            .expect("create empty packages file");
            write!(&mut packages_file, "system_image/0={}\n", merkle)
                .expect("write to package file");
            PackageResolverProxyTempDir { temp_dir }
        }
    }
    impl PackageResolverProxyInterface for PackageResolverProxyTempDir {
        type ResolveResponseFut = future::Ready<Result<i32, fidl::Error>>;
        fn resolve(
            &self,
            package_url: &str,
            selectors: &mut dyn ExactSizeIterator<Item = &str>,
            update_policy: &mut UpdatePolicy,
            dir: fidl::endpoints::ServerEnd<fidl_fuchsia_io::DirectoryMarker>,
        ) -> Self::ResolveResponseFut {
            assert_eq!(package_url, UPDATE_PACKAGE_URL);
            assert_eq!(selectors.len(), 0);
            assert_eq!(
                update_policy,
                &UpdatePolicy { fetch_if_absent: true, allow_old_versions: false }
            );
            fdio::service_connect(
                self.temp_dir.path().to_str().expect("path is utf8"),
                dir.into_channel(),
            )
            .unwrap();
            future::ok(zx::sys::ZX_OK)
        }
    }

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
            fs::File::create(pkgfs.join("garbage")).expect("create garbage file");
            service_connector
        }
    }
    impl TempDirServiceConnector {
        fn has_garbage_file(&self) -> bool {
            self.temp_dir.path().join("pkgfs/garbage").exists()
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
        ) -> BoxFuture<Result<(), Error>> {
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
        ) -> BoxFuture<Result<(), Error>> {
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
    async fn test_missing_system_meta_file() {
        let mut file_system = FakeFileSystem { contents: hashmap![] };
        let package_resolver = PackageResolverProxyTempDir::new_with_empty_dir();
        let service_connector = TempDirServiceConnector::new();
        let mut component_runner = DoNothingComponentRunner;
        let time_source = FakeTimeSource { now: 0 };

        let result = await!(check_for_and_apply_system_update_impl(
            &mut file_system,
            &package_resolver,
            &service_connector,
            &mut component_runner,
            "test-initiator",
            &time_source,
        ));

        assert!(result.is_err());
        assert_eq!(result.err().unwrap().kind(), ErrorKind::ReadSystemMeta);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_malformatted_system_meta_file() {
        let mut file_system = FakeFileSystem {
            contents: hashmap![
                "/system/meta".to_string() => "not-a-merkle".to_string()
            ],
        };
        let package_resolver = PackageResolverProxyTempDir::new_with_empty_dir();
        let service_connector = TempDirServiceConnector::new();
        let mut component_runner = DoNothingComponentRunner;
        let time_source = FakeTimeSource { now: 0 };

        let result = await!(check_for_and_apply_system_update_impl(
            &mut file_system,
            &package_resolver,
            &service_connector,
            &mut component_runner,
            "test-initiator",
            &time_source,
        ));

        assert!(result.is_err());
        assert_eq!(result.err().unwrap().kind(), ErrorKind::ParseSystemMeta);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resolve_update_package_fidl_error() {
        struct PackageResolverProxyFidlError;
        impl PackageResolverProxyInterface for PackageResolverProxyFidlError {
            type ResolveResponseFut = future::Ready<Result<i32, fidl::Error>>;
            fn resolve(
                &self,
                _package_url: &str,
                _selectors: &mut dyn ExactSizeIterator<Item = &str>,
                _update_policy: &mut UpdatePolicy,
                _dir: fidl::endpoints::ServerEnd<fidl_fuchsia_io::DirectoryMarker>,
            ) -> Self::ResolveResponseFut {
                future::err(fidl::Error::Invalid)
            }
        }

        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyFidlError;
        let service_connector = TempDirServiceConnector::new();
        let mut component_runner = DoNothingComponentRunner;
        let time_source = FakeTimeSource { now: 0 };

        let result = await!(check_for_and_apply_system_update_impl(
            &mut file_system,
            &package_resolver,
            &service_connector,
            &mut component_runner,
            "test-initiator",
            &time_source,
        ));

        assert!(result.is_err());
        assert_eq!(result.err().unwrap().kind(), ErrorKind::ResolveUpdatePackageFidl);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resolve_update_package_zx_error() {
        struct PackageResolverProxyZxError;
        impl PackageResolverProxyInterface for PackageResolverProxyZxError {
            type ResolveResponseFut = future::Ready<Result<i32, fidl::Error>>;
            fn resolve(
                &self,
                _package_url: &str,
                _selectors: &mut dyn ExactSizeIterator<Item = &str>,
                _update_policy: &mut UpdatePolicy,
                _dir: fidl::endpoints::ServerEnd<fidl_fuchsia_io::DirectoryMarker>,
            ) -> Self::ResolveResponseFut {
                future::ok(zx::sys::ZX_ERR_INTERNAL)
            }
        }

        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyZxError;
        let service_connector = TempDirServiceConnector::new();
        let mut component_runner = DoNothingComponentRunner;
        let time_source = FakeTimeSource { now: 0 };

        let result = await!(check_for_and_apply_system_update_impl(
            &mut file_system,
            &package_resolver,
            &service_connector,
            &mut component_runner,
            "test-initiator",
            &time_source,
        ));

        assert!(result.is_err());
        assert_eq!(result.err().unwrap().kind(), ErrorKind::ResolveUpdatePackage);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_package_missing_packages_file() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_empty_dir();
        let service_connector = TempDirServiceConnector::new();
        let mut component_runner = DoNothingComponentRunner;
        let time_source = FakeTimeSource { now: 0 };

        let result = await!(check_for_and_apply_system_update_impl(
            &mut file_system,
            &package_resolver,
            &service_connector,
            &mut component_runner,
            "test-initiator",
            &time_source,
        ));

        assert!(result.is_err());
        assert_eq!(result.err().unwrap().kind(), ErrorKind::CreatePackagesFd);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_package_empty_packages_file() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_empty_packages_file();
        let service_connector = TempDirServiceConnector::new();
        let mut component_runner = DoNothingComponentRunner;
        let time_source = FakeTimeSource { now: 0 };

        let result = await!(check_for_and_apply_system_update_impl(
            &mut file_system,
            &package_resolver,
            &service_connector,
            &mut component_runner,
            "test-initiator",
            &time_source,
        ));

        assert!(result.is_err());
        assert_eq!(result.err().unwrap().kind(), ErrorKind::MissingLatestSystemImageMerkle);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_package_bad_system_image_merkle() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver =
            PackageResolverProxyTempDir::new_with_latest_system_image_merkle("bad-merkle");
        let service_connector = TempDirServiceConnector::new();
        let mut component_runner = DoNothingComponentRunner;
        let time_source = FakeTimeSource { now: 0 };

        let result = await!(check_for_and_apply_system_update_impl(
            &mut file_system,
            &package_resolver,
            &service_connector,
            &mut component_runner,
            "test-initiator",
            &time_source,
        ));

        assert!(result.is_err());
        assert_eq!(
            result.err().unwrap().kind(),
            ErrorKind::ParseLatestSystemImageMerkle {
                packages_entry: "system_image/0=bad-merkle".to_string()
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_pkgfs_gc_if_system_already_up_to_date() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_latest_system_image_merkle(
            ACTIVE_SYSTEM_IMAGE_MERKLE,
        );
        let service_connector = TempDirServiceConnector::new_with_pkgfs_garbage();
        let mut component_runner = DoNothingComponentRunner;
        let time_source = FakeTimeSource { now: 0 };

        let result = await!(check_for_and_apply_system_update_impl(
            &mut file_system,
            &package_resolver,
            &service_connector,
            &mut component_runner,
            "test-initiator",
            &time_source,
        ));

        assert!(result.is_ok(), "{:?}", result);
        assert!(service_connector.has_garbage_file());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_component_launching_if_system_already_up_to_date() {
        struct PanicingComponentRunner;
        impl ComponentRunner for PanicingComponentRunner {
            fn run_until_exit(
                &mut self,
                _url: String,
                _arguments: Option<Vec<String>>,
            ) -> BoxFuture<Result<(), Error>> {
                panic!("PanicingComponentRunner isn't supposed to be called");
            }
        }

        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_latest_system_image_merkle(
            ACTIVE_SYSTEM_IMAGE_MERKLE,
        );
        let service_connector = TempDirServiceConnector::new();
        let mut component_runner = PanicingComponentRunner;
        let time_source = FakeTimeSource { now: 0 };

        let result = await!(check_for_and_apply_system_update_impl(
            &mut file_system,
            &package_resolver,
            &service_connector,
            &mut component_runner,
            "test-initiator",
            &time_source,
        ));

        assert!(result.is_ok(), "{:?}", result);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_trigger_pkgfs_gc_if_update_available() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_latest_system_image_merkle(
            NEW_SYSTEM_IMAGE_MERKLE,
        );
        let service_connector = TempDirServiceConnector::new_with_pkgfs_garbage();
        let mut component_runner = DoNothingComponentRunner;
        let time_source = FakeTimeSource { now: 0 };
        assert!(service_connector.has_garbage_file());

        let result = await!(check_for_and_apply_system_update_impl(
            &mut file_system,
            &package_resolver,
            &service_connector,
            &mut component_runner,
            "test-initiator",
            &time_source,
        ));

        assert!(result.is_err());
        assert_eq!(result.err().unwrap().kind(), ErrorKind::SystemUpdaterFinished);
        assert!(!service_connector.has_garbage_file());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_launch_system_updater_if_update_available() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_latest_system_image_merkle(
            NEW_SYSTEM_IMAGE_MERKLE,
        );
        let service_connector = TempDirServiceConnector::new();
        let mut component_runner = WasCalledComponentRunner { was_called: false };
        let time_source = FakeTimeSource { now: 0 };

        let result = await!(check_for_and_apply_system_update_impl(
            &mut file_system,
            &package_resolver,
            &service_connector,
            &mut component_runner,
            "test-initiator",
            &time_source,
        ));

        assert!(result.is_err());
        assert_eq!(result.err().unwrap().kind(), ErrorKind::SystemUpdaterFinished);
        assert!(component_runner.was_called);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_launch_system_updater_even_if_gc_fails() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_latest_system_image_merkle(
            NEW_SYSTEM_IMAGE_MERKLE,
        );
        let service_connector = TempDirServiceConnector::new();
        let mut component_runner = WasCalledComponentRunner { was_called: false };
        let time_source = FakeTimeSource { now: 0 };

        let result = await!(check_for_and_apply_system_update_impl(
            &mut file_system,
            &package_resolver,
            &service_connector,
            &mut component_runner,
            "test-initiator",
            &time_source,
        ));

        assert!(result.is_err());
        assert_eq!(result.err().unwrap().kind(), ErrorKind::SystemUpdaterFinished);
        assert!(component_runner.was_called);
    }

    proptest! {
        #[test]
        fn test_values_passed_through_to_component_launcher(
            initiator in ".{1,10}",
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
                ) -> BoxFuture<Result<(), Error>> {
                    self.captured_args.push(Args { url, arguments });
                    future::ok(()).boxed()
                }
            }

            let mut file_system = FakeFileSystem::new_with_valid_system_meta_and_active_merkle(
                &source_merkle);
            let package_resolver =
                PackageResolverProxyTempDir::new_with_latest_system_image_merkle(&target_merkle);
            let service_connector = TempDirServiceConnector::new();
            let mut component_runner = ArgumentCapturingComponentRunner { captured_args: vec![] };
            let time_source = FakeTimeSource { now: start_time };

            let mut executor =
                fasync::Executor::new().expect("create executor in test");
            let result = executor.run_singlethreaded(check_for_and_apply_system_update_impl(
                &mut file_system,
                &package_resolver,
                &service_connector,
                &mut component_runner,
                &initiator,
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
mod test_real_file_system {
    use super::*;
    use proptest::prelude::*;
    use std::fs;
    use std::io::{self, Write};

    #[test]
    fn test_read_to_string_errors_on_missing_file() {
        let dir = tempfile::tempdir().expect("create temp dir");
        let read_res = RealFileSystem.read_to_string(
            dir.path().join("this-file-does-not-exist").to_str().expect("paths are utf8"),
        );
        assert_eq!(read_res.err().expect("read should fail").kind(), io::ErrorKind::NotFound);
    }

    proptest! {
        #[test]
        fn test_read_to_string_preserves_contents(
            contents in ".{0, 65}",
            file_name in "[^\\.\0/]{1,10}",
        ) {
            let dir = tempfile::tempdir().expect("create temp dir");
            let file_path = dir.path().join(file_name);
            let mut file = fs::File::create(&file_path).expect("create file");
            file.write_all(contents.as_bytes()).expect("write the contents");

            let read_contents = RealFileSystem
                .read_to_string(file_path.to_str().expect("paths are utf8"))
                .expect("read the file");

            prop_assert_eq!(read_contents, contents);
        }
    }
}

#[cfg(test)]
mod test_real_service_connector {
    use super::*;
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
        let status = await!(dir_proxy.unlink(file_name)).expect("unlink the file fidl");
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

        let read_dirents_res =
            await!(dir_proxy
                .read_dirents(1000 /*size shouldn't matter, as this should immediately fail*/));

        match read_dirents_res.err().expect("read should fail") {
            fidl::Error::ClientRead(zx_status) => assert_eq!(zx_status, zx::Status::PEER_CLOSED),
            fidl::Error::ClientWrite(zx_status) => assert_eq!(zx_status, zx::Status::PEER_CLOSED),
            err => panic!("unexpected error variant: {}", err),
        }
    }
}

#[cfg(test)]
mod test_real_component_runner {
    use super::*;

    const EXIT_WITH_CODE_RESOURCE_URL: &str =
        "fuchsia-pkg://fuchsia.com/system-update-checker-tests/0#meta/exit-with-code.cmx";

    #[fasync::run_singlethreaded(test)]
    async fn test_run_a_component_that_exits_0() {
        let launcher_proxy = connect_to_service::<LauncherMarker>().expect("connect to launcher");
        let mut runner = RealComponentRunner { launcher_proxy };
        let run_res = await!(runner
            .run_until_exit(EXIT_WITH_CODE_RESOURCE_URL.to_string(), Some(vec!["0".to_string()])));
        assert!(run_res.is_ok(), "{:?}", run_res.err().unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_run_a_component_that_exits_1() {
        let launcher_proxy = connect_to_service::<LauncherMarker>().expect("connect to launcher");
        let mut runner = RealComponentRunner { launcher_proxy };
        let run_res = await!(runner
            .run_until_exit(EXIT_WITH_CODE_RESOURCE_URL.to_string(), Some(vec!["1".to_string()])));
        assert_eq!(run_res.err().expect("run should fail").kind(), ErrorKind::SystemUpdaterFailed);
    }
}
