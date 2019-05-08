// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {
    component_manager_lib::{
        elf_runner::ElfRunner,
        klog,
        model::{AbsoluteMoniker, Model, ModelParams},
        startup,
    },
    failure::{self, Error, ResultExt},
    fuchsia_async as fasync,
    futures::prelude::*,
    log::*,
    std::env,
    std::process,
    std::sync::Arc,
};

const NUM_THREADS: usize = 2;

struct Opt {
    pub root_component_uri: String,
}

fn parse_args() -> Result<Opt, Error> {
    let mut args: Vec<String> = env::args().collect();
    if args.len() != 2 {
        println!("Usage: {} <root-component-uri>", &args[0]);
        return Err(failure::err_msg("Invalid arguments"));
    }
    Ok(Opt { root_component_uri: args.remove(1) })
}

fn main() -> Result<(), Error> {
    klog::KernelLogger::init().expect("Failed to initialize logger");
    let opt = parse_args()?;

    info!("Component manager is starting up...");

    let mut executor = fasync::Executor::new().context("error creating executor")?;

    let resolver_registry = startup::available_resolvers()?;
    let params = ModelParams {
        root_component_uri: opt.root_component_uri,
        root_resolver_registry: resolver_registry,
        root_default_runner: Box::new(ElfRunner::new()),
    };

    let model = Arc::new(Model::new(params));

    // TODO: Bring up the hub (backed by the model) before running the root component.
    executor.run(run_root(model), NUM_THREADS);
    Ok(())
}

async fn run_root(model: Arc<Model>) {
    match await!(model.look_up_and_bind_instance(AbsoluteMoniker::root())) {
        Ok(()) => {
            // TODO: Exit the component manager when the root component's binding is lost
            // (when it terminates) or perhaps attempt to rebind automatically.
            // For now, the component manager just runs forever.
            await!(future::empty::<()>())
        }
        Err(error) => {
            error!("Failed to bind to root component: {:?}", error);
            process::exit(1)
        }
    }
}
