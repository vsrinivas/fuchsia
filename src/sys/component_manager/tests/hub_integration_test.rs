// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {
    component_manager_lib::{
        elf_runner::{ElfRunner, ProcessLauncherConnector},
        framework::RealFrameworkServiceHost,
        model::{
            self,
            testing::test_utils::{list_directory, read_file},
            Hub, Model, ModelParams,
        },
        startup,
    },
    failure::{self, Error},
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fidl_examples_routing_echo as fecho,
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, NodeMarker, MODE_TYPE_SERVICE, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    fuchsia_vfs_pseudo_fs::directory::{self, entry::DirectoryEntry},
    fuchsia_zircon as zx,
    std::{iter, path::PathBuf, sync::Arc, vec::Vec},
};

async fn connect_to_echo_service(hub_proxy: &DirectoryProxy, echo_service_path: String) {
    let node_proxy = io_util::open_node(
        &hub_proxy,
        &PathBuf::from(echo_service_path),
        io_util::OPEN_RIGHT_READABLE,
        MODE_TYPE_SERVICE,
    )
    .expect("failed to open echo service");
    let echo_proxy = fecho::EchoProxy::new(node_proxy.into_channel().unwrap());
    let res = await!(echo_proxy.echo_string(Some("hippos")));
    assert_eq!(res.expect("failed to use echo service"), Some("hippos".to_string()));
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test() -> Result<(), Error> {
    let args = startup::Arguments { use_builtin_process_launcher: false, ..Default::default() };
    let builtin_services = Arc::new(startup::BuiltinRootServices::new(&args)?);
    let launcher_connector = ProcessLauncherConnector::new(&args, builtin_services);
    let runner = ElfRunner::new(launcher_connector);
    let resolver_registry = startup::available_resolvers()?;
    let root_component_url =
        "fuchsia-pkg://fuchsia.com/hub_integration_test#meta/echo_realm.cm".to_string();

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
    let hub_test_hook = Arc::new(hub_test_hook::HubTestHook::new());
    hooks.push(hub.clone());
    hooks.push(hub_test_hook.clone());

    let params = ModelParams {
        framework_services: Arc::new(RealFrameworkServiceHost::new()),
        root_component_url: root_component_url,
        root_resolver_registry: resolver_registry,
        root_default_runner: Arc::new(runner),
        hooks,
        config: model::ModelConfig::default(),
    };

    let model = Arc::new(Model::new(params));

    let res = await!(model.look_up_and_bind_instance(model::AbsoluteMoniker::root()));
    let expected_res: Result<(), model::ModelError> = Ok(());
    assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));

    let hub_proxy = ClientEnd::<DirectoryMarker>::new(client_chan)
        .into_proxy()
        .expect("failed to create directory proxy");

    // Verify that echo_realm has two children.
    let children_dir_proxy = io_util::open_directory(
        &hub_proxy,
        &PathBuf::from("self/children"),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
    )
    .expect("Failed to open directory");
    assert_eq!(vec!["echo_server", "hub_client"], await!(list_directory(&children_dir_proxy)));

    // These args are from hub_client.cml.
    assert_eq!(
        "Hippos",
        await!(read_file(&hub_proxy, "self/children/hub_client/exec/runtime/args/0"))
    );
    assert_eq!(
        "rule!",
        await!(read_file(&hub_proxy, "self/children/hub_client/exec/runtime/args/1"))
    );

    let echo_service_name = "fidl.examples.routing.echo.Echo";
    let hub_report_service_name = "fuchsia.test.hub.HubReport";
    let expose_svc_dir = "self/children/echo_server/exec/expose/svc";
    let expose_svc_dir_proxy = io_util::open_directory(
        &hub_proxy,
        &PathBuf::from(expose_svc_dir.clone()),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
    )
    .expect("Failed to open directory");
    assert_eq!(vec![echo_service_name], await!(list_directory(&expose_svc_dir_proxy)));

    let in_dir = "self/children/hub_client/exec/in";
    let svc_dir = format!("{}/{}", in_dir, "svc");
    let svc_dir_proxy = io_util::open_directory(
        &hub_proxy,
        &PathBuf::from(svc_dir.clone()),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
    )
    .expect("Failed to open directory");
    assert_eq!(
        vec![echo_service_name, hub_report_service_name],
        await!(list_directory(&svc_dir_proxy))
    );

    // Verify that the 'pkg' directory is avaialble.
    let pkg_dir = format!("{}/{}", in_dir, "pkg");
    let pkg_dir_proxy = io_util::open_directory(
        &hub_proxy,
        &PathBuf::from(pkg_dir),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
    )
    .expect("Failed to open directory");
    assert_eq!(vec!["bin", "lib", "meta", "test"], await!(list_directory(&pkg_dir_proxy)));

    // Verify that we can connect to the echo service from the in/svc directory.
    let in_echo_service_path = format!("{}/{}", svc_dir, echo_service_name);
    await!(connect_to_echo_service(&hub_proxy, in_echo_service_path));

    // Verify that we can connect to the echo service from the expose/svc directory.
    let expose_echo_service_path = format!("{}/{}", expose_svc_dir, echo_service_name);
    await!(connect_to_echo_service(&hub_proxy, expose_echo_service_path));

    // Verify that the 'hub' directory is avaialble. The 'hub' mapped to 'hub_client''s
    // namespace is actually mapped to the 'exec' directory of 'hub_client'.
    let scoped_hub_dir = format!("{}/{}", in_dir, "hub");
    let scoped_hub_dir_proxy = io_util::open_directory(
        &hub_proxy,
        &PathBuf::from(scoped_hub_dir),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
    )
    .expect("Failed to open directory");
    assert_eq!(
        vec!["expose", "in", "out", "resolved_url", "runtime"],
        await!(list_directory(&scoped_hub_dir_proxy))
    );

    // Verify that hub_client's view of the hub matches the view reachable from
    // the global hub.
    assert_eq!(
        vec!["expose", "in", "out", "resolved_url", "runtime"],
        await!(hub_test_hook.observe("/hub".to_string()))
    );

    Ok(())
}
