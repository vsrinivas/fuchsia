// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_manager_lib::{klog, model::AbsoluteMoniker, startup},
    failure::{Error, ResultExt},
    fuchsia_async as fasync,
    futures::prelude::*,
    log::*,
    std::process,
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
            Ok(()) => {
                // TODO: Exit the component manager when the root component's binding is lost
                // (when it terminates) or perhaps attempt to rebind automatically.
                // For now, the component manager just runs forever.
                future::pending::<()>().await
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

async fn run_root(args: startup::Arguments) -> Result<(), Error> {
    let hub = startup::create_hub_if_possible(args.root_component_url.clone())
        .await
        .context("failed to create hub")?;
    let model = startup::model_setup(&args, hub.hooks()).await.context("failed to set up model")?;
    model
        .look_up_and_bind_instance(AbsoluteMoniker::root())
        .await
        .map_err(|e| Error::from(e))
        .context(format!("failed to bind to root component {}", args.root_component_url))?;
    Ok(())
}
