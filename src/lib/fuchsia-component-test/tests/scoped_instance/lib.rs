// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fuchsia_async as fasync, fuchsia_syslog as syslog,
    test_case::test_case,
    test_utils_lib::opaque_test::OpaqueTest,
};

#[test_case("fuchsia-pkg://fuchsia.com/fuchsia-component-test-tests#meta/realm_with_wait.cm"; "wait")]
#[test_case("fuchsia-pkg://fuchsia.com/fuchsia-component-test-tests#meta/realm.cm"; "no_wait")]
#[fasync::run_singlethreaded(test)]
async fn scoped_instances(root_component: &'static str) {
    syslog::init_with_tags(&["fuchsia_component_v2_test"]).expect("could not initialize logging");
    let test = OpaqueTest::default(root_component).await.unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();

    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME], EventMode::Async)])
        .await
        .unwrap();

    event_source.start_component_tree().await;

    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker_regex(".")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}
