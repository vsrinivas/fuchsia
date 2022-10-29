// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component_test::{ChildOptions, RealmBuilder},
};

#[fasync::run_singlethreaded(test)]
async fn echo_with_args() {
    run_single_test("#meta/reporter_args.cm").await
}

#[fasync::run_singlethreaded(test)]
async fn echo_without_args() {
    run_single_test("#meta/reporter_no_args.cm").await
}

async fn run_single_test(url: &str) {
    let builder = RealmBuilder::new().await.unwrap();
    builder.add_child("reporter", url, ChildOptions::new().eager()).await.unwrap();
    let instance =
        builder.build_in_nested_component_manager("#meta/component_manager.cm").await.unwrap();
    let proxy =
        instance.root.connect_to_protocol_at_exposed_dir::<fsys::EventStream2Marker>().unwrap();
    proxy.wait_for_ready().await.unwrap();
    let mut event_stream = EventStream::new_v2(proxy);

    instance.start_component_tree().await.unwrap();

    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker("./reporter")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}
