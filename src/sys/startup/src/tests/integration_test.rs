// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    test_utils_lib::{
        events::{CapabilityRouted, Event, Handler},
        matcher::EventMatcher,
        opaque_test::{OpaqueTest, OpaqueTestBuilder},
    },
};

#[fasync::run_singlethreaded(test)]
async fn integration_test() {
    let mut builder =
        OpaqueTestBuilder::new("fuchsia-pkg://fuchsia.com/startup-integration-test#meta/root.cm");
    builder = builder
        .component_manager_url(
            "fuchsia-pkg://fuchsia.com/startup-integration-test#meta/component_manager.cmx",
        )
        .config("/pkg/data/component_manager_config");
    let test: OpaqueTest = builder.build().await.unwrap();
    let event_source = test.connect_to_event_source().await.unwrap();

    let mut event_stream = event_source.subscribe(vec![CapabilityRouted::NAME]).await.unwrap();
    event_source.start_component_tree().await;

    let event = EventMatcher::ok()
        .moniker("./startup")
        .capability_id("fuchsia.appmgr.Startup")
        .expect_match::<CapabilityRouted>(&mut event_stream)
        .await;
    event.resume().await.unwrap();
}
