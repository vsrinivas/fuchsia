// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::EventMatcher},
    fuchsia_async as fasync,
    test_utils_lib::opaque_test::*,
};

#[fasync::run_singlethreaded(test)]
/// This uses the root_component.rs implementation to make the test package's
/// contents appear at /pkgfs. This allows component manager's built-in base
/// package resolver to see the contents of the package. HOWEVER, the component
/// manager configuration here sets the built-in resolver to 'None', meaning we
/// expect the attempt to start `echo_server` to not resolve.
async fn base_resolver_appmgr_bridge_test() {
    // Create an OpaqueTest that uses the appmgr bridge config
    let test = OpaqueTestBuilder::new("fuchsia-boot:///#meta/simple_root.cm")
        .component_manager_url(
            "fuchsia-pkg://fuchsia.com/base_resolver_test#meta/component_manager_appmgr_loader.cmx",
        )
        .config("/pkg/data/component_manager_config_appmgr_loader")
        .build()
        .await
        .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();

    // Subscribe to events and begin execution of component manager
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Started::NAME], EventMode::Async)])
        .await
        .unwrap();

    // Begin component manager's execution
    event_source.start_component_tree().await;

    // Expect the root component to be bound to
    EventMatcher::ok().moniker_regex(".").expect_match::<Started>(&mut event_stream).await;

    // // Expect start to succeed because we're using the appmgr loader
    EventMatcher::ok()
        .moniker_regex("./echo_server")
        .expect_match::<Started>(&mut event_stream)
        .await;
}
