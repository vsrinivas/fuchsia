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
    fuchsia_component_test::new::{
        Capability, ChildOptions, LocalComponentHandles, RealmBuilder, Ref, Route,
    },
};
// [END import_statement_rust]

#[fuchsia::test]
async fn make_echo_call() -> Result<(), Error> {
    // [START init_realm_builder_rust]
    let builder = RealmBuilder::new().await?;
    // [END init_realm_builder_rust]

    // [START add_component_rust]
    // Add component `a` to the realm, which is fetched using a URL.
    let a = builder
        .add_child(
            "a",
            "fuchsia-pkg://fuchsia.com/realm-builder-examples#meta/echo_client.cm",
            ChildOptions::new(),
        )
        .await?;
    // Add component `b` to the realm, which is fetched using a relative URL.
    let b = builder.add_child("b", "#meta/echo_client.cm", ChildOptions::new()).await?;
    // [END add_component_rust]

    // [START add_legacy_component_rust]
    // Add component `c` to the realm, which is fetched using a legacy URL.
    let c = builder
        .add_legacy_child(
            "c",
            "fuchsia-pkg://fuchsia.com/realm-builder-examples#meta/echo_client.cmx",
            ChildOptions::new(),
        )
        .await?;
    // [END add_legacy_component_rust]

    // [START add_mock_component_rust]
    let d = builder
        .add_local_child(
            "d",
            move |handles: LocalComponentHandles| Box::pin(echo_server_mock(handles)),
            ChildOptions::new(),
        )
        .await?;
    // [END add_mock_component_rust]

    // [START route_between_children_rust]
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fidl.examples.routing.echo.Echo"))
                .from(&d)
                .to(&a)
                .to(&b)
                .to(&c),
        )
        .await?;
    // [END route_between_children_rust]

    // [START route_to_test_rust]
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fidl.examples.routing.echo.Echo"))
                .from(&d)
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
                .to(&a)
                .to(&b)
                .to(&c)
                .to(&d),
        )
        .await?;
    // [END route_from_test_rust]

    // [START route_from_test_sibling_rust]
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.example.Foo"))
                .from(Ref::parent())
                .to(&a),
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
