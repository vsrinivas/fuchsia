// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fidl_examples_routing_echo::EchoMarker, fuchsia_component::client, tracing::info};

// [START imports]
use fidl_fuchsia_component::{BinderMarker, CreateChildArgs, RealmMarker};
use fidl_fuchsia_component_decl::{Child, ChildRef, CollectionRef, StartupMode};
// [END imports]

#[fuchsia::main(logging_tags = ["lifecycle", "example"])]
async fn main() {
    // Read program arguments, and strip off binary name
    let args: Vec<String> = std::env::args().skip(1).collect();

    // Connect to the fuchsia.component.Binder capability exposed by the static
    // child instance, causing it to start.
    info!("Starting lifecycle child instance.");
    let _binder = client::connect_to_protocol::<BinderMarker>()
        .expect("failed to connect to fuchsia.component.Binder");

    // Create a dynamic child instance in the collection, send a protocol
    // request, then destroy the child instance.
    for message in args {
        info!("Sending request: {}", message);

        create_dynamic_child().await;
        info!("Dynamic echo instance created.");

        connect_dynamic_child(message).await;

        destroy_dynamic_child().await;
        info!("Dynamic echo instance destroyed.");
    }
}

// [START connect_child]
// Connect to the fidl.examples.routing.echo capability exposed by the child
// instance, and send a request.
async fn connect_dynamic_child(message: String) {
    // Open the child's exposed directory
    let exposed_dir = client::open_childs_exposed_directory(
        String::from("lifecycle_dynamic"),
        Some(String::from("echo")),
    )
    .await
    .expect("failed to open exposed directory");

    // [START_EXCLUDE]
    // [START echo_send]
    // Access the fidl.examples.routing.echo capability provided there
    let echo = client::connect_to_protocol_at_dir_root::<EchoMarker>(&exposed_dir)
        .expect("failed to connect to fidl.examples.routing.echo");

    let response = echo
        .echo_string(Some(&message))
        .await
        .expect("echo_string failed")
        .expect("echo_string got empty result");
    info!("Server response: {}", response);
    // [END echo_send]
    // [END_EXCLUDE]
}
// [END connect_child]

// [START create_child]
// Use the fuchsia.component.Realm protocol to create a dynamic
// child instance in the collection.
async fn create_dynamic_child() {
    let realm = client::connect_to_protocol::<RealmMarker>()
        .expect("failed to connect to fuchsia.component.Realm");

    let mut collection_ref = CollectionRef { name: String::from("echo") };
    let child_decl = Child {
        name: Some(String::from("lifecycle_dynamic")),
        url: Some(String::from("#meta/echo_server.cm")),
        startup: Some(StartupMode::Lazy),
        ..Child::EMPTY
    };

    realm
        .create_child(&mut collection_ref, child_decl, CreateChildArgs::EMPTY)
        .await
        .expect("create_child failed")
        .expect("failed to create child");
}
// [END create_child]

// [START destroy_child]
// Use the fuchsia.component.Realm protocol to destroy the dynamic
// child instance running in the collection.
async fn destroy_dynamic_child() {
    let realm = client::connect_to_protocol::<RealmMarker>()
        .expect("failed to connect to fuchsia.component.Realm");

    let mut child_ref = ChildRef {
        name: String::from("lifecycle_dynamic"),
        collection: Some(String::from("echo")),
    };

    realm
        .destroy_child(&mut child_ref)
        .await
        .expect("destroy_child failed")
        .expect("failed to destroy child");
}
// [END destroy_child]
