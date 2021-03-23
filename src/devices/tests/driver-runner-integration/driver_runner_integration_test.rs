// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{
        events::{self, Event},
        matcher::EventMatcher,
        sequence::{self, EventSequence},
    },
    fidl::endpoints::Proxy,
    fidl_fuchsia_io as fio, fuchsia_async as fasync,
    test_utils_lib::opaque_test::OpaqueTestBuilder,
};

#[fasync::run_singlethreaded(test)]
async fn test() {
    fuchsia_syslog::init().unwrap();

    // Obtain access to this component's pkg directory
    let pkg_proxy = io_util::open_directory_in_namespace(
        "/pkg",
        fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
    )
    .unwrap();
    let pkg_channel = pkg_proxy.into_channel().unwrap().into_zx_channel();

    // Create an OpaqueTest that uses the appmgr bridge config
    let test_env = OpaqueTestBuilder::new("fuchsia-pkg://fuchsia.com/driver-runner-integration-test#meta/driver-runner-integration-root.cm")
        .add_dir_handle("/boot", pkg_channel.into())
        .build()
        .await
        .unwrap();

    let mut event_source = test_env
        .connect_to_event_source()
        .await
        .expect("could not connect to event source for opaque test");

    // List the components that we expect to be created.
    // We list the components by monikers which are described at:
    // https://fuchsia.dev/fuchsia-src/concepts/components/v2/monikers?hl=en
    // Drivers live in a collection under driver_manager, and their monikers will look like:
    //   /driver_manager:0/drivers:driver-{DRIVER_NUMBER}:{INSTANCE_NUMBER}
    // Driver hosts live in a collection under driver_manager, and their monikers will look like:
    //   /driver_manager:0/driver_hosts:driver_host-{DRIVER_NUMBER}:{INSTANCE_NUMBER}
    // We don't know how consistent the INSTANCE_NUMBER is so we regex match it with '\d+'.
    let expected = EventSequence::new()
        .all_of(
            vec![
                EventMatcher::ok().r#type(events::Started::TYPE).moniker(r"/driver_manager:\d+"),
                EventMatcher::ok()
                    .r#type(events::Started::TYPE)
                    .moniker(r"/driver_manager:\d+/drivers:driver-0:\d+"),
                EventMatcher::ok()
                    .r#type(events::Started::TYPE)
                    .moniker(r"/driver_manager:\d+/driver_hosts:driver_host-1:\d+"),
            ],
            sequence::Ordering::Ordered,
        )
        .subscribe_and_expect(&mut event_source)
        .await
        .unwrap();

    let (_, res) = futures::join!(event_source.start_component_tree(), expected);
    res.unwrap();
}
