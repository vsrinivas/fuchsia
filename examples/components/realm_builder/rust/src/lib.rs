// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START import_statement_rust]
use {
    // [START_EXCLUDE]
    anyhow::{self, Error},
    fidl_fidl_examples_routing_echo as fecho,
    fuchsia_async::{
        self as fasync,
        futures::{StreamExt, TryStreamExt},
    },
    fuchsia_component::server as fserver,
    // [END_EXCLUDE]
    fuchsia_component_test::{
        mock::MockHandles, ChildOptions, RealmBuilder, RouteBuilder, RouteEndpoint,
    },
};
// [END import_statement_rust]

#[fuchsia::test]
async fn make_echo_call() -> Result<(), Error> {
    // [START init_realm_builder_rust]
    let builder = RealmBuilder::new().await?;
    // [END init_realm_builder_rust]

    // [START add_component_rust]
    builder
        // Add component `a` to the realm, which is fetched using a URL.
        .add_child(
            "a",
            "fuchsia-pkg://fuchsia.com/realm-builder-examples#meta/echo_client.cm",
            ChildOptions::new(),
        )
        .await?
        // Add component `b` to the realm, which is fetched using a relative URL.
        .add_child("b", "#meta/echo_client.cm", ChildOptions::new())
        .await?;
    // [END add_component_rust]

    // [START add_legacy_component_rust]
    builder
        // Add component `c` to the realm, which is fetched using a legacy URL.
        .add_legacy_child(
            "c",
            "fuchsia-pkg://fuchsia.com/realm-builder-examples#meta/echo_client.cmx",
            ChildOptions::new(),
        )
        .await?;
    // [END add_legacy_component_rust]

    // [START add_mock_component_rust]
    builder
        .add_mock_child(
            "d",
            move |mock_handles: MockHandles| Box::pin(echo_server_mock(mock_handles)),
            ChildOptions::new(),
        )
        .await?;
    // [END add_mock_component_rust]

    // [START route_between_children_rust]
    builder
        .add_route(
            RouteBuilder::protocol("fidl.examples.routing.echo.Echo")
                .source(RouteEndpoint::component("d"))
                .targets(vec![
                    RouteEndpoint::component("a"),
                    RouteEndpoint::component("b"),
                    RouteEndpoint::component("c"),
                ]),
        )
        .await?;
    // [END route_between_children_rust]

    // [START route_to_test_rust]
    builder
        .add_route(
            RouteBuilder::protocol("fidl.examples.routing.echo.Echo")
                .source(RouteEndpoint::component("d"))
                .targets(vec![RouteEndpoint::above_root()]),
        )
        .await?;
    // [END route_to_test_rust]

    // [START route_from_test_rust]
    builder
        .add_route(
            RouteBuilder::protocol("fuchsia.logger.LogSink")
                .source(RouteEndpoint::above_root())
                .targets(vec![
                    RouteEndpoint::component("a"),
                    RouteEndpoint::component("b"),
                    RouteEndpoint::component("c"),
                    RouteEndpoint::component("d"),
                ]),
        )
        .await?;
    // [END route_from_test_rust]

    // [START route_from_test_sibling_rust]
    builder
        .add_route(
            RouteBuilder::protocol("fuchsia.example.Foo")
                .source(RouteEndpoint::above_root())
                .targets(vec![RouteEndpoint::component("a")]),
        )
        .await?;
    // [END route_from_test_sibling_rust]

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

// [START mock_component_impl_rust]
async fn echo_server_mock(mock_handles: MockHandles) -> Result<(), Error> {
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
    fs.serve_connection(mock_handles.outgoing_dir.into_channel())?;
    fs.collect::<()>().await;

    Ok(())
}
// [END mock_component_impl_rust]
