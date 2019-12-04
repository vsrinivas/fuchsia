// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO Follow 2018 idioms
#![allow(elided_lifetimes_in_paths)]

use {
    component_manager_lib::{
        builtin_environment::BuiltinEnvironment,
        klog,
        model::{AbsoluteMoniker, Binder, ComponentManagerConfig, Model},
        startup,
    },
    failure::{Error, ResultExt},
    fuchsia_async as fasync,
    log::*,
    std::{panic, process, sync::Arc},
};

const NUM_THREADS: usize = 2;

fn main() -> Result<(), Error> {
    // Make sure we exit if there is a panic. Add this hook before we init the
    // KernelLogger because it installs its own hook and then calls any
    // existing hook.
    panic::set_hook(Box::new(|_| {
        println!("Panic in component_manager, aborting process.");
        process::abort();
    }));
    klog::KernelLogger::init();
    let args = match startup::Arguments::from_args() {
        Ok(args) => args,
        Err(err) => {
            error!("{}\n{}", err, startup::Arguments::usage());
            return Err(err);
        }
    };

    info!("Component manager is starting up...");

    let mut executor = fasync::Executor::new().context("error creating executor")?;

    let fut = async {
        match run_root(args).await {
            Ok((model, _builtin_environment)) => {
                model.wait_for_root_realm_stop().await;
            }
            Err(err) => {
                panic!("Component manager setup failed: {:?}", err);
            }
        }
    };
    executor.run(fut, NUM_THREADS);

    Ok(())
}

async fn run_root(args: startup::Arguments) -> Result<(Arc<Model>, BuiltinEnvironment), Error> {
    let model = startup::model_setup(&args).await.context("failed to set up model")?;
    let builtin_environment =
        BuiltinEnvironment::new(&args, &model, ComponentManagerConfig::default()).await?;
    builtin_environment.bind_service_fs_to_out(&model).await?;

    let root_moniker = AbsoluteMoniker::root();
    model
        .bind(&root_moniker)
        .await
        .map_err(|e| Error::from(e))
        .context(format!("failed to bind to root component {}", args.root_component_url))?;
    Ok((model, builtin_environment))
}
