// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::fake_appmgr::{CreateComponentFn, FakeAppmgr},
    anyhow::{anyhow, Context, Error},
    fidl::endpoints::{create_endpoints, create_proxy, ServerEnd},
    fidl_fuchsia_element as felement, fidl_fuchsia_examples as fexamples, fidl_fuchsia_io as fio,
    fidl_fuchsia_modular as fmodular, fidl_fuchsia_modular_internal as fmodular_internal,
    fidl_fuchsia_sys as fsys, fidl_fuchsia_ui_app as fapp, fuchsia_async as fasync,
    fuchsia_component::{
        client::connect_to_protocol_at_dir_root,
        server::{ServiceFs, ServiceObj},
    },
    fuchsia_component_test::{
        Capability, ChildOptions, ChildRef, LocalComponentHandles, RealmBuilder, Ref, Route,
    },
    fuchsia_scenic as scenic, fuchsia_zircon as zx,
    fuchsia_zircon::Peered,
    futures::{channel::mpsc, prelude::*},
    std::collections::HashMap,
    std::sync::Arc,
    vfs::{directory::entry::DirectoryEntry, file::vmo::read_only_static, pseudo_directory},
};

mod fake_appmgr;

const SESSIONMGR_URL: &str = "#meta/sessionmgr.cm";
const MOCK_COBALT_URL: &str = "#meta/mock_cobalt.cm";
const TEST_SESSION_SHELL_URL: &str =
    "fuchsia-pkg://fuchsia.com/test_session_shell#meta/test_session_shell.cmx";
const TEST_MOD_URL: &str = "fuchsia-pkg://fuchsia.com/test_mod#meta/test_mod.cmx";

struct TestFixture {
    pub builder: RealmBuilder,
    pub sessionmgr: ChildRef,
}

impl TestFixture {
    async fn new() -> Result<TestFixture, Error> {
        let builder = RealmBuilder::new().await?;

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

        // Add sessionmgr to the realm.
        let sessionmgr =
            builder.add_child("sessionmgr", SESSIONMGR_URL, ChildOptions::new()).await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(
                        "fuchsia.metrics.MetricEventLoggerFactory",
                    ))
                    .from(&mock_cobalt)
                    .to(&sessionmgr),
            )
            .await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .capability(Capability::protocol_by_name("fuchsia.tracing.provider.Registry"))
                    .from(Ref::parent())
                    .to(&sessionmgr),
            )
            .await?;

        // Expose sessionmgr's protocols to the test.
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<fmodular_internal::SessionmgrMarker>())
                    .from(&sessionmgr)
                    .to(Ref::parent()),
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
                    .capability(Capability::protocol_by_name("fuchsia.intl.PropertyProvider"))
                    .from(&placeholder)
                    .to(&sessionmgr),
            )
            .await?;

        return Ok(TestFixture { builder, sessionmgr });
    }

    async fn with_config<Bytes>(self, config: Bytes) -> Result<TestFixture, Error>
    where
        Bytes: AsRef<[u8]> + Send + Sync + 'static,
    {
        let config_data_dir = pseudo_directory! {
            "startup.config" => read_only_static(config),
        };

        // Add a local component that provides the `config-data` directory to the realm.
        let config_data_server = self
            .builder
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

        self.builder
            .add_route(
                Route::new()
                    .capability(
                        Capability::directory("config-data")
                            .path("/config-data")
                            .rights(fio::R_STAR_DIR),
                    )
                    .from(&config_data_server)
                    .to(&self.sessionmgr),
            )
            .await?;

        Ok(self)
    }

    async fn with_fake_appmgr(self, fake_appmgr: Arc<FakeAppmgr>) -> Result<TestFixture, Error> {
        let appmgr = self
            .builder
            .add_local_child(
                "appmgr",
                move |handles| Box::pin(fake_appmgr.clone().serve_local_child(handles)),
                ChildOptions::new(),
            )
            .await?;
        self.builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<fsys::EnvironmentMarker>())
                    .capability(Capability::protocol::<fsys::LauncherMarker>())
                    .from(&appmgr)
                    .to(&self.sessionmgr),
            )
            .await?;

        Ok(self)
    }
}

// Returns a `DirectoryProxy` that serves the directory entry `dir`.
fn spawn_vfs(dir: Arc<dyn DirectoryEntry>) -> fio::DirectoryProxy {
    let (client_end, server_end) = create_endpoints::<fio::DirectoryMarker>().unwrap();
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

// Tests that sessionmgr starts and attempts to launch the session shell.
#[fuchsia::test]
async fn test_launch_sessionmgr() -> Result<(), Error> {
    let fixture = TestFixture::new()
        .await?
        .with_config(
            r#"{
  "basemgr": {
    "enable_cobalt": false,
    "session_shells": [
      {
        "url": "fuchsia-pkg://fuchsia.com/test_session_shell#meta/test_session_shell.cmx"
      }
    ]
  },
  "sessionmgr": {
    "enable_cobalt": false
  }
}"#,
        )
        .await?;

    let (mut shell_launched_sender, shell_launched_receiver) = mpsc::channel(1);

    let mock_session_shell: CreateComponentFn = Box::new(move |launch_info: fsys::LaunchInfo| {
        let mut outgoing_fs = ServiceFs::<ServiceObj<'_, ()>>::new();
        outgoing_fs
            .serve_connection(launch_info.directory_request.unwrap())
            .expect("failed to serve outgoing fs");
        fasync::Task::local(outgoing_fs.collect::<()>()).detach();

        shell_launched_sender.try_send(()).expect("failed to send shell launched");
    });

    let mut mock_v1_components = HashMap::new();
    mock_v1_components.insert(TEST_SESSION_SHELL_URL.to_string(), mock_session_shell);

    let fake_appmgr = FakeAppmgr::new(mock_v1_components);
    let fixture = fixture.with_fake_appmgr(fake_appmgr).await?;

    let instance = fixture.builder.build().await?;

    let (session_context_client_end, _session_context_server_end) =
        create_endpoints::<fmodular_internal::SessionContextMarker>()?;
    let (_services_from_sessionmgr, services_from_sessionmgr_server_end) =
        create_proxy::<fio::DirectoryMarker>()?;
    let mut link_token_pair = scenic::flatland::ViewCreationTokenPair::new()?;
    let mut services_for_agents_fs = ServiceFs::<ServiceObj<'_, ()>>::new();

    let sessionmgr_proxy = instance
        .root
        .connect_to_protocol_at_exposed_dir::<fmodular_internal::SessionmgrMarker>()?;
    sessionmgr_proxy.initialize(
        "test_session_id",
        session_context_client_end,
        &mut services_for_agents_fs.host_services_list()?,
        services_from_sessionmgr_server_end,
        &mut link_token_pair.view_creation_token,
    )?;

    fasync::Task::local(services_for_agents_fs.collect()).detach();

    let () = shell_launched_receiver
        .take(1)
        .next()
        .await
        .ok_or_else(|| anyhow!("expected shell launched message"))?;

    instance.destroy().await?;

    Ok(())
}

// Tests that an agent (in this case, the session shell) can connect to a protocol exposed by a
// v2 component through the `fuchsia.modular.Agent` protocol.
#[fuchsia::test]
async fn test_v2_modular_agents() -> Result<(), Error> {
    let fixture = TestFixture::new()
        .await?
        .with_config(
            r#"{
  "basemgr": {
    "enable_cobalt": false,
    "session_shells": [
      {
        "url": "fuchsia-pkg://fuchsia.com/test_session_shell#meta/test_session_shell.cmx"
      }
    ]
  },
  "sessionmgr": {
    "enable_cobalt": false,
    "agent_service_index": [
      {
        "service_name": "fuchsia.examples.Echo",
        "agent_url": "fuchsia-pkg://fuchsia.com/test_agent#meta/test_agent.cmx"
      }
    ],
    "v2_modular_agents": [
      {
        "service_name": "fuchsia.modular.Agent.test_agent",
        "agent_url": "fuchsia-pkg://fuchsia.com/test_agent#meta/test_agent.cmx"
      }
    ]
  }
}"#,
        )
        .await?;

    let (shell_called_echo_sender, shell_called_echo_receiver) = mpsc::channel(1);

    let mock_session_shell: CreateComponentFn = Box::new(move |launch_info: fsys::LaunchInfo| {
        let mut shell_called_echo_sender = shell_called_echo_sender.clone();
        fasync::Task::local(async move {
            let mut outgoing_fs = ServiceFs::<ServiceObj<'_, ()>>::new();
            outgoing_fs
                .serve_connection(launch_info.directory_request.unwrap())
                .expect("failed to serve outgoing fs");

            let svc = launch_info.additional_services.unwrap();

            assert!(svc.names.contains(&"fuchsia.examples.Echo".to_string()));

            let provider =
                svc.provider.unwrap().into_proxy().expect("failed to create ServiceProvider proxy");

            let (echo, echo_server_end) =
                create_proxy::<fexamples::EchoMarker>().expect("failed to create Echo endpoints");
            provider
                .connect_to_service("fuchsia.examples.Echo", echo_server_end.into_channel())
                .expect("failed to call ConnectToService");

            let result = echo.echo_string("hello").await.expect("failed to call EchoString");

            assert_eq!("hello", result);

            shell_called_echo_sender.try_send(()).expect("failed to send shell called echo");
        })
        .detach();
    });

    let mut mock_v1_components = HashMap::new();
    mock_v1_components.insert(TEST_SESSION_SHELL_URL.to_string(), mock_session_shell);

    let fake_appmgr = FakeAppmgr::new(mock_v1_components);
    let fixture = fixture.with_fake_appmgr(fake_appmgr).await?;

    let instance = fixture.builder.build().await?;

    let (session_context_client_end, _session_context_server_end) =
        create_endpoints::<fmodular_internal::SessionContextMarker>()?;
    let (_services_from_sessionmgr, services_from_sessionmgr_server_end) =
        create_proxy::<fio::DirectoryMarker>()?;
    let mut link_token_pair = scenic::flatland::ViewCreationTokenPair::new()?;

    // `fuchsia.modular.Agent.test_agent` represents a v2 component whose Agent protocol
    // is routed to sessionmgr through /svc_for_v1_sessionmgr.
    let mut services_for_agents_fs = ServiceFs::new();
    services_for_agents_fs.add_fidl_service_at(
        "fuchsia.modular.Agent.test_agent",
        move |stream: fmodular::AgentRequestStream| {
            fasync::Task::local(async move { serve_echo_agent(stream).await }).detach();
        },
    );

    let sessionmgr_proxy = instance
        .root
        .connect_to_protocol_at_exposed_dir::<fmodular_internal::SessionmgrMarker>()?;
    sessionmgr_proxy.initialize(
        "test_session_id",
        session_context_client_end,
        &mut services_for_agents_fs.host_services_list()?,
        services_from_sessionmgr_server_end,
        &mut link_token_pair.view_creation_token,
    )?;

    fasync::Task::local(services_for_agents_fs.collect()).detach();

    let () = shell_called_echo_receiver
        .take(1)
        .next()
        .await
        .ok_or_else(|| anyhow!("expected shell called echo message"))?;

    instance.destroy().await?;

    Ok(())
}

#[fuchsia::test]
async fn test_v2_session_shell() -> Result<(), Error> {
    let fixture = TestFixture::new()
        .await?
        .with_config(
            r#"{
  "basemgr": {
    "enable_cobalt": false
  },
  "sessionmgr": {
    "enable_cobalt": false,
    "present_mods_as_stories": true
  }
}"#,
        )
        .await?;

    // The mock module, TEST_MOD_URL, serves fuchsia.ui.app.ViewProvider and signals the received
    // ViewCreationToken channel.
    let mock_mod: CreateComponentFn = Box::new(move |launch_info: fsys::LaunchInfo| {
        fasync::Task::local(async move {
            let mut outgoing_fs = ServiceFs::new();
            outgoing_fs.add_fidl_service(move |stream: fapp::ViewProviderRequestStream| {
                fasync::Task::local(
                    stream
                        .try_for_each(move |req| {
                            match req {
                                fapp::ViewProviderRequest::CreateView2 {
                                    args,
                                    ..
                                } => {
                                    args.view_creation_token.unwrap().value
                                            .signal_peer(zx::Signals::NONE, zx::Signals::USER_0)
                                            .expect("Signalling viewport_creation_token");
                                }
                                _ => panic!("expected sessionmgr to get mod view through CreateViewWithViewRef")
                            };
                            futures::future::ready(Ok(()))
                        })
                        .unwrap_or_else(|e| {
                            panic!("error serving ViewProvider: {:?}", e)
                        }),
                )
                .detach()
            });

            outgoing_fs
            .serve_connection(launch_info.directory_request.unwrap())
                .expect("failed to serve outgoing fs");
            outgoing_fs.collect::<()>().await;
        })
        .detach();
    });

    let mut mock_v1_components = HashMap::new();
    mock_v1_components.insert(TEST_MOD_URL.to_string(), mock_mod);

    let fake_appmgr = FakeAppmgr::new(mock_v1_components);
    let fixture = fixture.with_fake_appmgr(fake_appmgr).await?;

    let instance = fixture.builder.build().await?;

    let (session_context_client_end, _session_context_server_end) =
        create_endpoints::<fmodular_internal::SessionContextMarker>()?;
    let (services_from_sessionmgr, services_from_sessionmgr_server_end) =
        create_proxy::<fio::DirectoryMarker>()?;
    let mut link_token_pair = scenic::flatland::ViewCreationTokenPair::new()?;

    let (view_spec_sender, view_spec_receiver) = mpsc::channel(1);

    // `fuchsia.element.GraphicalPresenter` is served via `services_for_agents_fs`
    // to simulate a v2 session shell component.
    let mut services_for_agents_fs = ServiceFs::new();
    services_for_agents_fs.add_fidl_service(
        move |stream: felement::GraphicalPresenterRequestStream| {
            let view_spec_sender = view_spec_sender.clone();
            fasync::Task::local(async move {
                serve_graphical_presenter(stream, view_spec_sender).await
            })
            .detach();
        },
    );

    let sessionmgr_proxy = instance
        .root
        .connect_to_protocol_at_exposed_dir::<fmodular_internal::SessionmgrMarker>()?;
    sessionmgr_proxy.initialize(
        "test_session_id",
        session_context_client_end,
        &mut services_for_agents_fs.host_services_list()?,
        services_from_sessionmgr_server_end,
        &mut link_token_pair.view_creation_token,
    )?;

    fasync::Task::local(services_for_agents_fs.collect()).detach();

    // Create a story to launch a mod.
    let puppet_master =
        connect_to_protocol_at_dir_root::<fmodular::PuppetMasterMarker>(&services_from_sessionmgr)?;

    let (story_puppet_master, story_puppet_master_server_end) =
        create_proxy::<fmodular::StoryPuppetMasterMarker>()?;

    puppet_master.control_story("test_story", story_puppet_master_server_end)?;

    // Launch the test mod.
    story_puppet_master.enqueue(
        &mut vec![fmodular::StoryCommand::AddMod(fmodular::AddMod {
            mod_name: vec![],
            mod_name_transitional: Some("test_mod".to_string()),
            surface_relation: fmodular::SurfaceRelation {
                arrangement: fmodular::SurfaceArrangement::None,
                dependency: fmodular::SurfaceDependency::None,
                emphasis: 1.0,
            },
            surface_parent_mod_name: None,
            intent: fmodular::Intent {
                action: None,
                handler: Some(TEST_MOD_URL.to_string()),
                parameters: None,
            },
        })]
        .iter_mut(),
    )?;
    let execute_result = story_puppet_master.execute().await?;
    assert_eq!(fmodular::ExecuteStatus::Ok, execute_result.status);

    let view_spec = view_spec_receiver
        .take(1)
        .next()
        .await
        .ok_or_else(|| anyhow!("expected to receive ViewSpec"))?;

    // The viewport creation token from graphical presenter should be signalled by the mock module,
    // ensuring that both have endpoints for the view creation channel pair.
    fasync::OnSignals::new(&view_spec.viewport_creation_token.unwrap().value, zx::Signals::USER_0)
        .await
        .unwrap();

    instance.destroy().await?;

    Ok(())
}

// Serves the `fuchsia.element.GraphicalPresenter` protocol.
async fn serve_graphical_presenter(
    mut stream: felement::GraphicalPresenterRequestStream,
    mut view_spec_sender: mpsc::Sender<felement::ViewSpec>,
) {
    let mut view_controller_requests = vec![];

    while let Some(felement::GraphicalPresenterRequest::PresentView {
        view_spec,
        view_controller_request,
        responder,
        ..
    }) = stream.try_next().await.expect("failed to serve GraphicalPresenter")
    {
        // Save the ViewController request so sessionmgr doesn't think that the view was dismissed.
        view_controller_requests.push(view_controller_request);

        view_spec_sender.try_send(view_spec).expect("failed to send ViewSpec");
        let _ = responder.send(&mut Ok(()));
    }
}

// Serves the `fuchsia.modular.Agent` protocol that exposes the `fuchsia.examples.Echo`
// protocol through a `ServiceProvider`.
async fn serve_echo_agent(mut stream: fmodular::AgentRequestStream) {
    while let Some(fmodular::AgentRequest::Connect { services, .. }) =
        stream.try_next().await.expect("failed to serve Agent")
    {
        fasync::Task::local(async move {
            let stream = services.into_stream().expect("failed to create ServiceProvider stream");
            serve_echo_serviceprovider(stream).await
        })
        .detach();
    }
}

// Serves the `fuchsia.sys.ServiceProvider` protocol that exposes the
// `fuchsia.examples.Echo` protocol.
async fn serve_echo_serviceprovider(mut stream: fsys::ServiceProviderRequestStream) {
    while let Some(fsys::ServiceProviderRequest::ConnectToService {
        service_name, channel, ..
    }) = stream.try_next().await.expect("failed to serve ServiceProvider")
    {
        assert_eq!("fuchsia.examples.Echo", service_name);

        fasync::Task::local(async move {
            let stream: fexamples::EchoRequestStream =
                ServerEnd::<fexamples::EchoMarker>::new(channel)
                    .into_stream()
                    .expect("failed to create ServiceProvider stream");
            serve_echo(stream).await.expect("failed to serve Echo")
        })
        .detach();
    }
}

// Serves the `fuchsia.examples.Echo` protocol.
async fn serve_echo(stream: fexamples::EchoRequestStream) -> Result<(), Error> {
    stream
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async move {
            match request {
                fexamples::EchoRequest::EchoString { value, responder } => {
                    responder.send(&value).context("error sending response")?;
                }
                fexamples::EchoRequest::SendString { value, control_handle } => {
                    control_handle.send_on_string(&value).context("error sending event")?;
                }
            }
            Ok(())
        })
        .await
}
