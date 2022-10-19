// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, Ref, Route},
    tracing::*,
};

// This test demonstrates constructing a realm with two child components
// and verifying the `fidl.examples.routing.Echo` protocol.
#[fuchsia::test]
async fn routes_from_echo() {
    let builder = RealmBuilder::new().await.unwrap();

    // Add the server component to the realm, fetched from a subpackage
    let echo_server = builder
        // TODO(fxbug.dev/100060): When full-resolver is updated to call
        // PackageResolver::ResolveWithContext, it will allow subpackages to
        // be renamed. At that time, update BUILD.gn to give the
        // echo_server_package an explicit name ("my-echo-server"), and replace
        // the following line with the commented out version below it:
        .add_child("echo_server", "echo_server#meta/default.cm", ChildOptions::new())
        // .add_child("echo_server", "my-echo-server#meta/default.cm",
        //            ChildOptions::new())
        .await
        .unwrap();

    // Add the client component to the realm, fetched from a subpackage, using a
    // name that is still scoped to the parent package, but the name matches the
    // package's top-level name. In `BUILD.gn` the subpackage name defaults to
    // the referenced package name, unless an explicit subpackage name is
    // declared.
    let echo_client = builder
        .add_child("echo_client", "echo_client#meta/default.cm", ChildOptions::new().eager())
        .await
        .unwrap();

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fidl.examples.routing.echo.Echo"))
                .from(&echo_server)
                .to(&echo_client),
        )
        .await
        .unwrap();

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&echo_server)
                .to(&echo_client),
        )
        .await
        .unwrap();

    // Subscribe to stopped events for child components
    let mut event_stream = EventStream::open_at_path("/events/stopped").await.unwrap();

    let realm = builder.build().await.unwrap();

    println!("Child Name: {}", realm.root.child_name());

    info!("awaiting echo_client Stopped event");
    EventMatcher::ok()
        .moniker_regex("./echo_client")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}
