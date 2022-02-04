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
    fuchsia_syslog as syslog, fuchsia_trace_provider as trace_provider,
    fuchsia_zircon::JobCriticalOptions,
    log::*,
    std::path::PathBuf,
    std::{panic, process, thread, time::Duration},
};

fn main() {
    // Make sure we exit if there is a panic. Add this hook before we init the
    // KernelLogger because it installs its own hook and then calls any
    // existing hook.
    panic::set_hook(Box::new(|info| {
        println!("Panic in component_manager, aborting process. Message: {}", info);
        // TODO remove after 43671 is resolved
        std::thread::spawn(move || {
            let mut nap_duration = Duration::from_secs(1);
            // Do a short sleep, hopefully under "normal" circumstances the
            // process will exit before this is printed
            thread::sleep(nap_duration);
            println!("component manager abort was started");
            // set a fairly long duration so we don't spam logs
            nap_duration = Duration::from_secs(30);
            loop {
                thread::sleep(nap_duration);
                println!("component manager alive long after abort");
            }
        });
        process::abort();
    }));

    // Set ourselves as critical to our job. If we do not fail gracefully, our
    // job will be killed.
    if let Err(err) =
        job_default().set_critical(JobCriticalOptions::RETCODE_NONZERO, &process_self())
    {
        panic!("Component manager failed to set itself as critical: {}", err);
    }

    // Enable tracing in Component Manager
    trace_provider::trace_provider_create_with_fdio();

    let (runtime_config, bootfs_svc) = build_runtime_config();

    match runtime_config.log_destination {
        finternal::LogDestination::Syslog => {
            syslog::init().expect("failed to init syslog");
        }
        finternal::LogDestination::Klog => {
            klog::KernelLogger::init();
        }
    }

    info!("Component manager is starting up...");

    let num_threads = runtime_config.num_threads;

    let fut = async move {
        let mut builtin_environment = match build_environment(runtime_config, bootfs_svc).await {
            Ok(environment) => environment,
            Err(error) => {
                error!("Component manager setup failed: {}", error);
                process::exit(1);
            }
        };

        if let Err(error) = builtin_environment.run_root().await {
            error!("Failed to start root component: {}", error);
            process::exit(1);
        }
    };

    let mut executor = fasync::SendExecutor::new(num_threads).expect("error creating executor");
    executor.run(fut);
}

/// Loads component_manager's config.
///
/// This function panics on failure because the logger is not initialized yet.
fn build_runtime_config() -> (RuntimeConfig, Option<BootfsSvc>) {
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
        (Some(_url), None) => (config, bootfs_svc),
        (None, Some(url)) => {
            config.root_component_url = Some(url.clone());
            (config, bootfs_svc)
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
