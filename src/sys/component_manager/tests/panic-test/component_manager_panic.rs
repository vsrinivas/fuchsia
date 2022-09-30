// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::events::*, component_events::matcher::*,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_io as fio, fuchsia_async as fasync, fuchsia_component::client,
};

#[fasync::run_singlethreaded(test)]
async fn test() {
    // Start the component manager component.
    let realm_svc = client::connect_to_protocol::<fcomponent::RealmMarker>()
        .expect("Could not connect to Realm service");
    let mut child = fdecl::ChildRef { name: "component_manager".to_string(), collection: None };

    // Create endpoints for the fuchsia.io.Directory protocol.
    // Component manager will connect us to the exposed directory of the component we bound to.
    // This isn't needed for this test, but we must do it anyway.
    let (exposed_dir, exposed_dir_server) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    realm_svc
        .open_exposed_dir(&mut child, exposed_dir_server)
        .await
        .expect("Could not send open_exposed_dir command")
        .expect("open_exposed_dir command did not succeed");

    let _ = client::connect_to_protocol_at_dir_root::<fcomponent::BinderMarker>(&exposed_dir)
        .expect("failed to connect to fuchsia.component.Binder");

    // Wait for the component manager to stop because of a panic
    let mut stream = EventStream::open().await.unwrap();
    let mut matcher = EventMatcher::ok().stop(Some(ExitStatusMatcher::Crash(11)));
    matcher.expect_match::<Stopped>(&mut stream).await;
}
