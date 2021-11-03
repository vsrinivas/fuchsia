// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{
        events::{CapabilityRouted, Event, EventMode, EventSubscription, Handler},
        matcher::EventMatcher,
    },
    fuchsia_async as fasync,
    test_utils_lib::opaque_test::{OpaqueTest, OpaqueTestBuilder},
};

#[fasync::run_singlethreaded(test)]
async fn integration_test() {
    let mut builder =
        OpaqueTestBuilder::new("fuchsia-pkg://fuchsia.com/startup-integration-test#meta/root.cm");
    builder = builder
        .component_manager_url(
            "fuchsia-pkg://fuchsia.com/startup-integration-test#meta/component_manager.cmx",
        )
        .config("/pkg/data/component_manager_debug_config");
    let test: OpaqueTest = builder.build().await.unwrap();
    let event_source = test.connect_to_event_source().await.unwrap();

    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![CapabilityRouted::NAME], EventMode::Sync)])
        .await
        .unwrap();
    event_source.start_component_tree().await;

    let event = EventMatcher::ok()
        .moniker_regex("./startup")
        .capability_name("fuchsia.appmgr.Startup")
        .expect_match::<CapabilityRouted>(&mut event_stream)
        .await;
    event.resume().await.unwrap();
}
