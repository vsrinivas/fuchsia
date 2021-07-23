// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This test component creates a test component under the environment `sub`.  This component has the option:
//! - with-event-listener: listens for started events of sub components.

use {
    anyhow::{Context, Error},
    fidl::endpoints::DiscoverableProtocolMarker,
    fidl_fuchsia_sys::{
        EnvironmentControllerEvent, EnvironmentControllerProxy, EnvironmentMarker,
        EnvironmentOptions, EnvironmentProxy, ServiceProviderMarker,
    },
    fidl_fuchsia_sys_internal::{
        ComponentEventListenerMarker, ComponentEventListenerRequest,
        ComponentEventListenerRequestStream, ComponentEventProviderMarker,
        ComponentEventProviderProxy,
    },
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{connect_to_protocol, launch},
        server::ServiceFs,
    },
    futures::prelude::*,
    lazy_static::lazy_static,
};

lazy_static! {
    static ref TEST_COMPONENT_URL: &'static str =
        "fuchsia-pkg://fuchsia.com/component_events_integration_tests#meta/test_component.cmx";
    static ref SELF_COMPONENT: &'static str = "test_component_with_subrealm.cmx";
    static ref TEST_COMPONENT: &'static str = "test_component.cmx";
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
) -> Result<(ComponentEventProviderProxy, ComponentEventListenerRequestStream), Error> {
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
    Ok((event_provider, listener_request_stream))
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new();

    let env = connect_to_protocol::<EnvironmentMarker>().expect("connect to current environment");
    let (nested_env, _env_controller) =
        create_nested_environment(&env, "sub").await.context("create env")?;

    let args: Vec<String> = std::env::args().collect();

    let (launcher, launcher_server_end) =
        fidl::endpoints::create_proxy().context("launcher proxy")?;
    nested_env.get_launcher(launcher_server_end).context("get launcher")?;
    let _app = launch(&launcher, TEST_COMPONENT_URL.to_string(), None)?;

    // Keep provider and stream alive for the lifetime of the program.
    let (provider, mut stream) = {
        if args.len() > 1 && args[1] == "with-event-listener" {
            let (p, s) = listen_for_component_events(&env).context("listen for events")?;
            (Some(p), Some(s))
        } else {
            (None, None)
        }
    };
    match (provider.as_ref(), stream.as_mut()) {
        (Some(_), Some(stream)) => {
            let request = stream.try_next().await.expect("Fetch self start");
            if let Some(ComponentEventListenerRequest::OnStart { component, .. }) = request {
                assert_eq!(component.component_name, Some(SELF_COMPONENT.to_string()));
            }
            let request = stream.try_next().await.expect("Fetch component start");
            if let Some(ComponentEventListenerRequest::OnStart { component, .. }) = request {
                assert_eq!(component.component_name, Some(TEST_COMPONENT.to_string()));
            }
        }
        _ => {}
    }

    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}
