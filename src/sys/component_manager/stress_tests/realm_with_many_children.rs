// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cm_stress_tests_lib::{create_child, stop_child, Child},
    fidl_test_componentmanager_stresstests as fstresstests, fuchsia_async as fasync,
    futures::prelude::*,
    futures::stream,
};

const NUM_CHILDREN: u16 = 1000;

// TODO(fxbug.dev/58641): enable this stress test
#[fasync::run_singlethreaded(test)]
#[ignore]
/// This stress test will create a 1000 children, make sure they are running and then stop them.
async fn launch_and_stress_test() {
    let stream = stream::iter(0..NUM_CHILDREN);
    const URL: &str =
        "fuchsia-pkg://fuchsia.com/component-manager-stress-tests#meta/child-for-stress-test.cm";
    const COL: &str = "children";
    let children: Vec<Child> =
        stream.then(|_| async { create_child(COL, URL).await.unwrap() }).collect().await;

    stream::iter(children)
        .for_each_concurrent(None, |child| async move {
            match child.realm.take_event_stream().try_next().await.unwrap().unwrap() {
                fstresstests::ChildRealmEvent::OnConnected {} => {}
            }
            stop_child(child).await.unwrap();
        })
        .await;
}
