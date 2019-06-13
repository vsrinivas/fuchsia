// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {
    component_manager_lib::{
        ambient::RealAmbientEnvironment,
        elf_runner::{ElfRunner, ProcessLauncherConnector},
        model::{self, Hub, Model, ModelParams},
        startup,
    },
    failure::{self, Error},
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, NodeMarker, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_vfs_pseudo_fs::directory::{self, entry::DirectoryEntry},
    fuchsia_zircon as zx,
    std::{iter, path::PathBuf, sync::Arc, vec::Vec},
};

async fn read_file<'a>(root_proxy: &'a DirectoryProxy, path: &'a str) -> String {
    let file_proxy =
        io_util::open_file(&root_proxy, &PathBuf::from(path)).expect("Failed to open file.");
    let res = await!(io_util::read_file(&file_proxy));
    res.expect("Unable to read file.")
}

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args = startup::Arguments { use_builtin_process_launcher: false, ..Default::default() };
    let builtin_services = Arc::new(startup::BuiltinRootServices::new(&args)?);
    let launcher_connector = ProcessLauncherConnector::new(&args, builtin_services);
    let runner = ElfRunner::new(launcher_connector);
    let resolver_registry = startup::available_resolvers()?;
    let root_component_url =
        "fuchsia-pkg://fuchsia.com/hub_integration_test#meta/echo_args.cm".to_string();

    let (client_chan, server_chan) = zx::Channel::create().unwrap();
    let mut root_directory = directory::simple::empty();
    root_directory.open(
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        0,
        &mut iter::empty(),
        ServerEnd::<NodeMarker>::new(server_chan.into()),
    );
    let mut hooks: model::Hooks = Vec::new();
    let hub = Arc::new(Hub::new(root_component_url.clone(), root_directory).unwrap());
    hooks.push(hub.clone());

    let params = ModelParams {
        ambient: Box::new(RealAmbientEnvironment::new()),
        root_component_url: root_component_url,
        root_resolver_registry: resolver_registry,
        root_default_runner: Box::new(runner),
        hooks,
    };

    let model = Arc::new(Model::new(params));

    let res = await!(model.look_up_and_bind_instance(model::AbsoluteMoniker::root()));
    let expected_res: Result<(), model::ModelError> = Ok(());
    assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));

    let hub_proxy = ClientEnd::<DirectoryMarker>::new(client_chan)
        .into_proxy()
        .expect("failed to create directory proxy");

    // These args are from echo_args.cml.
    assert_eq!("Hippos", await!(read_file(&hub_proxy, "self/exec/runtime/args/0")));
    assert_eq!("rule!", await!(read_file(&hub_proxy, "self/exec/runtime/args/1")));

    Ok(())
}
