// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
#![cfg(test)]
use {
    failure::{bail, Error},
    fidl_fuchsia_pkg::PackageResolverRequestStream,
    fidl_fuchsia_sys::{LauncherProxy, TerminationReason},
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{AppBuilder, Stdio},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_zircon::Status,
    futures::prelude::*,
    parking_lot::Mutex,
    std::{fs::create_dir, fs::File, path::PathBuf, sync::Arc},
    tempfile::TempDir,
};

struct TestEnv {
    env: NestedEnvironment,
    resolver: Arc<MockResolverService>,
    reboot_service: Arc<MockRebootService>,
    logger_factory: Arc<MockLoggerFactory>,
    _test_dir: TempDir,
    update_path: PathBuf,
    blobfs_path: PathBuf,
}

impl TestEnv {
    fn launcher(&self) -> &LauncherProxy {
        self.env.launcher()
    }

    fn new() -> Self {
        let mut fs = ServiceFs::new();
        let resolver = Arc::new(MockResolverService::new());
        let resolver_clone = resolver.clone();
        fs.add_fidl_service(move |stream: PackageResolverRequestStream| {
            let resolver_clone = resolver_clone.clone();
            fasync::spawn(
                resolver_clone
                    .run_resolver_service(stream)
                    .unwrap_or_else(|e| eprintln!("error running resolver service: {:?}", e)),
            )
        });
        let reboot_service = Arc::new(MockRebootService::new());
        let reboot_service_clone = reboot_service.clone();
        fs.add_fidl_service(move |stream| {
            let reboot_service_clone = reboot_service_clone.clone();
            fasync::spawn(
                reboot_service_clone
                    .run_reboot_service(stream)
                    .unwrap_or_else(|e| eprintln!("error running reboot service: {:?}", e)),
            )
        });
        let logger_factory = Arc::new(MockLoggerFactory::new());
        let logger_factory_cloned = logger_factory.clone();
        fs.add_fidl_service(move |stream| {
            let logger_factory_cloned = logger_factory_cloned.clone();
            fasync::spawn(
                logger_factory_cloned
                    .run_logger_factory(stream)
                    .unwrap_or_else(|e| eprintln!("error running logger factory: {:?}", e)),
            )
        });
        let env = fs
            .create_salted_nested_environment("systemupdater_env")
            .expect("nested environment to create successfully");
        fasync::spawn(fs.collect());

        let test_dir = TempDir::new().expect("create test tempdir");

        let blobfs_path = test_dir.path().join("blob");
        create_dir(&blobfs_path).expect("create blob dir");

        let update_path = test_dir.path().join("update");
        create_dir(&update_path).expect("create update pkg dir");

        Self {
            env,
            resolver,
            reboot_service,
            logger_factory,
            _test_dir: test_dir,
            update_path,
            blobfs_path,
        }
    }

    async fn run_system_updater<'a>(&'a self, args: SystemUpdaterArgs<'a>) {
        let launcher = self.launcher();
        let argv = ["-initiator", args.initiator, "-target", args.target];
        let blobfs_dir = File::open(&self.blobfs_path).expect("open blob dir");
        let update_dir = File::open(&self.update_path).expect("open update pkg dir");

        let system_updater = AppBuilder::new(
            "fuchsia-pkg://fuchsia.com/systemupdater-tests#meta/system_updater_isolated.cmx",
        )
        .args(argv.iter().map(|s| *s))
        .add_dir_to_namespace("/blob".to_string(), blobfs_dir)
        .expect("/blob to mount")
        .add_dir_to_namespace("/pkgfs/packages/update/0".to_string(), update_dir)
        .expect("/pkgfs/packages/update/0 to mount")
        .stderr(Stdio::MakePipe)
        .spawn(launcher)
        .expect("system_updater to launch");

        let output =
            await!(system_updater.wait_with_output()).expect("no errors while waiting for exit");

        assert_eq!(output.exit_status.reason(), TerminationReason::Exited);
        println!(
            "system_updater {:?} exited with {}. logs:\n{}\n",
            argv,
            output.exit_status.code(),
            String::from_utf8_lossy(&output.stderr)
        );
        assert!(
            output.exit_status.success(),
            "system_updater {:?} exited with {}",
            argv,
            output.exit_status.code(),
        );
    }
}

struct SystemUpdaterArgs<'a> {
    initiator: &'a str,
    target: &'a str,
}

struct MockResolverService {
    resolved_uris: Mutex<Vec<String>>,
}

impl MockResolverService {
    fn new() -> Self {
        Self { resolved_uris: Mutex::new(vec![]) }
    }
    async fn run_resolver_service(
        self: Arc<Self>,
        mut stream: PackageResolverRequestStream,
    ) -> Result<(), Error> {
        while let Some(event) = await!(stream.try_next())? {
            let fidl_fuchsia_pkg::PackageResolverRequest::Resolve {
                package_uri,
                selectors: _,
                update_policy: _,
                dir: _,
                responder,
            } = event;
            eprintln!("TEST: Got resolve request for {:?}", package_uri);
            self.resolved_uris.lock().push(package_uri);
            responder.send(Status::OK.into_raw())?;
        }

        Ok(())
    }
}

struct MockRebootService {
    called: Mutex<u32>,
}
impl MockRebootService {
    fn new() -> Self {
        Self { called: Mutex::new(0) }
    }

    async fn run_reboot_service(
        self: Arc<Self>,
        mut stream: fidl_fuchsia_device_manager::AdministratorRequestStream,
    ) -> Result<(), Error> {
        while let Some(event) = await!(stream.try_next())? {
            let fidl_fuchsia_device_manager::AdministratorRequest::Suspend { flags, responder } =
                event;
            eprintln!("TEST: Got reboot request with flags {:?}", flags);
            *self.called.lock() += 1;
            responder.send(Status::OK.into_raw())?;
        }

        Ok(())
    }
}

#[derive(Clone)]
struct CustomEvent {
    metric_id: u32,
    values: Vec<fidl_fuchsia_cobalt::CustomEventValue>,
}

struct MockLogger {
    custom_events: Mutex<Vec<CustomEvent>>,
}

impl MockLogger {
    fn new() -> Self {
        Self { custom_events: Mutex::new(vec![]) }
    }

    async fn run_logger(
        self: Arc<Self>,
        mut stream: fidl_fuchsia_cobalt::LoggerRequestStream,
    ) -> Result<(), Error> {
        while let Some(event) = await!(stream.try_next())? {
            match event {
                fidl_fuchsia_cobalt::LoggerRequest::LogCustomEvent {
                    metric_id,
                    event_values,
                    responder,
                } => {
                    eprintln!("TEST: Got Logger request with metric_id {:?}", metric_id);
                    self.custom_events.lock().push(CustomEvent { metric_id, values: event_values });
                    responder.send(fidl_fuchsia_cobalt::Status::Ok)?;
                }
                _ => {
                    bail!("unhandled Logger method {:?}", event);
                }
            }
        }

        Ok(())
    }
}

struct MockLoggerFactory {
    loggers: Mutex<Vec<Arc<MockLogger>>>,
}

impl MockLoggerFactory {
    fn new() -> Self {
        Self { loggers: Mutex::new(vec![]) }
    }

    async fn run_logger_factory(
        self: Arc<Self>,
        mut stream: fidl_fuchsia_cobalt::LoggerFactoryRequestStream,
    ) -> Result<(), Error> {
        while let Some(event) = await!(stream.try_next())? {
            match event {
                fidl_fuchsia_cobalt::LoggerFactoryRequest::CreateLogger {
                    profile,
                    logger,
                    responder,
                } => {
                    eprintln!("TEST: Got CreateLogger request with profile {:?}", profile);
                    let mock_logger = Arc::new(MockLogger::new());
                    self.loggers.lock().push(mock_logger.clone());
                    fasync::spawn(
                        mock_logger
                            .run_logger(logger.into_stream()?)
                            .unwrap_or_else(|e| eprintln!("error while running Logger: {:?}", e)),
                    );
                    responder.send(fidl_fuchsia_cobalt::Status::Ok)?;
                }
                _ => {
                    bail!("unhandled LoggerFactory method: {:?}", event);
                }
            }
        }

        Ok(())
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_system_update() {
    let env = TestEnv::new();

    std::fs::write(
        env.update_path.join("packages"),
        "system_image/0=42ade6f4fd51636f70c68811228b4271ed52c4eb9a647305123b4f4d0741f296\n",
    )
    .expect("create update/packages");

    await!(env.run_system_updater(SystemUpdaterArgs { initiator: "manual", target: "m3rk13" }));

    assert_eq!(*env.resolver.resolved_uris.lock(), vec![
        "fuchsia-pkg://fuchsia.com/system_image/0?hash=42ade6f4fd51636f70c68811228b4271ed52c4eb9a647305123b4f4d0741f296"
    ]);

    let loggers = env.logger_factory.loggers.lock().clone();
    // NOTE(rudominer) Logging to Cobalt from Amber has temporarily been disabled.
    // It will be re-enabled when https://fuchsia-review.googlesource.com/c/fuchsia/+/272921
    // is landed. This is being tracked by PKG-723.
    assert_eq!(loggers.len(), 0);

    assert_eq!(*env.reboot_service.called.lock(), 1);
}
