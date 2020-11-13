// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::display_provider::{DisplayControllerProviderInjector, DisplayState},
    log::info,
    std::sync::Arc,
    test_utils_lib::{
        events::EventSource,
        injectors::{CapabilityInjector, TestNamespaceInjector},
        matcher::EventMatcher,
        opaque_test::OpaqueTest,
    },
};

const ROOT_URL: &str = "fuchsia-pkg://fuchsia.com/scenic-stress-tests#meta/root.cm";

/// Injects a capability at |path| by connecting to a test namespace capability
/// at the same |path|.
pub async fn inject_from_test_namespace(
    name: &'static str,
    path: &'static str,
    event_source: &EventSource,
) {
    TestNamespaceInjector::new(path)
        .inject(event_source, EventMatcher::ok().capability_name(name))
        .await;
}

/// Starts Scenic in an isolated instance of component manager.
/// Injects required dependencies from the system or from the test, as needed.
/// Waits for the display to update at least |min_updates| times before returning.
pub async fn init_scenic(min_updates: u64) -> (OpaqueTest, DisplayState) {
    let test = OpaqueTest::default(ROOT_URL).await.unwrap();
    let event_source = test.connect_to_event_source().await.unwrap();
    let display_state = DisplayState::new();

    // This directory is needed to start up Scenic's GFX subsystem.
    inject_from_test_namespace(
        "dev-display-controller",
        "/dev/class/display-controller",
        &event_source,
    )
    .await;

    // These directories provide access to the AEMU GPU, needed by Vulkan.
    inject_from_test_namespace(
        "dev-goldfish-address-space",
        "/dev/class/goldfish-address-space",
        &event_source,
    )
    .await;
    inject_from_test_namespace(
        "dev-goldfish-control",
        "/dev/class/goldfish-control",
        &event_source,
    )
    .await;
    inject_from_test_namespace("dev-goldfish-pipe", "/dev/class/goldfish-pipe", &event_source)
        .await;

    // Vulkan needs this directory for loading dynamic libraries
    inject_from_test_namespace("config-vulkan-icd.d", "/config/vulkan/icd.d", &event_source).await;

    // This is the protocol that is used to load Vulkan
    inject_from_test_namespace(
        "fuchsia.vulkan.loader.Loader",
        "/svc/fuchsia.vulkan.loader.Loader",
        &event_source,
    )
    .await;

    // Used by Vulkan
    inject_from_test_namespace(
        "fuchsia.sysmem.Allocator",
        "/svc/fuchsia.sysmem.Allocator",
        &event_source,
    )
    .await;

    // Scenic looks for a display using this protocol
    Arc::new(DisplayControllerProviderInjector::new(display_state.clone()))
        .inject(&event_source, EventMatcher::ok())
        .await;

    // Start all the components
    event_source.start_component_tree().await;

    info!("Waiting for {} display updates...", min_updates);

    // Wait for the display to update the minimum number of times
    while display_state.get_num_updates() < min_updates {}

    (test, display_state)
}
