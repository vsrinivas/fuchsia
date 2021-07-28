// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO Follow 2018 idioms
#![allow(elided_lifetimes_in_paths)]

use {
    anyhow::Error,
    component_manager_lib::{
        builtin_environment::{BuiltinEnvironment, BuiltinEnvironmentBuilder},
        config::RuntimeConfig,
        klog, startup,
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
    panic::set_hook(Box::new(|_| {
        println!("Panic in component_manager, aborting process.");
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

    let runtime_config = build_runtime_config();

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
        let mut builtin_environment = match build_environment(runtime_config).await {
            Ok(environment) => environment,
            Err(error) => {
                error!("Component manager setup failed: {}", error);
                process::exit(1);
            }
        };

        if let Err(error) = builtin_environment.run_root().await {
            error!("Failed to bind to root component: {}", error);
            process::exit(1);
        }
    };

    let mut executor = fasync::SendExecutor::new(num_threads).expect("error creating executor");
    executor.run(fut);
}

/// Loads component_manager's config.
///
/// This function panics on failure because the logger is not initialized yet.
fn build_runtime_config() -> RuntimeConfig {
    let args = match startup::Arguments::from_args() {
        Ok(args) => args,
        Err(err) => {
            panic!("{}\n{}", err, startup::Arguments::usage());
        }
    };

    let path = PathBuf::from(&args.config);
    let mut config = match RuntimeConfig::load_from_file(&path) {
        Ok(config) => config,
        Err(err) => {
            panic!("Failed to load runtime config: {}", err);
        }
    };

    match (config.root_component_url.as_ref(), args.root_component_url.as_ref()) {
        (Some(_url), None) => config,
        (None, Some(url)) => {
            config.root_component_url = Some(url.clone());
            config
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

async fn build_environment(config: RuntimeConfig) -> Result<BuiltinEnvironment, Error> {
    BuiltinEnvironmentBuilder::new()
        .set_runtime_config(config)
        .create_utc_clock()
        .await?
        .add_elf_runner()?
        .include_namespace_resolvers()
        .build()
        .await
}
