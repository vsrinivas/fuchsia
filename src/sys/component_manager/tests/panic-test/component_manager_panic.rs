// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::events::*, component_events::matcher::*,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_async as fasync, fuchsia_component::client,
};

#[fasync::run_singlethreaded(test)]
async fn test() {
    // Bind to the component manager component, causing it to start
    let realm_svc = client::connect_to_protocol::<fsys::RealmMarker>()
        .expect("Could not connect to Realm service");
    let mut child = fsys::ChildRef { name: "component_manager".to_string(), collection: None };

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
    let source = EventSource::new().expect("Could not connect to fuchsia.sys2.EventSource");
    let mut stream = source
        .take_static_event_stream("panic_test_event_stream")
        .await
        .expect("Could not take static event stream");
    let mut matcher = EventMatcher::ok().stop(Some(ExitStatusMatcher::Crash(11)));
    matcher.expect_match::<Stopped>(&mut stream).await;
}
