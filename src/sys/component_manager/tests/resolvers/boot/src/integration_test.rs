// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests the ability to route the built-in "boot" resolver from component manager's realm to
//! a new environment not-inherited from the built-in environment.
//!
//! This tested by starting a component manager instance, set to expose any protocol exposed to it
//! in its outgoing directory. A fake bootfs is offered to the component manager, which provides the
//! manifests and executables needed to run the test.
//!
//! The test then calls the expected test FIDL protocol in component manager's outgoing namespace.
//! If the call is successful, then the boot resolver was correctly routed.

use {
    fidl_fidl_test_components as ftest, fidl_fuchsia_io2 as fio2,
    fuchsia_component::server::ServiceFs, fuchsia_component_test::builder::*, futures::prelude::*,
};

#[fuchsia::test]
async fn boot_resolver_can_be_routed_from_component_manager() {
    let mut builder = RealmBuilder::new().await.unwrap();
    builder
        .add_component(
            "component-manager",
            ComponentSource::url(
                "fuchsia-pkg://fuchsia.com/boot-resolver-routing-tests#meta/component_manager.cm",
            ),
        )
        .await
        .unwrap();
    builder
        .add_component(
            "mock-boot",
            ComponentSource::mock(|mock_handles| {
                async move {
                    let pkg = io_util::directory::open_in_namespace(
                        "/pkg",
                        io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_EXECUTABLE,
                    )
                    .unwrap();
                    let mut fs = ServiceFs::new();
                    fs.add_remote("boot", pkg);
                    fs.serve_connection(mock_handles.outgoing_dir.into_channel()).unwrap();
                    fs.collect::<()>().await;
                    Ok::<(), anyhow::Error>(())
                }
                .boxed()
            }),
        )
        .await
        .unwrap();

    // Supply a fake boot directory which is really just an alias to this package's pkg directory.
    // TODO(fxbug.dev/37534): Add the execute bit when supported.
    builder
        .add_route(CapabilityRoute {
            capability: Capability::directory("boot", "/boot", fio2::R_STAR_DIR),
            source: RouteEndpoint::component("mock-boot"),
            targets: vec![RouteEndpoint::component("component-manager")],
        })
        .unwrap();

    // This is the test protocol that is expected to be callable.
    builder
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fidl.test.components.Trigger"),
            source: RouteEndpoint::component("component-manager"),
            targets: vec![RouteEndpoint::AboveRoot],
        })
        .unwrap();

    // Forward logging to debug test breakages.
    builder
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.LogSink"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component("component-manager")],
        })
        .unwrap();

    // Component manager needs fuchsia.process.Launcher to spawn new processes.
    builder
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.process.Launcher"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component("component-manager")],
        })
        .unwrap();

    let realm_instance = builder.build().create().await.unwrap();
    let trigger =
        realm_instance.root.connect_to_protocol_at_exposed_dir::<ftest::TriggerMarker>().unwrap();
    let out = trigger.run().await.expect("trigger failed");
    assert_eq!(out, "Triggered");
}
