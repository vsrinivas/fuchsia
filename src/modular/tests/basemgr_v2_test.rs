// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_hardware_power_statecontrol as fhpower,
    fidl_fuchsia_io::{self as fio, DirectoryMarker, DirectoryProxy},
    fidl_fuchsia_modular_internal as fmodular, fidl_fuchsia_sys as fsys, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::new::{
        Capability, ChildOptions, ChildRef, LocalComponentHandles, RealmBuilder, Ref, Route,
    },
    futures::{channel::mpsc, future::join_all, prelude::*},
    std::sync::Arc,
    vfs::{directory::entry::DirectoryEntry, file::vmo::read_only_static, pseudo_directory},
};

const BASEMGR_URL: &str = "#meta/basemgr.cm";
const BASEMGR_WITH_CHILDREN_URL: &str = "#meta/basemgr-with-children.cm";
const MOCK_COBALT_URL: &str = "#meta/mock_cobalt.cm";
const SESSIONMGR_URL: &str = "fuchsia-pkg://fuchsia.com/sessionmgr#meta/sessionmgr.cmx";

struct TestFixture {
    pub builder: RealmBuilder,
    pub basemgr: ChildRef,
    pub placeholder: ChildRef,
}

impl TestFixture {
    async fn new(basemgr_url: &str) -> Result<TestFixture, Error> {
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

        // Add basemgr to the realm.
        let basemgr =
            builder.add_child("basemgr", basemgr_url, ChildOptions::new().eager()).await?;
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
                    .capability(Capability::storage("cache"))
                    .from(Ref::parent())
                    .to(&basemgr),
            )
            .await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::storage("data"))
                    .from(Ref::parent())
                    .to(&basemgr),
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
                    .capability(Capability::protocol_by_name("fuchsia.ui.policy.Presenter"))
                    .from(&placeholder)
                    .to(&basemgr),
            )
            .await?;

        return Ok(TestFixture { builder, basemgr, placeholder });
    }

    async fn route_noop_sys_launcher(self) -> Result<TestFixture, Error> {
        let sys_launcher = self
            .builder
            .add_local_child(
                "sys_launcher",
                move |handles| Box::pin(sys_launcher_noop(handles)),
                ChildOptions::new(),
            )
            .await?;
        let () = self
            .builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.sys.Launcher"))
                    .from(&sys_launcher)
                    .to(&self.basemgr),
            )
            .await?;
        Ok(self)
    }

    // Vend a placeholder implementation `fuchsia.hardware.power.statecontrol.Admin`
    // because this test doesn't expect to have this protocol exercised.
    async fn route_placeholder_admin(self) -> Result<TestFixture, Error> {
        let () = self
            .builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(
                        "fuchsia.hardware.power.statecontrol.Admin",
                    ))
                    .from(&self.placeholder)
                    .to(&self.basemgr),
            )
            .await?;
        Ok(self)
    }
}

// Tests that the session launches sessionmgr as a child v1 component.
#[fuchsia::test]
async fn test_launch_sessionmgr() -> Result<(), Error> {
    // Add a local component that serves `fuchsia.sys.Launcher` to the realm.
    let (launch_info_sender, launch_info_receiver) = mpsc::channel(1);
    let fixture = TestFixture::new(BASEMGR_URL).await?.route_placeholder_admin().await?;

    let sys_launcher = fixture
        .builder
        .add_local_child(
            "sys_launcher",
            move |handles| Box::pin(sys_launcher_local_child(launch_info_sender.clone(), handles)),
            ChildOptions::new(),
        )
        .await?;
    let () = fixture
        .builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.sys.Launcher"))
                .from(&sys_launcher)
                .to(&fixture.basemgr),
        )
        .await?;

    let _instance = fixture.builder.build().await?;

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

// Tests that basemgr will launch all of connect to all Binder connections
// in its namespace by passing in in child names via the "--eager-child" flag.
// In production, basemgr will have actual child components and will have
// fuchsia.component.Binder entries as a result of "use from child" clauses.
// However, in this test, basemgr will not have any child components but will
// have the expected Binder protocols routed to it via Realm Builder below.
// This allows us to use a local component implementation to assert that basemgr
// does in fact connect to expected Binder path.
#[fuchsia::test]
async fn test_launch_v2_children() -> Result<(), Error> {
    const NUM_TRIES: usize = 2;
    const CHILDREN: [&str; 2] = ["foo", "bar"];
    let fixture = TestFixture::new(BASEMGR_WITH_CHILDREN_URL)
        .await?
        .route_placeholder_admin()
        .await?
        .route_noop_sys_launcher()
        .await?;

    let mut child_restart_futs = vec![];
    for child_name in CHILDREN.iter() {
        let binder_path = format!("fuchsia.component.Binder.{}", child_name);

        // The function |basemgr_child_impl| is structured such that when it
        // encounters a connection to the Binder protocol, it will exit. This will
        // trigger a component Stopped event which should notify `basemgr` that the
        // component exited. This is repeated to ensure that `basemgr` not only
        // starts the component, but also that it restarts it.
        let (binder_sender, binder_receiver) = mpsc::channel(NUM_TRIES);
        let binder_path_clone = binder_path.clone();
        let local_child = fixture
            .builder
            .add_local_child(
                child_name.to_string(),
                move |handles| {
                    Box::pin(basemgr_child_impl(
                        binder_sender.clone(),
                        binder_path.clone(),
                        handles,
                    ))
                },
                ChildOptions::new(),
            )
            .await?;
        fixture
            .builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(binder_path_clone))
                    .from(&local_child)
                    .to(&fixture.basemgr),
            )
            .await?;

        let fut = async move {
            let mut stream = binder_receiver.take(NUM_TRIES);
            for _ in 0..NUM_TRIES {
                let binder_connected = stream.next().await.unwrap();
                assert!(binder_connected);
            }
        };
        child_restart_futs.push(fut);
    }

    let instance = fixture.builder.build().await?;

    let _ = join_all(child_restart_futs).await;

    // We have to destroy the instance after the test assertion because
    // basemgr teardown will be spammy with error logs due to closing sessionmgr's
    // ComponentController channel
    instance.destroy().await?;

    Ok(())
}

// Tests that failure to connect to children will yield a system reboot.
// This is accomplished by *not* routing `fuchsia.component.Binder` to the
// basemgr component. basemgr will encounter PEER_CLOSED and after exhausting
// all of its retry attempts, trigger a reboot by calling
// |fuchsia.hardware.power.statecontrol/Admin.Reboot|.
#[fuchsia::test]
async fn test_reboots_system_after_max_attempts() -> Result<(), Error> {
    let fixture =
        TestFixture::new(BASEMGR_WITH_CHILDREN_URL).await?.route_noop_sys_launcher().await?;

    let (shutdown_sender, shutdown_receiver) = mpsc::channel(1);
    let local_admin = fixture
        .builder
        .add_local_child(
            "local_admin",
            move |handles| Box::pin(local_admin_impl(shutdown_sender.clone(), handles)),
            ChildOptions::new(),
        )
        .await?;
    let () = fixture
        .builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name(
                    "fuchsia.hardware.power.statecontrol.Admin",
                ))
                .from(&local_admin)
                .to(&fixture.basemgr),
        )
        .await?;

    let instance = fixture.builder.build().await?;

    let shutdown_requested = shutdown_receiver.take(1).next().await.unwrap();
    assert!(shutdown_requested);

    // We have to destroy the instance after the test assertion because
    // basemgr teardown will be spammy with error logs due to closing sessionmgr's
    // ComponentController channel
    instance.destroy().await?;

    Ok(())
}

// Local implementation that pretends to be a basemgr child. Its connection
// to |fuchsia.component.Binder| is used to signal to basemgr that its still
// running. If this function receives a signal on |binder_sender| it will exit,
// thus closing the Binder channel that basemgr holds.
async fn basemgr_child_impl(
    mut binder_sender: mpsc::Sender<bool>,
    binder_path: String,
    handles: LocalComponentHandles,
) -> Result<(), Error> {
    // This check is added here because basemgr will continue to attempts restart
    // of this child component after the test assertions in |test_launch_v2_children|
    // completes.
    if binder_sender.is_closed() {
        return Ok(());
    }

    let (mut reboot_sender, reboot_receiver) = mpsc::channel(1);
    let svc_fut = async move {
        let mut fs = ServiceFs::new();
        fs.dir("svc").add_fidl_service_at(
            binder_path,
            move |_stream: fcomponent::BinderRequestStream| {
                binder_sender.try_send(true).expect("failed to send message");
                reboot_sender.try_send(true).expect("failed to send message");
            },
        );
        fs.serve_connection(handles.outgoing_dir.into_channel()).unwrap();
        fs.collect::<()>().await;
    }
    .fuse();
    futures::pin_mut!(svc_fut);
    let reboot_fut = async move {
        reboot_receiver.take(1).next().await.unwrap();
    }
    .fuse();
    futures::pin_mut!(reboot_fut);

    futures::select! {
        _ = svc_fut => panic!("vfs server should not stop"),
        _ = reboot_fut => {}
    }

    Ok(())
}

async fn local_admin_impl(
    reboot_sender: mpsc::Sender<bool>,
    handles: LocalComponentHandles,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |mut stream: fhpower::AdminRequestStream| {
        let mut reboot_sender = reboot_sender.clone();
        fasync::Task::local(async move {
            while let Some(request) = stream.try_next().await.unwrap() {
                match request {
                    fhpower::AdminRequest::Reboot { .. } => {
                        reboot_sender.try_send(true).expect("failed to send message");
                    }
                    _ => {
                        panic!("Unexpected request: {:?}", request);
                    }
                }
            }
        })
        .detach();
    });
    fs.serve_connection(handles.outgoing_dir.into_channel()).unwrap();
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

async fn sys_launcher_noop(handles: LocalComponentHandles) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(|mut stream: fsys::LauncherRequestStream| {
        fasync::Task::local(async move {
            while let Some(request) = stream.try_next().await.expect("failed to serve Launcher") {
                match request {
                    fsys::LauncherRequest::CreateComponent {
                        launch_info,
                        controller,
                        control_handle: _,
                    } => {
                        let () = serve_sessionmgr(
                            launch_info.directory_request.expect("no DirectoryRequest received"),
                            controller.unwrap(),
                        )
                        .await
                        .unwrap();
                    }
                }
            }
        })
        .detach();
    });
    fs.serve_connection(handles.outgoing_dir.into_channel())?;
    fs.collect::<()>().await;
    Ok(())
}

// We keep a reference to |_controller| to ensure the that channel doesn't close
// while Sessionmgr is being served.
async fn serve_sessionmgr(
    channel: fidl::Channel,
    _controller: fidl::endpoints::ServerEnd<fsys::ComponentControllerMarker>,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    fs.add_fidl_service(move |mut stream: fmodular::SessionmgrRequestStream| {
        fasync::Task::local(async move { while let Some(_) = stream.try_next().await.unwrap() {} })
            .detach();
    });
    fs.serve_connection(channel)?;
    fs.collect::<()>().await;
    Ok(())
}
