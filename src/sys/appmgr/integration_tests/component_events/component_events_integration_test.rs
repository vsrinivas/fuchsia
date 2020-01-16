// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#![allow(dead_code, unused_imports)]
use {
    anyhow::{format_err, Context, Error},
    fdio,
    fidl::endpoints::DiscoverableService,
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
    fuchsia_component::client::{connect_to_service, launch},
    fuchsia_inspect::{assert_inspect_tree, reader},
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
    listener_request_stream: ComponentEventListenerRequestStream,
    diagnostics_test_env: EnvironmentProxy,
    _diagnostics_test_env_ctrl: EnvironmentControllerProxy,
}

impl ComponentEventsTest {
    pub async fn new(env: &EnvironmentProxy) -> Result<Self, Error> {
        let (diagnostics_test_env, ctrl) =
            create_nested_environment(&env, "diagnostics_test").await?;
        let listener_request_stream =
            listen_for_component_events(&env).context("get component event provider")?;
        let mut test = Self {
            listener_request_stream,
            diagnostics_test_env,
            _diagnostics_test_env_ctrl: ctrl,
        };
        test.consume_self_events().await.expect("failed to consume self events");
        Ok(test)
    }

    // Asserts that we receive the expected events about this component itself.
    async fn consume_self_events(&mut self) -> Result<(), Error> {
        let self_realm_path = Vec::new();
        let request = self.listener_request_stream.try_next().await.context("Fetch self start")?;
        if let Some(ComponentEventListenerRequest::OnStart { component, .. }) = request {
            self.assert_identity(component, &SELF_COMPONENT, &self_realm_path);
        } else {
            return Err(format_err!("Expected start event for self"));
        }
        Ok(())
    }

    /// Asserts a start event for the test_component.
    async fn assert_on_start(&mut self) -> Result<(), Error> {
        let request = self.listener_request_stream.try_next().await.context("Fetch test start")?;
        if let Some(ComponentEventListenerRequest::OnStart { component, .. }) = request {
            self.assert_identity(component, &*TEST_COMPONENT, &*TEST_COMPONENT_REALM_PATH);
        } else {
            return Err(format_err!("Expected start event. Got: {:?}", request));
        }
        Ok(())
    }

    /// Asserts a stop event for the test_component.
    async fn assert_on_stop(&mut self) -> Result<(), Error> {
        let request = self.listener_request_stream.try_next().await.context("Fetch test stop")?;
        if let Some(ComponentEventListenerRequest::OnStop { component, .. }) = request {
            self.assert_identity(component, &*TEST_COMPONENT, &*TEST_COMPONENT_REALM_PATH);
        } else {
            return Err(format_err!("Expected stop event. Got: {:?}", request));
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
        assert_eq!(
            component.component_url,
            Some(format!(
                "fuchsia-pkg://fuchsia.com/component_events_integration_tests#meta/{}",
                name
            ))
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

    // Component manager will run with these environment options
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
        ComponentEventProviderMarker::SERVICE_NAME,
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
    let env = connect_to_service::<EnvironmentMarker>().expect("connect to current environment");
    let mut test = ComponentEventsTest::new(&env).await?;
    let arguments = vec!["with-diagnostics".to_string()];
    let launcher = get_launcher(&test.diagnostics_test_env).context("get launcher")?;
    let mut app = launch(&launcher, TEST_COMPONENT_URL.to_string(), Some(arguments))
        .context("start test component")?;

    test.assert_on_start().await?;

    let request = test.listener_request_stream.try_next().await.context("Fetch test dir ready")?;
    if let Some(ComponentEventListenerRequest::OnDiagnosticsDirReady {
        component, directory, ..
    }) = request
    {
        test.assert_identity(component, &*TEST_COMPONENT, &*TEST_COMPONENT_REALM_PATH);
        let (tree, server_end) =
            fidl::endpoints::create_proxy::<TreeMarker>().expect("Create Tree proxy");
        fdio::service_connect_at(
            directory.channel(),
            TreeMarker::SERVICE_NAME,
            server_end.into_channel(),
        )
        .expect("Connect to Tree service");
        let hierarchy = reader::read_from_tree(&tree).await.context("Get inspect hierarchy")?;
        assert_inspect_tree!(hierarchy, root: {
            "fuchsia.inspect.Health": {
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
    let env = connect_to_service::<EnvironmentMarker>().expect("connect to current environment");
    let mut test = ComponentEventsTest::new(&env).await?;
    let launcher = get_launcher(&test.diagnostics_test_env).context("get launcher")?;
    let mut app = launch(&launcher, TEST_COMPONENT_URL.to_string(), None)?;
    test.assert_on_start().await?;
    let request = test
        .listener_request_stream
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

// Tests that START events for components that exist before the listener is attached are
// propagated only to the first ancestor realm with a listener attached.
// This test does the following:
// 1. Create a realm hierarchy under the existing "test_env". This hierarchy looks like
//    "test_env->A->B"
// 2. Listen for events in B
// 3. Start a component in B
// 4. Listen for events in A
// 5. Even though there's a component running under B, A is never notified of this component as
//    existing given that there's already a listener on B.
#[fasync::run_singlethreaded(test)]
async fn test_register_listener_in_subrealm() -> Result<(), Error> {
    let env = connect_to_service::<EnvironmentMarker>().expect("connect to current environment");
    let _test = ComponentEventsTest::new(&env).await?;
    let (env_a, _a_ctrl) = create_nested_environment(&env, "a").await?;
    let (env_b, _b_ctrl) = create_nested_environment(&env_a, "b").await?;

    // Connect a ComponentEventProvider to B
    let _env_b_events_stream =
        listen_for_component_events(&env_b).context("failed to listen for events on b")?;

    // Start our test component in environment B
    let _app = launch(&get_launcher(&env_b)?, TEST_COMPONENT_URL.to_string(), None)?;

    // Attach a provider to A
    let mut env_a_events_stream =
        listen_for_component_events(&env_a).context("failed to listen for events on a")?;

    // Verify the listener on A is not notified about anything given that B already
    // is listening for events.
    let request = env_a_events_stream
        .try_next()
        .on_timeout(TIMEOUT_SECONDS.seconds().after_now(), || Ok(None))
        .await
        .context("Fetch start event on a")?;
    if let Some(ComponentEventListenerRequest::OnStart { .. }) = request {
        return Err(format_err!("Unexpected start event for test component on A"));
    }

    Ok(())
}
