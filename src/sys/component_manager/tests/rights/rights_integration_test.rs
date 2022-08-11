// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fidl_test_components as ftest, fuchsia_async as fasync, fuchsia_component_test::*};

const COMPONENT_MANAGER_URL: &str = "#meta/component_manager_for_rights_test.cm";

async fn run_test(url: &str, expected_result: &str) {
    // Define the realm inside component manager.
    let builder = RealmBuilder::new().await.unwrap();
    let realm = builder.add_child("realm", url, ChildOptions::new().eager()).await.unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&realm),
        )
        .await
        .unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<ftest::TriggerMarker>())
                .from(&realm)
                .to(Ref::parent()),
        )
        .await
        .unwrap();

    let (cm_builder, _task) =
        builder.with_nested_component_manager(COMPONENT_MANAGER_URL).await.unwrap();

    cm_builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<ftest::TriggerMarker>())
                .from(Ref::child("component_manager"))
                .to(Ref::parent()),
        )
        .await
        .unwrap();

    let instance = cm_builder.build().await.unwrap();

    let trigger = instance
        .root
        .connect_to_protocol_at_exposed_dir::<ftest::TriggerMarker>()
        .expect("failed to connect to Trigger");

    let result = trigger.run().await.expect("trigger failed");
    assert_eq!(result, expected_result, "Results did not match");
}

// Verifies that the component_manager supports routing capabilities with different rights and that
// offer right filtering and right inference are correctly working. The use statement will attempt
// to access permissions it isn't allowed and verify they return ACCESS_DENIED.
#[fasync::run_singlethreaded(test)]
async fn offer_dir_rights() {
    run_test("#meta/root_offer_dir_rights.cm", "All tests passed").await
}

// Verifies that an invalidly configured use in a component will result in a failure on attempt to
// access that directory. Over accessing permissions will prevent that directory being routed to
// the component.
#[fasync::run_singlethreaded(test)]
async fn invalid_use_in_offer_dir_rights_prevented() {
    run_test(
        "#meta/root_invalid_use_in_offer_dir_rights.cm",
        "Directory rights test failed: /read_only - connection aborted",
    )
    .await
}

// Verifies that an invalid offer that offers more than is exposed to it is invalid and will result
// in the directory not being offered to the child process.
#[fasync::run_singlethreaded(test)]
async fn invalid_offer_dir_rights_prevented() {
    run_test(
        "#meta/root_invalid_offer_dir_rights.cm",
        "Directory rights test failed: /read_only - connection aborted",
    )
    .await
}

// Verifies that an invalid intermediate expose that attempts to increase its rights to a read only
// directory fails with that exposed direcotry not being mapped into the testing proccess.
#[fasync::run_singlethreaded(test)]
async fn invalid_intermediate_expose_prevented() {
    run_test(
        "#meta/root_invalid_expose_intermediate_offer_dir_rights.cm",
        "Directory rights test failed: /read_only - connection aborted",
    )
    .await
}

// Verifies that an intermediate expose that attempts to reduces the rights on a directory is able
// to have that propagate through to the rest of the system.
#[fasync::run_singlethreaded(test)]
async fn intermediate_expose_rights() {
    run_test("#meta/root_expose_intermediate_offer_dir_rights.cm", "All tests passed").await
}

// Verifies that an intermediate offer that attempts to reduces the rights on a directory is able
// to have that propagate down to children nodes.
#[fasync::run_singlethreaded(test)]
async fn intermediate_offer_rights() {
    run_test("#meta/root_offer_intermediate_offer_dir_rights.cm", "All tests passed").await
}

// Verifies that an intermediate offer that attempts to increase its rights to a directory results
// in that directory not being mapped into the child proccess.
#[fasync::run_singlethreaded(test)]
async fn invalid_intermediate_offer_prevented() {
    run_test(
        "#meta/root_invalid_offer_intermediate_offer_dir_rights.cm",
        "Directory rights test failed: /read_only - connection aborted",
    )
    .await
}

// Verifies that if the offer utilizes aliases instead of direct mapping rights scoping
// rules are still correctly enforced.
#[fasync::run_singlethreaded(test)]
async fn alias_offer_dir_rights() {
    run_test("#meta/root_alias_offer_dir_rights.cm", "All tests passed").await
}

// Verifies that component_manager supports directory capabilities with differing rights
// when the source of the capability is component_manager's namespace.
#[fasync::run_singlethreaded(test)]
async fn route_directories_from_component_manager_namespace() {
    // Define the realm inside component manager.
    let builder = RealmBuilder::new().await.unwrap();
    let realm = builder
        .add_child("realm", "#meta/use_dir_rights.cm", ChildOptions::new().eager())
        .await
        .unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&realm),
        )
        .await
        .unwrap();

    let dirs =
        vec!["read_only", "read_write", "read_exec", "read_write_dup", "read_only_after_scoped"];

    for dir in dirs {
        builder
            .add_route(
                Route::new().capability(Capability::directory(dir)).from(Ref::parent()).to(&realm),
            )
            .await
            .unwrap();
    }

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<ftest::TriggerMarker>())
                .from(&realm)
                .to(Ref::parent()),
        )
        .await
        .unwrap();

    let (cm_builder, _task) =
        builder.with_nested_component_manager(COMPONENT_MANAGER_URL).await.unwrap();

    cm_builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<ftest::TriggerMarker>())
                .from(Ref::child("component_manager"))
                .to(Ref::parent()),
        )
        .await
        .unwrap();

    let instance = cm_builder.build().await.unwrap();

    let trigger = instance
        .root
        .connect_to_protocol_at_exposed_dir::<ftest::TriggerMarker>()
        .expect("failed to connect to Trigger");

    let result = trigger.run().await.expect("trigger failed");
    assert_eq!(result, "All tests passed", "Results did not match");
}

// Verifies that if the storage capability offered is valid then you can write to the storage.
#[fasync::run_singlethreaded(test)]
async fn storage_offer_from_rw_dir() {
    run_test("#meta/root_storage_offer_rights.cm", "All tests passed").await
}

// Verifies you can't write to storage if its backing source capability is not writable.
#[ignore] // TODO(https://fxbug.dev/103991) re-enable after fixing racy logic in run_test
#[fasync::run_singlethreaded(test)]
async fn storage_offer_from_r_dir_fails() {
    run_test("#meta/root_invalid_storage_offer_rights.cm", "Failed to write to file").await
}
