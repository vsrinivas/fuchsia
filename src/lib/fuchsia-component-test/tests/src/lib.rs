// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::server as fserver,
    fuchsia_component_test::{builder::*, mock},
    futures::{channel::oneshot, lock::Mutex, StreamExt, TryStreamExt},
    log::*,
    std::sync::Arc,
};

const V1_ECHO_CLIENT_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/fuchsia-component-test-tests#meta/echo_client.cmx";
const V2_ECHO_CLIENT_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/fuchsia-component-test-tests#meta/echo_client.cm";

const V1_ECHO_SERVER_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/fuchsia-component-test-tests#meta/echo_server.cmx";
const V2_ECHO_SERVER_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/fuchsia-component-test-tests#meta/echo_server.cm";

const ECHO_STR: &'static str = "Hippos rule!";

#[fasync::run_singlethreaded(test)]
async fn protocol_with_uncle_test() -> Result<(), Error> {
    let (send_echo_server_called, receive_echo_server_called) = oneshot::channel();
    let sender = Arc::new(Mutex::new(Some(send_echo_server_called)));

    let mut builder = RealmBuilder::new().await?;
    builder
        .add_component(
            "echo-server",
            ComponentSource::Mock(mock::Mock::new(move |mock_handles: mock::MockHandles| {
                Box::pin(echo_server_mock(sender.clone(), mock_handles))
            })),
        )
        .await?
        .add_eager_component("parent/echo-client", ComponentSource::url(V2_ECHO_CLIENT_URL))
        .await?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
            source: RouteEndpoint::component("echo-server"),
            targets: vec![RouteEndpoint::component("parent/echo-client")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.LogSink"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![
                RouteEndpoint::component("echo-server"),
                RouteEndpoint::component("parent/echo-client"),
            ],
        })?;
    let _child_instance = builder.build().create().await?;

    receive_echo_server_called.await??;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn protocol_with_siblings_test() -> Result<(), Error> {
    let (send_echo_server_called, receive_echo_server_called) = oneshot::channel();
    let sender = Arc::new(Mutex::new(Some(send_echo_server_called)));

    let mut builder = RealmBuilder::new().await?;
    builder
        .add_eager_component("echo-client", ComponentSource::url(V2_ECHO_CLIENT_URL))
        .await?
        .add_component(
            "echo-server",
            ComponentSource::Mock(mock::Mock::new(move |mock_handles: mock::MockHandles| {
                Box::pin(echo_server_mock(sender.clone(), mock_handles))
            })),
        )
        .await?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
            source: RouteEndpoint::component("echo-server"),
            targets: vec![RouteEndpoint::component("echo-client")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.LogSink"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![
                RouteEndpoint::component("echo-client"),
                RouteEndpoint::component("echo-server"),
            ],
        })?;
    let _child_instance = builder.build().create().await?;

    receive_echo_server_called.await??;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn protocol_with_cousins_test() -> Result<(), Error> {
    let (send_echo_server_called, receive_echo_server_called) = oneshot::channel();
    let sender = Arc::new(Mutex::new(Some(send_echo_server_called)));

    let mut builder = RealmBuilder::new().await.unwrap();
    builder
        .add_eager_component("parent-1/echo-client", ComponentSource::url(V2_ECHO_CLIENT_URL))
        .await?
        .add_component(
            "parent-2/echo-server",
            ComponentSource::Mock(mock::Mock::new(move |mock_handles: mock::MockHandles| {
                Box::pin(echo_server_mock(sender.clone(), mock_handles))
            })),
        )
        .await?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
            source: RouteEndpoint::component("parent-2/echo-server"),
            targets: vec![RouteEndpoint::component("parent-1/echo-client")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.LogSink"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![
                RouteEndpoint::component("parent-1/echo-client"),
                RouteEndpoint::component("parent-2/echo-server"),
            ],
        })?;
    let _child_instance = builder.build().create().await.unwrap();

    receive_echo_server_called.await.unwrap().unwrap();
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn mock_component_with_a_child() -> Result<(), Error> {
    let (send_echo_server_called, receive_echo_server_called) = oneshot::channel();
    let sender = Arc::new(Mutex::new(Some(send_echo_server_called)));

    let mut builder = RealmBuilder::new().await?;
    builder
        .add_component(
            "echo-server",
            ComponentSource::Mock(mock::Mock::new(move |mock_handles: mock::MockHandles| {
                Box::pin(echo_server_mock(sender.clone(), mock_handles))
            })),
        )
        .await?
        .add_eager_component("echo-server/echo-client", ComponentSource::url(V2_ECHO_CLIENT_URL))
        .await?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
            source: RouteEndpoint::component("echo-server"),
            targets: vec![RouteEndpoint::component("echo-server/echo-client")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.LogSink"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![
                RouteEndpoint::component("echo-server"),
                RouteEndpoint::component("echo-server/echo-client"),
            ],
        })?;
    let _child_instance = builder.build().create().await?;

    receive_echo_server_called.await??;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn echo_clients() -> Result<(), Error> {
    // This test runs a series of echo clients from different sources against a mock echo server,
    // confirming that each client successfully connects to the server.

    let (send_echo_client_results, receive_echo_client_results) = oneshot::channel();
    let sender = Arc::new(Mutex::new(Some(send_echo_client_results)));
    let client_sources = vec![
        ComponentSource::legacy_url(V1_ECHO_CLIENT_URL),
        ComponentSource::url(V2_ECHO_CLIENT_URL),
        ComponentSource::mock(move |h| Box::pin(echo_client_mock(sender.clone(), h))),
    ];

    for client_source in client_sources {
        let (send_echo_server_called, receive_echo_server_called) = oneshot::channel();
        let sender = Arc::new(Mutex::new(Some(send_echo_server_called)));

        let mut builder = RealmBuilder::new().await?;
        builder
            .add_component(
                "echo-server",
                ComponentSource::mock(move |h| Box::pin(echo_server_mock(sender.clone(), h))),
            )
            .await?
            .add_eager_component("echo-client", client_source)
            .await?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                source: RouteEndpoint::component("echo-server"),
                targets: vec![RouteEndpoint::component("echo-client")],
            })?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.logger.LogSink"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![
                    RouteEndpoint::component("echo-server"),
                    RouteEndpoint::component("echo-client"),
                ],
            })?;

        let _child_instance = builder.build().create().await?;

        receive_echo_server_called.await??;
    }

    receive_echo_client_results.await??;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn echo_servers() -> Result<(), Error> {
    // This test runs a series of echo servers from different sources against a mock echo client,
    // confirming that the client can successfully connect to and use each server.

    let (send_echo_server_called, receive_echo_server_called) = oneshot::channel();
    let sender = Arc::new(Mutex::new(Some(send_echo_server_called)));

    let server_sources = vec![
        ComponentSource::legacy_url(V1_ECHO_SERVER_URL),
        ComponentSource::url(V2_ECHO_SERVER_URL),
        ComponentSource::mock(move |h| Box::pin(echo_server_mock(sender.clone(), h))),
    ];

    for server_source in server_sources {
        info!("running test for server {:?}", server_source);
        let (send_echo_client_results, receive_echo_client_results) = oneshot::channel();
        let sender = Arc::new(Mutex::new(Some(send_echo_client_results)));

        let mut builder = RealmBuilder::new().await?;
        builder
            .add_component("echo-server", server_source)
            .await?
            .add_eager_component(
                "echo-client",
                ComponentSource::mock(move |h| Box::pin(echo_client_mock(sender.clone(), h))),
            )
            .await?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                source: RouteEndpoint::component("echo-server"),
                targets: vec![RouteEndpoint::component("echo-client")],
            })?
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fuchsia.logger.LogSink"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![
                    RouteEndpoint::component("echo-server"),
                    RouteEndpoint::component("echo-client"),
                ],
            })?;

        let _child_instance = builder.build().create().await?;

        receive_echo_client_results.await??;
        info!("success!");
    }

    receive_echo_server_called.await??;
    Ok(())
}

// A mock echo server implementation, that will crash if it doesn't receive anything other than the
// contents of `ECHO_STR`. It takes and sends a message over `send_echo_server_called` once it
// receives one echo request.
async fn echo_server_mock(
    send_echo_server_called: Arc<Mutex<Option<oneshot::Sender<Result<(), Error>>>>>,
    mock_handles: mock::MockHandles,
) -> Result<(), Error> {
    let mut fs = fserver::ServiceFs::new();
    let mut tasks = vec![];
    fs.dir("svc").add_fidl_service(move |mut stream: fecho::EchoRequestStream| {
        let send_echo_server_called = send_echo_server_called.clone();
        tasks.push(fasync::Task::local(async move {
            while let Some(fecho::EchoRequest::EchoString { value, responder }) =
                stream.try_next().await.expect("failed to serve echo service")
            {
                assert_eq!(Some(ECHO_STR.to_string()), value);
                responder.send(value.as_ref().map(|s| &**s)).expect("failed to send echo response");
                send_echo_server_called
                    .lock()
                    .await
                    .take()
                    .unwrap()
                    .send(Ok(()))
                    .expect("failed to send results");
            }
        }));
    });
    fs.serve_connection(mock_handles.outgoing_dir.into_channel())?;
    fs.collect::<()>().await;
    Ok(())
}

async fn echo_client_mock(
    send_echo_client_results: Arc<Mutex<Option<oneshot::Sender<Result<(), Error>>>>>,
    mock_handles: mock::MockHandles,
) -> Result<(), Error> {
    let echo = mock_handles.connect_to_service::<fecho::EchoMarker>()?;
    let out = echo.echo_string(Some(ECHO_STR)).await?;
    send_echo_client_results
        .lock()
        .await
        .take()
        .unwrap()
        .send(Ok(()))
        .expect("failed to send results");
    if Some(ECHO_STR.to_string()) != out {
        return Err(format_err!("unexpected echo result: {:?}", out));
    }
    Ok(())
}
