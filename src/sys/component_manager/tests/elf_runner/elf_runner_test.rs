// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fuchsia_async as fasync,
    test_utils_lib::opaque_test::*,
};

#[fasync::run_singlethreaded(test)]
async fn echo_with_args() {
    run_single_test("fuchsia-pkg://fuchsia.com/elf-runner-test#meta/reporter_args.cm").await
}

#[fasync::run_singlethreaded(test)]
async fn echo_without_args() {
    run_single_test("fuchsia-pkg://fuchsia.com/elf-runner-test#meta/reporter_no_args.cm").await
}

async fn run_single_test(url: &str) {
    let test = OpaqueTest::default(url).await.unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();

    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME], EventMode::Async)])
        .await
        .unwrap();

    event_source.start_component_tree().await;

    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}
