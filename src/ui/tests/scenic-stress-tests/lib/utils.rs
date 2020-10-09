// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::display_provider::{DisplayControllerProviderInjector, DisplayState},
    std::sync::Arc,
    test_utils_lib::{
        events::EventSource,
        injectors::{CapabilityInjector, TestNamespaceInjector},
        matcher::EventMatcher,
        opaque_test::OpaqueTest,
    },
};

pub const ROOT_URL: &str = "fuchsia-pkg://fuchsia.com/scenic-stress-tests#meta/root.cm";

/// Injects a capability at |path| by connecting to a test namespace capability
/// at the same |path|.
pub async fn inject_from_test_namespace(path: &'static str, event_source: &EventSource) {
    TestNamespaceInjector::new(path)
        .inject(event_source, EventMatcher::ok().capability_id(path))
        .await;
}

/// Starts Scenic in an isolated instance of component manager.
/// Injects required dependencies from the system or from the test, as needed.
pub async fn init_scenic() -> (OpaqueTest, EventSource, DisplayState) {
    let test: OpaqueTest = OpaqueTest::default(ROOT_URL).await.unwrap();
    let event_source = test.connect_to_event_source().await.unwrap();
    let display_state = DisplayState::new();

    // This directory is needed to start up Scenic's GFX subsystem.
    inject_from_test_namespace("/dev/class/display-controller", &event_source).await;

    // These directories provide access to the AEMU GPU, needed by Vulkan.
    inject_from_test_namespace("/dev/class/goldfish-address-space", &event_source).await;
    inject_from_test_namespace("/dev/class/goldfish-control", &event_source).await;
    inject_from_test_namespace("/dev/class/goldfish-pipe", &event_source).await;

    // Vulkan needs this directory for loading dynamic libraries
    inject_from_test_namespace("/config/vulkan/icd.d", &event_source).await;

    // This is the protocol that is used to load Vulkan
    inject_from_test_namespace("/svc/fuchsia.vulkan.loader.Loader", &event_source).await;

    // Used by Vulkan
    inject_from_test_namespace("/svc/fuchsia.sysmem.Allocator", &event_source).await;

    // Scenic looks for a display using this protocol
    Arc::new(DisplayControllerProviderInjector::new(display_state.clone()))
        .inject(&event_source, EventMatcher::ok())
        .await;

    (test, event_source, display_state)
}
