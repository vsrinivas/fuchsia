// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    component_manager_lib::{
        elf_runner::{ElfRunner, ProcessLauncherConnector},
        framework::RealFrameworkServiceHost,
        model::{
            self,
            testing::test_utils::list_directory,
            AbsoluteMoniker, //self,AbsoluteMoniker
            Hub,
            Model,
            ModelConfig,
            ModelParams,
        },
        startup,
    },
    failure::{self, Error},
    fidl::endpoints::ServiceMarker,
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_io::{DirectoryMarker, NodeMarker, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fidl_fuchsia_test::SuiteMarker,
    fuchsia_component::server::{ServiceFs, ServiceObj},
    fuchsia_syslog as syslog,
    fuchsia_vfs_pseudo_fs::directory::{self, entry::DirectoryEntry},
    fuchsia_zircon as zx,
    futures::prelude::*,
    log::*,
    std::{iter, path::PathBuf, process, sync::Arc, vec::Vec},
};

/// This is a prototype used with ftf to launch v2 tests.
/// This is a temporary workaround till we get around using actual component manager.
/// We will start test as root component and using hub v2 attach its "expose/svc" directory to this
/// managers out/svc(v1 version) directory.

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["component_manager_for_test"])?;
    let args = match startup::Arguments::from_args() {
        Ok(args) => args,
        Err(err) => {
            error!("{}\n{}", err, startup::Arguments::usage());
            return Err(err);
        }
    };

    info!("Component manager for test is starting up...");

    let resolver_registry = startup::available_resolvers()?;
    let builtin_services = Arc::new(startup::BuiltinRootServices::new(&args)?);
    let launcher_connector = ProcessLauncherConnector::new(&args, builtin_services);

    let (client_chan, server_chan) = zx::Channel::create().unwrap();
    let mut root_directory = directory::simple::empty();
    root_directory.open(
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        0,
        &mut iter::empty(),
        ServerEnd::<NodeMarker>::new(server_chan.into()),
    );
    let mut hooks: model::Hooks = Vec::new();
    let hub = Arc::new(Hub::new(args.root_component_url.clone(), root_directory).unwrap());
    hooks.push(hub.clone());

    let params = ModelParams {
        framework_services: Arc::new(RealFrameworkServiceHost::new()),
        root_component_url: args.root_component_url,
        root_resolver_registry: resolver_registry,
        root_default_runner: Arc::new(ElfRunner::new(launcher_connector)),
        config: ModelConfig::default(),
        hooks,
    };

    let model = Arc::new(Model::new(params));

    match model.look_up_and_bind_instance(AbsoluteMoniker::root()).await {
        Ok(()) => {
            // TODO: Exit the component manager when the root component's binding is lost
            // (when it terminates).
        }
        Err(error) => {
            error!("Failed to bind to root component: {:?}", error);
            process::exit(1)
        }
    }

    let hub_proxy = ClientEnd::<DirectoryMarker>::new(client_chan)
        .into_proxy()
        .expect("failed to create directory proxy");

    // make sure root component exposes test suite protocol.
    let expose_dir_proxy = io_util::open_directory(
        &hub_proxy,
        &PathBuf::from("self/exec/expose/svc"),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
    )
    .expect("Failed to open directory");

    assert_eq!(vec![SuiteMarker::DEBUG_NAME], list_directory(&expose_dir_proxy).await);

    // bind expose/svc to out/svc of this v1 component.
    let mut fs = ServiceFs::<ServiceObj<'_, ()>>::new();
    fs.add_remote("svc", expose_dir_proxy);

    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}
