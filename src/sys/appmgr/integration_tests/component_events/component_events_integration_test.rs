// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fdio,
    fidl::endpoints::DiscoverableProtocolMarker,
    fidl_fuchsia_inspect::TreeMarker,
    fidl_fuchsia_sys::{
        EnvironmentControllerEvent, EnvironmentControllerProxy, EnvironmentMarker,
        EnvironmentOptions, EnvironmentProxy, LauncherProxy, ServiceProviderMarker,
    },
    fidl_fuchsia_sys_internal::{
        ComponentEventListenerMarker, ComponentEventListenerRequest,
        ComponentEventListenerRequestStream, ComponentEventProviderMarker, SourceIdentity,
    },
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_component::client::{connect_to_protocol, launch},
    fuchsia_inspect::{assert_data_tree, reader},
    fuchsia_zircon::DurationNum,
    futures::stream::{StreamExt, TryStreamExt},
    lazy_static::lazy_static,
    regex::Regex,
};

lazy_static! {
    static ref TEST_COMPONENT: String = "test_component.cmx".to_string();
    static ref TEST_COMPONENT_URL: String =
        "fuchsia-pkg://fuchsia.com/component_events_integration_tests#meta/test_component.cmx"
            .to_string();
    static ref SELF_COMPONENT: String = "component_events_integration_test.cmx".to_string();
    static ref TEST_COMPONENT_REALM_PATH: Vec<String> = vec!["diagnostics_test".to_string()];
    static ref TIMEOUT_SECONDS: i64 = 1;
}

struct ComponentEventsTest {
    listener_request_stream: Option<ComponentEventListenerRequestStream>,
    diagnostics_test_env: EnvironmentProxy,
    _diagnostics_test_env_ctrl: EnvironmentControllerProxy,
}

impl ComponentEventsTest {
    pub async fn new(env: &EnvironmentProxy, with_listener: bool) -> Result<Self, Error> {
        let (diagnostics_test_env, ctrl) =
            create_nested_environment(&env, "diagnostics_test").await?;
        let listener_request_stream = if with_listener {
            Some(listen_for_component_events(&env).context("get component event provider")?)
        } else {
            None
        };
        let test = Self {
            listener_request_stream,
            diagnostics_test_env,
            _diagnostics_test_env_ctrl: ctrl,
        };
        Ok(test)
    }

    /// Asserts a stop event for the test_component.
    async fn assert_on_stop(&mut self) -> Result<(), Error> {
        match &mut self.listener_request_stream {
            None => {}
            Some(listener_request_stream) => {
                let request =
                    listener_request_stream.try_next().await.context("Fetch test stop")?;
                if let Some(ComponentEventListenerRequest::OnStop { component, .. }) = request {
                    self.assert_identity(component, &*TEST_COMPONENT, &*TEST_COMPONENT_REALM_PATH);
                } else {
                    return Err(format_err!("Expected stop event. Got: {:?}", request));
                }
            }
        }
        Ok(())
    }

    /// Utility function to assert that a `SourceIdentity` is the expected one.
    fn assert_identity(
        &self,
        component: SourceIdentity,
        name: &str,
        expected_realm_path: &[String],
    ) {
        assert_eq!(component.component_name, Some(name.to_string()));
        let mut url = fuchsia_url::AbsoluteComponentUrl::parse(&component.component_url.unwrap())
            .expect("cannot parse url");
        url.clear_variant();

        assert_eq!(
            url,
            fuchsia_url::AbsoluteComponentUrl::new(
                "fuchsia-pkg://fuchsia.com".parse().unwrap(),
                "component_events_integration_tests".parse().unwrap(),
                None,
                url.hash().map(|h| h.clone()),
                format!("meta/{}", name),
            )
            .unwrap()
        );
        let instance_id = component.instance_id.expect("no instance id");
        assert!(Regex::new("[0-9]+").unwrap().is_match(&instance_id));
        let realm_path = component.realm_path.unwrap();
        assert_eq!(realm_path, expected_realm_path);
    }
}

async fn create_nested_environment(
    parent: &EnvironmentProxy,
    label: &str,
) -> Result<(EnvironmentProxy, EnvironmentControllerProxy), Error> {
    let (new_env, new_env_server_end) =
        fidl::endpoints::create_proxy::<EnvironmentMarker>().context("could not create proxy")?;
    let (controller, controller_server_end) =
        fidl::endpoints::create_proxy().context("could not create proxy")?;

    let mut env_options = EnvironmentOptions {
        inherit_parent_services: true,
        use_parent_runners: true,
        kill_on_oom: true,
        delete_storage_on_death: true,
    };

    parent
        .create_nested_environment(
            new_env_server_end,
            controller_server_end,
            label,
            None,
            &mut env_options,
        )
        .context("could not create isolated environment")?;

    let EnvironmentControllerEvent::OnCreated {} = controller
        .take_event_stream()
        .next()
        .await
        .unwrap()
        .expect("failed to get env created event");

    Ok((new_env, controller))
}

fn listen_for_component_events(
    env: &EnvironmentProxy,
) -> Result<ComponentEventListenerRequestStream, Error> {
    let (service_provider, server_end) =
        fidl::endpoints::create_proxy::<ServiceProviderMarker>().context("create proxy")?;
    env.get_services(server_end)?;

    let (event_provider, server_end) =
        fidl::endpoints::create_proxy::<ComponentEventProviderMarker>().context("create proxy")?;
    service_provider.connect_to_service(
        ComponentEventProviderMarker::PROTOCOL_NAME,
        server_end.into_channel(),
    )?;

    let (events_client_end, listener_request_stream) =
        fidl::endpoints::create_request_stream::<ComponentEventListenerMarker>()
            .context("Create request stream")?;
    event_provider.set_listener(events_client_end)?;
    Ok(listener_request_stream)
}

fn get_launcher(env: &EnvironmentProxy) -> Result<LauncherProxy, Error> {
    let (launcher, launcher_server_end) =
        fidl::endpoints::create_proxy().context("could not create proxy")?;
    env.get_launcher(launcher_server_end).context("could not get isolated environment launcher")?;
    Ok(launcher)
}

// Asserts that we receive the expected events about a component that contains an
// out/diagnostics directory: START, STOP, OUT_DIR_READY.
#[fasync::run_singlethreaded(test)]
async fn test_with_diagnostics() -> Result<(), Error> {
    let env = connect_to_protocol::<EnvironmentMarker>().expect("connect to current environment");
    let mut test = ComponentEventsTest::new(&env, true).await?;
    let arguments = vec!["with-diagnostics".to_string()];
    let launcher = get_launcher(&test.diagnostics_test_env).context("get launcher")?;
    let mut app = launch(&launcher, TEST_COMPONENT_URL.to_string(), Some(arguments))
        .context("start test component")?;

    let request = test
        .listener_request_stream
        .as_mut()
        .unwrap()
        .try_next()
        .await
        .context("Fetch test dir ready")?;
    if let Some(ComponentEventListenerRequest::OnDiagnosticsDirReady {
        component, directory, ..
    }) = request
    {
        test.assert_identity(component, &*TEST_COMPONENT, &*TEST_COMPONENT_REALM_PATH);
        let (tree, server_end) =
            fidl::endpoints::create_proxy::<TreeMarker>().expect("Create Tree proxy");
        fdio::service_connect_at(
            directory.channel(),
            TreeMarker::PROTOCOL_NAME,
            server_end.into_channel(),
        )
        .expect("Connect to Tree service");
        let hierarchy = reader::read(&tree).await.context("Get inspect hierarchy")?;
        assert_data_tree!(hierarchy, root: {
            "fuchsia.inspect.Health": contains {
                status: "OK",
            }
        });
    } else {
        return Err(format_err!("Expected diagnostics directory ready event. Got: {:?}", request));
    }

    app.kill().context("Kill app")?;
    test.assert_on_stop().await?;

    Ok(())
}

// Asserts that we receive the expected events about a component that doesn't contain an
// out/diagnostics directory: START, STOP.
#[fasync::run_singlethreaded(test)]
async fn test_without_diagnostics() -> Result<(), Error> {
    let env = connect_to_protocol::<EnvironmentMarker>().expect("connect to current environment");
    let mut test = ComponentEventsTest::new(&env, true).await?;
    let launcher = get_launcher(&test.diagnostics_test_env).context("get launcher")?;
    let mut app = launch(&launcher, TEST_COMPONENT_URL.to_string(), None)?;
    let request = test
        .listener_request_stream
        .as_mut()
        .unwrap()
        .try_next()
        .on_timeout(TIMEOUT_SECONDS.seconds().after_now(), || Ok(None))
        .await
        .context("Fetch start event on a")?;
    if let Some(ComponentEventListenerRequest::OnDiagnosticsDirReady { .. }) = request {
        return Err(format_err!(
            "Unexpected diagnostics dir ready event for component without diagnostics"
        ));
    }
    app.kill().context("Kill app")?;
    test.assert_on_stop().await?;
    Ok(())
}
