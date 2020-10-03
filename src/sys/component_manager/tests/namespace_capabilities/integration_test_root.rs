// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints, fidl_fidl_test_components as ftest, fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_component::client,
    fuchsia_syslog as syslog, log::*,
};

// `echo_client` is a child of this component and uses the `Echo` protocol from component
// manager's namespace.
#[fasync::run_singlethreaded]
async fn main() {
    syslog::init_with_tags(&[]).expect("could not initialize logging");
    info!("Started");
    let realm = client::connect_to_service::<fsys::RealmMarker>()
        .expect("could not connect to Realm service");

    // Bind to `trigger`, causing it to execute.
    let mut child_ref = fsys::ChildRef { name: "trigger".to_string(), collection: None };
    let (exposed_dir, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
    realm
        .bind_child(&mut child_ref, server_end)
        .await
        .expect(&format!("bind_child failed"))
        .expect(&format!("failed to bind to child"));
    let trigger = client::connect_to_protocol_at_dir_root::<ftest::TriggerMarker>(&exposed_dir)
        .await
        .expect("failed to open trigger service");
    info!("Invoking trigger");
    let out = trigger.run().await.expect(&format!("trigger failed"));
    assert_eq!(out, "Triggered");
    info!("Done");
}
