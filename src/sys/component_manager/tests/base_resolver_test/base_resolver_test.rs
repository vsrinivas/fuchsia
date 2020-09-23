// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fidl_examples_echo as fidl_echo, fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_component::client::*,
    test_utils_lib::{events::*, opaque_test::*},
};

#[fasync::run_singlethreaded(test)]
async fn base_resolver_test() {
    // Obtain access to this component's pkg directory
    let pkg_proxy = io_util::open_directory_in_namespace(
        "/pkg",
        fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
    )
    .unwrap();
    let pkg_channel = pkg_proxy.into_channel().unwrap().into_zx_channel();

    // A custom OpaqueTest is required because
    // 1. the /pkg dir of this component has to be passed in to component manager as /boot
    // 2. the component manager needs a manifest without fuchsia.sys.Loader
    let test = OpaqueTestBuilder::new("fuchsia-boot:///#meta/root.cm")
        .component_manager_url("fuchsia-pkg://fuchsia.com/base_resolver_test#meta/component_manager_without_loader.cmx")
        .add_dir_handle("/boot", pkg_channel.into())
        .build().await.unwrap();

    let event_source = &test.connect_to_event_source().await.unwrap();

    // Subscribe to events and begin execution of component manager
    let mut event_stream = event_source.subscribe(vec![Started::NAME]).await.unwrap();

    // Begin component manager's execution
    event_source.start_component_tree().await;

    // Expect the root component to be bound to
    let event = event_stream.expect_match::<Started>(EventMatcher::ok().expect_moniker(".")).await;
    event.resume().await.unwrap();

    // Expect the echo_server component to be bound to
    let event = event_stream
        .expect_match::<Started>(EventMatcher::ok().expect_moniker("./echo_server:0"))
        .await;
    event.resume().await.unwrap();

    // Connect to the echo service
    let path_to_service_dir =
        test.get_component_manager_path().join("out/hub/children/echo_server/exec/out/svc");
    let path_to_service_dir = path_to_service_dir.to_str().expect("unexpected chars");
    let echo_proxy = connect_to_service_at::<fidl_echo::EchoMarker>(path_to_service_dir).unwrap();

    // Test the echo service
    assert_eq!(Some("hippos!".to_string()), echo_proxy.echo_string(Some("hippos!")).await.unwrap());
}
