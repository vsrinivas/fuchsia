// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    test_utils_lib::{
        events::{self, Event},
        matcher::EventMatcher,
        opaque_test::OpaqueTestBuilder,
        sequence::{self, EventSequence},
    },
};

#[fasync::run_singlethreaded(test)]
/// Verifies that when a component has a LogSink in its namespace that the
/// component manager tries to connect to this.
async fn check_logsink_requested() {
    let test_env =
        OpaqueTestBuilder::new("fuchsia-pkg://fuchsia.com/attributed-logging-test#meta/root.cm")
            .component_manager_url(
                "fuchsia-pkg://fuchsia.com/attributed-logging-test#meta/component-manager.cmx",
            )
            .config("/pkg/data/cm_config")
            .build()
            .await
            .expect("failed to construct OpaqueTest");

    let mut event_source = test_env
        .connect_to_event_source()
        .await
        .expect("could not connect to event source for opaque test");

    let expected = EventSequence::new()
        .all_of(
            vec![
                EventMatcher::ok()
                    .r#type(events::CapabilityRouted::TYPE)
                    .capability_name("fuchsia.logger.LogSink")
                    .moniker("/empty_child:0"),
                EventMatcher::ok().r#type(events::Stopped::TYPE).moniker("/empty_child:0"),
            ],
            sequence::Ordering::Ordered,
        )
        .subscribe_and_expect(&mut event_source)
        .await
        .unwrap();

    event_source.start_component_tree().await;
    expected.await.unwrap();
}
