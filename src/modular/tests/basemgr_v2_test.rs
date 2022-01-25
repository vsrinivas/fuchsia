// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{anyhow, Error},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{self as fio, DirectoryMarker, DirectoryProxy},
    fidl_fuchsia_io2 as fio2, fidl_fuchsia_sys as fsys, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::new::{
        Capability, ChildOptions, LocalComponentHandles, RealmBuilder, Ref, Route,
    },
    futures::channel::mpsc,
    futures::prelude::*,
    std::sync::Arc,
    vfs::{directory::entry::DirectoryEntry, file::vmo::read_only_static, pseudo_directory},
};

const BASEMGR_URL: &str = "#meta/basemgr.cm";
const MOCK_COBALT_URL: &str = "#meta/mock_cobalt.cm";
const SESSIONMGR_URL: &str = "fuchsia-pkg://fuchsia.com/sessionmgr#meta/sessionmgr.cmx";

// Tests that the session launches sessionmgr as a child v1 component.
#[fuchsia::test]
async fn test_launch_sessionmgr() -> Result<(), Error> {
    let config_data_dir = pseudo_directory! {
        "basemgr" => pseudo_directory! {
            "startup.config" => read_only_static(r#"{ "basemgr": { "enable_cobalt": false } }"#),
        },
    };

    let builder = RealmBuilder::new().await?;

    // Add a local component that provides the `config-data` directory to the realm.
    let config_data_server = builder
        .add_local_child(
            "config-data-server",
            move |handles| {
                let proxy = spawn_vfs(config_data_dir.clone());
                async move {
                    let mut fs = ServiceFs::new();
                    fs.add_remote("config-data", proxy);
                    fs.serve_connection(handles.outgoing_dir.into_channel())
                        .expect("failed to serve config-data ServiceFs");
                    fs.collect::<()>().await;
                    Ok::<(), anyhow::Error>(())
                }
                .boxed()
            },
            ChildOptions::new(),
        )
        .await?;

    // Add mock_cobalt to the realm.
    let mock_cobalt =
        builder.add_child("mock_cobalt", MOCK_COBALT_URL, ChildOptions::new()).await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&mock_cobalt),
        )
        .await?;

    // Add a local component that serves `fuchsia.sys.Launcher` to the realm.
    let (launch_info_sender, launch_info_receiver) = mpsc::channel(1);
    let sys_launcher = builder
        .add_local_child(
            "sys_launcher",
            move |handles| Box::pin(sys_launcher_local_child(launch_info_sender.clone(), handles)),
            ChildOptions::new(),
        )
        .await?;

    // Add basemgr to the realm.
    let basemgr = builder.add_child("basemgr", BASEMGR_URL, ChildOptions::new().eager()).await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.cobalt.LoggerFactory"))
                .from(&mock_cobalt)
                .to(&basemgr),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&basemgr),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.sys.Launcher"))
                .from(&sys_launcher)
                .to(&basemgr),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(
                    Capability::directory("config-data")
                        .path("/config-data")
                        .rights(fio2::R_STAR_DIR),
                )
                .from(&config_data_server)
                .to(&basemgr),
        )
        .await?;
    builder
        .add_route(
            Route::new().capability(Capability::storage("cache")).from(Ref::parent()).to(&basemgr),
        )
        .await?;
    builder
        .add_route(
            Route::new().capability(Capability::storage("data")).from(Ref::parent()).to(&basemgr),
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
                .capability(Capability::protocol_by_name("fuchsia.tracing.provider.Registry"))
                .capability(Capability::protocol_by_name(
                    "fuchsia.hardware.power.statecontrol.Admin",
                ))
                .capability(Capability::protocol_by_name("fuchsia.ui.policy.Presenter"))
                .from(&placeholder)
                .to(&basemgr),
        )
        .await?;

    let _instance = builder.build().await?;

    // The session should have started sessionmgr as a v1 component.
    let launch_info =
        launch_info_receiver.take(1).next().await.ok_or_else(|| anyhow!("expected LaunchInfo"))?;
    assert_eq!(SESSIONMGR_URL, launch_info.url);

    Ok(())
}

// Serves an implementation of the `fuchsia.sys.Launcher` protocol that forwards
// the `LaunchInfo` of created components to `launch_info_sender`.
async fn sys_launcher_local_child(
    launch_info_sender: mpsc::Sender<fsys::LaunchInfo>,
    handles: LocalComponentHandles,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |mut stream: fsys::LauncherRequestStream| {
        let mut launch_info_sender = launch_info_sender.clone();
        fasync::Task::local(async move {
            while let Some(fsys::LauncherRequest::CreateComponent {
                launch_info,
                controller,
                control_handle: _,
            }) = stream.try_next().await.expect("failed to serve Launcher")
            {
                let mut controller_stream = controller
                    .unwrap()
                    .into_stream()
                    .expect("failed to create stream of ComponentController requests");
                fasync::Task::spawn(async move {
                    if let Some(request) = controller_stream.try_next().await.unwrap() {
                        panic!("Unexpected ComponentController request: {:?}", request);
                    }
                })
                .detach();

                launch_info_sender.try_send(launch_info).expect("failed to send LaunchInfo");
            }
        })
        .detach();
    });
    fs.serve_connection(handles.outgoing_dir.into_channel())?;
    fs.collect::<()>().await;
    Ok(())
}

// Returns a `DirectoryProxy` that serves the directory entry `dir`.
fn spawn_vfs(dir: Arc<dyn DirectoryEntry>) -> DirectoryProxy {
    let (client_end, server_end) = fidl::endpoints::create_endpoints::<DirectoryMarker>().unwrap();
    let scope = vfs::execution_scope::ExecutionScope::new();
    dir.open(
        scope,
        fio::OPEN_RIGHT_READABLE,
        0,
        vfs::path::Path::dot(),
        ServerEnd::new(server_end.into_channel()),
    );
    client_end.into_proxy().unwrap()
}
