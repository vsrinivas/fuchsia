// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{
        events::{EventStream, Resolved},
        matcher::EventMatcher,
    },
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_io as fio,
    fuchsia_component::client::{connect_to_protocol, connect_to_protocol_at_dir_root},
};

#[fuchsia::main(logging_tags = ["resolved_error_reporter"])]
async fn main() {
    // Track all the starting child components.
    let mut event_stream = EventStream::open().await.unwrap();

    // This will trigger the resolution of the child.
    let realm = connect_to_protocol::<fcomponent::RealmMarker>().unwrap();
    let mut child_ref = fdecl::ChildRef { name: "child_a".to_string(), collection: None };

    let (exposed_dir, server_end) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let _ = realm
        .open_exposed_dir(&mut child_ref, server_end)
        .await
        .expect("failed to open exposed dir");

    // Attempt to start the child component should fail.
    assert!(connect_to_protocol_at_dir_root::<fcomponent::BinderMarker>(&exposed_dir).is_err());

    let _resolved_event = EventMatcher::err().expect_match::<Resolved>(&mut event_stream).await;
}
