// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    test_utils_lib::{echo_capability::EchoCapability, opaque_test::*},
};

#[fasync::run_singlethreaded(test)]
async fn echo_with_args() {
    run_single_test(
        "fuchsia-pkg://fuchsia.com/elf-runner-test#meta/reporter_args.cm",
        "/pkg/bin/args_reporter Hippos rule!",
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn echo_without_args() {
    run_single_test(
        "fuchsia-pkg://fuchsia.com/elf-runner-test#meta/reporter_no_args.cm",
        "/pkg/bin/args_reporter",
    )
    .await
}

async fn run_single_test(url: &str, expected_output: &str) {
    let test = OpaqueTest::default(url).await.unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();
    let (capability, mut echo_rx) = EchoCapability::new();
    let injector = event_source.install_injector(capability, None).await.unwrap();
    event_source.start_component_tree().await;

    let event = echo_rx.next().await.unwrap();
    assert_eq!(expected_output, event.message);
    injector.abort();
}
