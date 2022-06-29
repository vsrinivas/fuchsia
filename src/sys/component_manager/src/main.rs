// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO Follow 2018 idioms
#![allow(elided_lifetimes_in_paths)]

use {
    ::cm_logger::klog,
    ::routing::config::RuntimeConfig,
    anyhow::Error,
    component_manager_lib::{
        bootfs::BootfsSvc,
        builtin_environment::{BuiltinEnvironment, BuiltinEnvironmentBuilder},
        startup,
    },
    fidl_fuchsia_component_internal as finternal, fuchsia_async as fasync,
    fuchsia_runtime::{job_default, process_self},
    fuchsia_zircon::JobCriticalOptions,
    std::path::PathBuf,
    std::{panic, process},
    tracing::{error, info},
};

extern "C" {
    fn dl_set_loader_service(
        handle: fuchsia_zircon::sys::zx_handle_t,
    ) -> fuchsia_zircon::sys::zx_handle_t;
}

fn main() {
    // Set ourselves as critical to our job. If we do not fail gracefully, our
    // job will be killed.
    if let Err(err) =
        job_default().set_critical(JobCriticalOptions::RETCODE_NONZERO, &process_self())
    {
        panic!("Component manager failed to set itself as critical: {}", err);
    }

    // Close any loader service passed to component manager so that the service session can be
    // freed, as component manager won't make use of a loader service such as by calling dlopen.
    // If userboot invoked component manager directly, this service was the only reason userboot
    // continued to run and closing it will let userboot terminate.
    let ldsvc = unsafe {
        fuchsia_zircon::Handle::from_raw(dl_set_loader_service(
            fuchsia_zircon::sys::ZX_HANDLE_INVALID,
        ))
    };
    drop(ldsvc);

    let (runtime_config, bootfs_svc, boot_defaults) = build_runtime_config();
    let mut executor =
        fasync::SendExecutor::new(runtime_config.num_threads).expect("error creating executor");

    match runtime_config.log_destination {
        finternal::LogDestination::Syslog => {
            diagnostics_log::init!();
        }
        finternal::LogDestination::Klog => {
            klog::KernelLogger::init();
        }
    };

    info!("Component manager is starting up...");
    if boot_defaults {
        info!("Component manager was started with boot defaults");
    }

    let run_root_fut = async move {
        let mut builtin_environment = match build_environment(runtime_config, bootfs_svc).await {
            Ok(environment) => environment,
            Err(error) => {
                error!(%error, "Component manager setup failed");
                process::exit(1);
            }
        };

        if let Err(error) = builtin_environment.run_root().await {
            error!(%error, "Failed to start root component");
            process::exit(1);
        }
    };

    executor.run(run_root_fut);
}

/// Loads component_manager's config.
///
/// This function panics on failure because the logger is not initialized yet.
fn build_runtime_config() -> (RuntimeConfig, Option<BootfsSvc>, bool) {
    let args = match startup::Arguments::from_args() {
        Ok(args) => args,
        Err(err) => {
            panic!("{}\n{}", err, startup::Arguments::usage());
        }
    };

    let mut config: RuntimeConfig;
    let mut bootfs_svc: Option<BootfsSvc> = None;
    if args.host_bootfs {
        // The Rust bootfs VFS has not been brought up yet, so to find the component manager's
        // config we must find the config's offset and size in the bootfs VMO, and read from it
        // directly.
        bootfs_svc = Some(BootfsSvc::new().expect("Failed to create Rust bootfs"));
        let canonicalized =
            if args.config.starts_with("/boot/") { &args.config[6..] } else { &args.config };
        let config_bytes =
            match bootfs_svc.as_ref().unwrap().read_config_from_uninitialized_vfs(canonicalized) {
                Ok(config) => config,
                Err(error) => {
                    panic!("Failed to read config from uninitialized vfs with error {}.", error)
                }
            };

        config = match RuntimeConfig::load_from_bytes(&config_bytes) {
            Ok(config) => config,
            Err(err) => {
                panic!("Failed to load runtime config: {}", err)
            }
        };
    } else {
        // This is the legacy path where bootsvc is hosting a C++ bootfs VFS,
        // and component manager can read its config using standard filesystem APIs.
        let path = PathBuf::from(&args.config);
        config = match RuntimeConfig::load_from_file(&path) {
            Ok(config) => config,
            Err(err) => {
                panic!("Failed to load runtime config: {}", err)
            }
        };
    }

    match (config.root_component_url.as_ref(), args.root_component_url.as_ref()) {
        (Some(_url), None) => (config, bootfs_svc, args.boot),
        (None, Some(url)) => {
            config.root_component_url = Some(url.clone());
            (config, bootfs_svc, args.boot)
        }
        (None, None) => {
            panic!(
                "`root_component_url` not provided. This field must be provided either as a \
                command line argument or config file parameter."
            );
        }
        (Some(_), Some(_)) => {
            panic!(
                "`root_component_url` set in two places: as a command line argument \
                and a config file parameter. This field can only be set in one of those places."
            );
        }
    }
}

async fn build_environment(
    config: RuntimeConfig,
    bootfs_svc: Option<BootfsSvc>,
) -> Result<BuiltinEnvironment, Error> {
    BuiltinEnvironmentBuilder::new()
        .set_runtime_config(config)
        .create_utc_clock(&bootfs_svc)
        .await?
        .add_elf_runner()?
        .include_namespace_resolvers()
        .set_bootfs_svc(bootfs_svc)
        .build()
        .await
}
