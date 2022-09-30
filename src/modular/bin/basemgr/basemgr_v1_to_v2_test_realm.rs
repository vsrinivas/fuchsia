// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{Context as _, Error},
    component_events::{events::*, matcher::*},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio, fidl_fuchsia_modular as modular, fidl_fuchsia_sys as fsys,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::{
        Capability, ChildOptions, LocalComponentHandles, RealmBuilder, Ref, Route,
    },
    futures::prelude::*,
    std::sync::Arc,
    tracing::info,
    vfs::{directory::entry::DirectoryEntry, file::vmo::read_only_static, pseudo_directory},
};

const BASEMGR_FOR_TESTING_V1_TO_V2_URL: &str = "#meta/basemgr-for-testing-v1-to-v2.cm";
const MOCK_COBALT_URL: &str = "#meta/mock_cobalt.cm";

// Tests that a v2 basemgr child can use services from v1 sessionmgr.
#[fuchsia::test]
async fn basemgr_v1_to_v2_test() -> Result<(), Error> {
    let config_data_dir = pseudo_directory! {
        "basemgr" => pseudo_directory! {
            "startup.config" => read_only_static(r##"
{
    "basemgr": {
        "enable_cobalt": false
    },
    "sessionmgr": {
        "agent_service_index": [
            {
                "agent_url": "fuchsia-pkg://fuchsia.com/basemgr_unittests#meta/v1-echo-server.cmx",
                "service_name": "fuchsia.examples.Echo"
            }
        ]
    }
}"##)
        }
    };

    // Subscribe to stopped events for child components
    let mut event_stream =
        EventStream::open_at_path_pipelined("/svc/fuchsia.component.EventStream").unwrap();

    let builder = RealmBuilder::new().await?;

    // Add a local component that provides the `config-data` directory to the realm.
    let config_data_server = builder
        .add_local_child(
            "config-data-server",
            move |handles| {
                let proxy = spawn_vfs(config_data_dir.clone());
                async move {
                    let _ = &handles;
                    let mut fs = ServiceFs::new();
                    fs.add_remote("config-data", proxy);
                    fs.serve_connection(handles.outgoing_dir)
                        .expect("failed to serve config-data ServiceFs");
                    fs.collect::<()>().await;
                    Ok::<(), anyhow::Error>(())
                }
                .boxed()
            },
            ChildOptions::new(),
        )
        .await?;

    // Add a local v2 component to test using v1 services from sessionmgr.
    let child_using_v1_services = builder
        .add_local_child(
            "child_using_v1_services",
            move |handles: LocalComponentHandles| Box::pin(child_using_v1_services(handles)),
            ChildOptions::new().eager(),
        )
        .await?;

    // Add basemgr to the realm.
    let basemgr = builder
        .add_child(
            "basemgr-for-testing-v1-to-v2",
            BASEMGR_FOR_TESTING_V1_TO_V2_URL,
            ChildOptions::new().eager(),
        )
        .await?;

    // Add mock_cobalt to the realm.
    let mock_cobalt =
        builder.add_child("mock_cobalt", MOCK_COBALT_URL, ChildOptions::new()).await?;

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fidl_fuchsia_logger::LogSinkMarker>())
                .from(Ref::parent())
                .to(&mock_cobalt),
        )
        .await?;

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<
                    fidl_fuchsia_metrics::MetricEventLoggerFactoryMarker,
                >())
                .from(&mock_cobalt)
                .to(&basemgr),
        )
        .await?;

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fidl_fuchsia_logger::LogSinkMarker>())
                .capability(Capability::protocol::<fsys::LauncherMarker>())
                .capability(Capability::storage("data"))
                .from(Ref::parent())
                .to(&basemgr),
        )
        .await?;

    builder
        .add_route(
            Route::new()
                .capability(
                    Capability::directory("config-data")
                        .path("/config-data")
                        .rights(fio::R_STAR_DIR),
                )
                .from(&config_data_server)
                .to(&basemgr),
        )
        .await?;

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fidl_fuchsia_examples::EchoMarker>())
                .capability(Capability::protocol::<modular::PuppetMasterMarker>())
                .capability(Capability::protocol::<modular::LifecycleMarker>())
                .from(&basemgr)
                .to(&child_using_v1_services),
        )
        .await?;

    // Add a placeholder component and routes for capabilities that are not
    // expected to be used in this test scenario.
    let placeholder = builder
        .add_local_child(
            "placeholder",
            |_: LocalComponentHandles| Box::pin(async move { Ok(()) }),
            ChildOptions::new(),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fidl_fuchsia_tracing_provider::RegistryMarker>())
                .capability(Capability::protocol::<
                    fidl_fuchsia_hardware_power_statecontrol::AdminMarker,
                >())
                .capability(Capability::protocol::<fidl_fuchsia_ui_policy::PresenterMarker>())
                .capability(Capability::protocol::<fidl_fuchsia_session::RestarterMarker>())
                .from(&placeholder)
                .to(&basemgr),
        )
        .await?;

    let _realm_instance = builder.build().await?;

    // After `child_using_v1_services` invokes the v1 services successfully, it
    // then calls `terminate()` on basemgr (via the `modular::Lifecycle`
    // protocol). Wait for basemgr's `Stopped` event, and exit this test.
    info!("awaiting basemgr Stopped event");
    EventMatcher::ok()
        .moniker_regex("./basemgr")
        .wait::<Stopped>(&mut event_stream)
        .await
        .context("failed to observe basemgr Stopped event")?;

    Ok(())
}

// A local v2 component that invokes v1 services exposed by sessionmgr, and then
// terminates basemgr so the test can complete successfully.
async fn child_using_v1_services(handles: LocalComponentHandles) -> Result<(), Error> {
    let puppet_master: modular::PuppetMasterProxy = handles
        .connect_to_protocol::<modular::PuppetMasterMarker>()
        .context("Failed to connect to PuppetMaster service")?;
    let _: Vec<String> = puppet_master.get_stories().await?;
    info!("Got stories!");

    let echo: fidl_fuchsia_examples::EchoProxy = handles
        .connect_to_protocol::<fidl_fuchsia_examples::EchoMarker>()
        .context("Failed to connect to Echo service")?;
    let test_str = "Hello echo!";
    let response = echo.echo_string(test_str).await?;
    info!("Got echo!");
    assert_eq!(test_str, response);

    // Success! Tell basemgr to terminate, which should generate the `Stopped`
    // event needed to exit this test.
    let modular_lifecycle: modular::LifecycleProxy = handles
        .connect_to_protocol::<modular::LifecycleMarker>()
        .context("Failed to connect to Lifecycle service")?;
    modular_lifecycle.terminate()?;

    Ok(())
}

// Returns a `DirectoryProxy` that serves the directory entry `dir`.
fn spawn_vfs(dir: Arc<dyn DirectoryEntry>) -> fio::DirectoryProxy {
    let (client_end, server_end) =
        fidl::endpoints::create_endpoints::<fio::DirectoryMarker>().unwrap();
    let scope = vfs::execution_scope::ExecutionScope::new();
    dir.open(
        scope,
        fio::OpenFlags::RIGHT_READABLE,
        0,
        vfs::path::Path::dot(),
        ServerEnd::new(server_end.into_channel()),
    );
    client_end.into_proxy().unwrap()
}
