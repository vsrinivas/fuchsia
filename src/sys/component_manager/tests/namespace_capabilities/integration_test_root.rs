// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints, fidl_fidl_test_components as ftest, fidl_fuchsia_component as fcomponent,
    fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_io::DirectoryMarker,
    fuchsia_component::client, tracing::*,
};

// `echo_client` is a child of this component and uses the `Echo` protocol from component
// manager's namespace.
#[fuchsia::component]
async fn main() {
    info!("Started");
    let realm = client::connect_to_protocol::<fcomponent::RealmMarker>()
        .expect("could not connect to Realm service");

    // Bind to `trigger`, causing it to execute.
    let mut child_ref = fdecl::ChildRef { name: "trigger".to_string(), collection: None };
    let (exposed_dir, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
    realm
        .open_exposed_dir(&mut child_ref, server_end)
        .await
        .expect(&format!("open_exposed_dir failed"))
        .expect(&format!("failed to open exposed dir of child"));
    let trigger = client::connect_to_protocol_at_dir_root::<ftest::TriggerMarker>(&exposed_dir)
        .expect("failed to open trigger service");
    info!("Invoking trigger");
    let out = trigger.run().await.expect(&format!("trigger failed"));
    assert_eq!(out, "Triggered");
    info!("Done");
}
