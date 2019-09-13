// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    component_manager_lib::{
        model::{
            self, hooks::*, testing::test_helpers, testing::test_helpers::list_directory, Hub,
            Model,
        },
        startup,
    },
    failure::{self, Error},
    fidl::endpoints::ClientEnd,
    fidl_fidl_examples_routing_echo as fecho,
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, MODE_TYPE_SERVICE, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon as zx,
    hub_test_hook::*,
    std::{path::PathBuf, sync::Arc, vec::Vec},
};

async fn connect_to_echo_service(
    hub_proxy: &DirectoryProxy,
    echo_service_path: String,
) -> Result<(), Error> {
    let node_proxy = io_util::open_node(
        &hub_proxy,
        &PathBuf::from(echo_service_path),
        io_util::OPEN_RIGHT_READABLE,
        MODE_TYPE_SERVICE,
    )?;
    let echo_proxy = fecho::EchoProxy::new(node_proxy.into_channel().unwrap());
    let res = echo_proxy.echo_string(Some("hippos")).await?;
    assert_eq!(res, Some("hippos".to_string()));
    Ok(())
}

fn hub_directory_listing(listing: Vec<&str>) -> HubReportEvent {
    HubReportEvent::DirectoryListing(listing.iter().map(|s| s.to_string()).collect())
}

fn file_content(content: &str) -> HubReportEvent {
    HubReportEvent::FileContent(content.to_string())
}

async fn create_model(root_component_url: &str) -> Result<Model, Error> {
    let root_component_url = root_component_url.to_string();
    // TODO(xbhatnag): Explain this in more detail. Setting use_builtin_process_launcher to false is non-obvious.
    let args = startup::Arguments { use_builtin_process_launcher: false, root_component_url };
    let model = startup::model_setup(&args).await?;
    Ok(model)
}

async fn install_hub(model: &Model) -> Result<DirectoryProxy, Error> {
    let (client_chan, server_chan) = zx::Channel::create()?;
    let root_component_url = model.root_realm.component_url.clone();
    let hub = Hub::new(root_component_url)?;
    // TODO(xbhatnag): Investigate why test() fails when OPEN_RIGHT_WRITABLE is removed
    hub.open_root(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, server_chan.into()).await?;
    model.hooks.install(hub.hooks()).await;
    let hub_proxy = ClientEnd::<DirectoryMarker>::new(client_chan).into_proxy()?;
    Ok(hub_proxy)
}

async fn install_hub_test_hook(model: &Model) -> Arc<HubTestHook> {
    let hub_test_hook = Arc::new(HubTestHook::new());
    model.hooks.install(vec![Hook::RouteFrameworkCapability(hub_test_hook.clone())]).await;
    hub_test_hook
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test() -> Result<(), Error> {
    let root_component_url = "fuchsia-pkg://fuchsia.com/hub_integration_test#meta/echo_realm.cm";
    let model = create_model(root_component_url).await?;
    let hub_proxy = install_hub(&model).await?;
    let hub_test_hook = install_hub_test_hook(&model).await;

    let res = model.look_up_and_bind_instance(model::AbsoluteMoniker::root()).await;
    let expected_res: Result<(), model::ModelError> = Ok(());
    assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));

    // Verify that echo_realm has two children.
    let children_dir_proxy =
        io_util::open_directory(&hub_proxy, &PathBuf::from("children"), OPEN_RIGHT_READABLE)?;
    assert_eq!(
        vec!["echo_server:0", "hub_client:0"],
        test_helpers::list_directory(&children_dir_proxy).await
    );

    // These args are from hub_client.cml.
    assert_eq!(
        "Hippos",
        test_helpers::read_file(&hub_proxy, "children/hub_client:0/exec/runtime/args/0").await
    );
    assert_eq!(
        "rule!",
        test_helpers::read_file(&hub_proxy, "children/hub_client:0/exec/runtime/args/1").await
    );

    let echo_service_name = "fidl.examples.routing.echo.Echo";
    let hub_report_service_name = "fuchsia.test.hub.HubReport";
    let expose_svc_dir = "children/echo_server:0/exec/expose/svc";
    let expose_svc_dir_proxy = io_util::open_directory(
        &hub_proxy,
        &PathBuf::from(expose_svc_dir.clone()),
        OPEN_RIGHT_READABLE,
    )?;
    assert_eq!(vec![echo_service_name], test_helpers::list_directory(&expose_svc_dir_proxy).await);

    let in_dir = "children/hub_client:0/exec/in";
    let svc_dir = format!("{}/{}", in_dir, "svc");
    let svc_dir_proxy =
        io_util::open_directory(&hub_proxy, &PathBuf::from(svc_dir.clone()), OPEN_RIGHT_READABLE)?;
    assert_eq!(
        vec![echo_service_name, hub_report_service_name],
        test_helpers::list_directory(&svc_dir_proxy).await
    );

    // Verify that the 'pkg' directory is avaialble.
    let pkg_dir = format!("{}/{}", in_dir, "pkg");
    let pkg_dir_proxy =
        io_util::open_directory(&hub_proxy, &PathBuf::from(pkg_dir), OPEN_RIGHT_READABLE)?;
    assert_eq!(
        vec!["bin", "lib", "meta", "test"],
        test_helpers::list_directory(&pkg_dir_proxy).await
    );

    // Verify that we can connect to the echo service from the in/svc directory.
    let in_echo_service_path = format!("{}/{}", svc_dir, echo_service_name);
    connect_to_echo_service(&hub_proxy, in_echo_service_path).await?;

    // Verify that we can connect to the echo service from the expose/svc directory.
    let expose_echo_service_path = format!("{}/{}", expose_svc_dir, echo_service_name);
    connect_to_echo_service(&hub_proxy, expose_echo_service_path).await?;

    // Verify that the 'hub' directory is avaialble. The 'hub' mapped to 'hub_client''s
    // namespace is actually mapped to the 'exec' directory of 'hub_client'.
    let scoped_hub_dir = format!("{}/{}", in_dir, "hub");
    let scoped_hub_dir_proxy =
        io_util::open_directory(&hub_proxy, &PathBuf::from(scoped_hub_dir), OPEN_RIGHT_READABLE)?;
    assert_eq!(
        vec!["expose", "in", "out", "resolved_url", "runtime"],
        test_helpers::list_directory(&scoped_hub_dir_proxy).await
    );

    // Verify that hub_client's view of the hub matches the view reachable from
    // the global hub.
    assert_eq!(
        hub_directory_listing(vec!["expose", "in", "out", "resolved_url", "runtime"]),
        hub_test_hook.observe("/hub").await
    );

    // Verify that hub_client's view is able to correctly read the names of the
    // children of the parent echo_realm.
    assert_eq!(
        hub_directory_listing(vec!["echo_server:0", "hub_client:0"]),
        hub_test_hook.observe("/parent_hub/children").await
    );

    // Verify that hub_client is able to see its sibling's hub correctly.
    assert_eq!(
        file_content("fuchsia-pkg://fuchsia.com/hub_integration_test#meta/echo_server.cm"),
        hub_test_hook.observe("/sibling_hub/exec/resolved_url").await
    );

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn dynamic_children_test() -> Result<(), Error> {
    let root_component_url =
        "fuchsia-pkg://fuchsia.com/hub_integration_test#meta/hub_collection_realm.cm";
    let model = create_model(root_component_url).await?;
    let hub_proxy = install_hub(&model).await?;
    let hub_test_hook = install_hub_test_hook(&model).await;

    // Start up the component instance!
    let res = model.look_up_and_bind_instance(model::AbsoluteMoniker::root()).await;
    let expected_res: Result<(), model::ModelError> = Ok(());
    assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));

    // Verify that collection_realm can see the child in the Hub
    assert_eq!(
        hub_directory_listing(vec!["coll:simple_instance:1"]),
        hub_test_hook.observe("/hub/children").await
    );

    // Verify that the global view also shows one dynamic child for the component.
    let children_dir_proxy =
        io_util::open_directory(&hub_proxy, &PathBuf::from("children"), OPEN_RIGHT_READABLE)?;
    assert_eq!(vec!["coll:simple_instance:1"], list_directory(&children_dir_proxy).await);

    // Verify that the dynamic child's hub (as seen by collection_realm) has the directories we expect
    // i.e. "children" and "url" but no "exec" because the child has not been bound.
    assert_eq!(
        hub_directory_listing(vec!["children", "url"]),
        hub_test_hook.observe("/hub/children/coll:simple_instance:1").await
    );

    // Verify that the external view also shows the same hub for the dynamic child.
    let children_dir_proxy = io_util::open_directory(
        &hub_proxy,
        &PathBuf::from("children/coll:simple_instance:1"),
        OPEN_RIGHT_READABLE,
    )?;
    assert_eq!(vec!["children", "url"], list_directory(&children_dir_proxy).await);

    Ok(())
}
