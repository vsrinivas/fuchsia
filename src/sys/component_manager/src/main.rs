// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_manager_lib::{
        klog,
        model::{AbsoluteMoniker, BuiltinEnvironment, ComponentManagerConfig, Model},
        startup,
    },
    failure::{Error, ResultExt},
    fuchsia_async as fasync,
    log::*,
    std::{process, sync::Arc},
};

const NUM_THREADS: usize = 2;

fn main() -> Result<(), Error> {
    klog::KernelLogger::init().expect("Failed to initialize logger");
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
                model.wait_for_root_realm_destroy().await;
            }
            Err(err) => {
                error!("Component manager setup failed: {:?}", err);
                process::exit(1)
            }
        }
    };
    executor.run(fut, NUM_THREADS);

    Ok(())
}

async fn run_root(args: startup::Arguments) -> Result<(Arc<Model>, BuiltinEnvironment), Error> {
    let model = startup::model_setup(&args).await.context("failed to set up model")?;
    let builtin_environment =
        startup::builtin_environment_setup(&args, &model, ComponentManagerConfig::default())
            .await?;
    builtin_environment.bind_hub_to_outgoing_dir(&model).await?;
    let root_moniker = AbsoluteMoniker::root();
    model
        .bind(&root_moniker)
        .await
        .map_err(|e| Error::from(e))
        .context(format!("failed to bind to root component {}", args.root_component_url))?;
    Ok((model, builtin_environment))
}
