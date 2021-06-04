// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::server as fserver,
    fuchsia_component_test::{builder::*, mock},
    futures::{channel::oneshot, lock::Mutex, StreamExt, TryStreamExt},
    std::sync::Arc,
};

const ECHO_CLIENT_URL: &'static str =
    "fuchsia-pkg://fuchsia.com/fuchsia-component-test-tests#meta/echo_client.cm";

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
        .add_eager_component("parent/echo-client", ComponentSource::url(ECHO_CLIENT_URL))
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
        .add_eager_component("echo-client", ComponentSource::url(ECHO_CLIENT_URL))
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
        .add_eager_component("parent-1/echo-client", ComponentSource::url(ECHO_CLIENT_URL))
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
        .add_eager_component("echo-server/echo-client", ComponentSource::url(ECHO_CLIENT_URL))
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

async fn echo_server_mock(
    send_echo_server_called: Arc<Mutex<Option<oneshot::Sender<Result<(), Error>>>>>,
    mock_handles: mock::MockHandles,
) -> Result<(), Error> {
    let mut fs = fserver::ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |mut stream: fecho::EchoRequestStream| {
        let send_echo_server_called = send_echo_server_called.clone();
        fasync::Task::local(async move {
            while let Some(fecho::EchoRequest::EchoString { value, responder }) =
                stream.try_next().await.expect("failed to serve echo service")
            {
                responder.send(value.as_ref().map(|s| &**s)).expect("failed to send echo response");
                send_echo_server_called
                    .lock()
                    .await
                    .take()
                    .unwrap()
                    .send(Ok(()))
                    .expect("failed to send results");
            }
        })
        .detach();
    });
    fs.serve_connection(mock_handles.outgoing_dir.into_channel())?;
    fs.collect::<()>().await;
    Ok(())
}
