// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START import_statement_rust]
use {
    // [START_EXCLUDE]
    anyhow::{self, Error},
    fidl_fidl_examples_routing_echo as fecho,
    fuchsia_async as fasync,
    fuchsia_component::server as fserver,
    // [END_EXCLUDE]
    fuchsia_component_test::{
        Capability, ChildOptions, LocalComponentHandles, RealmBuilder, Ref, Route,
    },
    futures::{StreamExt, TryStreamExt},
};
// [END import_statement_rust]

// This test demonstrates constructing a realm with two child components
// and verifying the `fidl.examples.routing.Echo` protocol.
#[fuchsia::test]
async fn routes_from_echo() -> Result<(), Error> {
    // [START init_realm_builder_rust]
    let builder = RealmBuilder::new().await?;
    // [END init_realm_builder_rust]

    // [START add_component_rust]
    // [START add_server_rust]
    // Add component to the realm, which is fetched using a URL.
    let echo_server = builder
        .add_child(
            "echo_server",
            "fuchsia-pkg://fuchsia.com/realm-builder-examples#meta/echo_server.cm",
            ChildOptions::new(),
        )
        .await?;
    // [END add_server_rust]
    // Add component to the realm, which is fetched using a relative URL. The
    // child is not exposing a service, so the `eager` option ensures the child
    // starts when the realm is built.
    let echo_client = builder
        .add_child("echo_client", "#meta/echo_client.cm", ChildOptions::new().eager())
        .await?;
    // [END add_component_rust]

    // [START route_between_children_rust]
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fidl.examples.routing.echo.Echo"))
                .from(&echo_server)
                .to(&echo_client),
        )
        .await?;
    // [END route_between_children_rust]

    // [START route_to_test_rust]
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fidl.examples.routing.echo.Echo"))
                .from(&echo_server)
                .to(Ref::parent()),
        )
        .await?;
    // [END route_to_test_rust]

    // [START route_from_test_rust]
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&echo_server)
                .to(&echo_client),
        )
        .await?;
    // [END route_from_test_rust]

    // [START build_realm_rust]
    let realm = builder.build().await?;
    // [END build_realm_rust]

    // [START get_child_name_rust]
    println!("Child Name: {}", realm.root.child_name());
    // [END get_child_name_rust]

    // [START call_echo_rust]
    let echo = realm.root.connect_to_protocol_at_exposed_dir::<fecho::EchoMarker>()?;
    assert_eq!(echo.echo_string(Some("hello")).await?, Some("hello".to_owned()));
    // [END call_echo_rust]

    Ok(())
}

// This test demonstrates constructing a realm with a single legacy component
// implementation of the `fidl.examples.routing.Echo` protocol.
#[fuchsia::test]
async fn routes_from_legacy_echo() -> Result<(), Error> {
    let builder = RealmBuilder::new().await?;

    // [START add_legacy_component_rust]
    // Add component `c` to the realm, which is fetched using a legacy URL.
    let echo_server = builder
        .add_legacy_child(
            "echo_server",
            "fuchsia-pkg://fuchsia.com/realm-builder-examples#meta/echo_server.cmx",
            ChildOptions::new(),
        )
        .await?;
    // [END add_legacy_component_rust]

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&echo_server),
        )
        .await?;

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fidl.examples.routing.echo.Echo"))
                .from(&echo_server)
                .to(Ref::parent()),
        )
        .await?;

    let realm = builder.build().await?;

    let echo = realm.root.connect_to_protocol_at_exposed_dir::<fecho::EchoMarker>()?;
    assert_eq!(echo.echo_string(Some("hello")).await?, Some("hello".to_owned()));

    Ok(())
}

// [START mock_component_impl_rust]
async fn echo_server_mock(handles: LocalComponentHandles) -> Result<(), Error> {
    // Create a new ServiceFs to host FIDL protocols from
    let mut fs = fserver::ServiceFs::new();
    let mut tasks = vec![];

    // Add the echo protocol to the ServiceFs
    fs.dir("svc").add_fidl_service(move |mut stream: fecho::EchoRequestStream| {
        tasks.push(fasync::Task::local(async move {
            while let Some(fecho::EchoRequest::EchoString { value, responder }) =
                stream.try_next().await.expect("failed to serve echo service")
            {
                responder.send(value.as_ref().map(|s| &**s)).expect("failed to send echo response");
            }
        }));
    });

    // Run the ServiceFs on the outgoing directory handle from the mock handles
    fs.serve_connection(handles.outgoing_dir.into_channel())?;
    fs.collect::<()>().await;

    Ok(())
}
// [END mock_component_impl_rust]

// This test demonstrates constructing a realm with a mocked LocalComponent
// implementation of the `fidl.examples.routing.Echo` protocol.
#[fuchsia::test]
async fn routes_from_mock_echo() -> Result<(), Error> {
    let builder = RealmBuilder::new().await?;

    // [START add_mock_component_rust]
    let echo_server = builder
        .add_local_child(
            "echo_server",
            move |handles: LocalComponentHandles| Box::pin(echo_server_mock(handles)),
            ChildOptions::new(),
        )
        .await?;
    // [END add_mock_component_rust]

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&echo_server),
        )
        .await?;

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fidl.examples.routing.echo.Echo"))
                .from(&echo_server)
                .to(Ref::parent()),
        )
        .await?;

    let realm = builder.build().await?;

    let echo = realm.root.connect_to_protocol_at_exposed_dir::<fecho::EchoMarker>()?;
    assert_eq!(echo.echo_string(Some("hello")).await?, Some("hello".to_owned()));

    Ok(())
}
