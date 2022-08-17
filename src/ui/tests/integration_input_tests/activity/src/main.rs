// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_utils::hanging_get::client::HangingGetStream,
    fidl_fuchsia_input_interaction::{NotifierMarker, NotifierProxy, State},
    fidl_fuchsia_logger::LogSinkMarker,
    fidl_fuchsia_scheduler::ProfileProviderMarker,
    fidl_fuchsia_sysmem::AllocatorMarker,
    fidl_fuchsia_tracing_provider::RegistryMarker,
    fidl_fuchsia_vulkan_loader::LoaderMarker,
    fuchsia_async::{Time, Timer},
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route},
    fuchsia_zircon::Duration,
    futures::future,
    futures::StreamExt,
    test_case::test_case,
};

const TEST_UI_STACK: &str = "ui";

// Set a maximum bound test timeout.
const TEST_TIMEOUT: Duration = Duration::from_minutes(2);

async fn assemble_realm(test_ui_stack_url: &str) -> RealmInstance {
    let builder = RealmBuilder::new().await.expect("Failed to create RealmBuilder.");

    // Add test UI stack component.
    builder
        .add_child(TEST_UI_STACK, test_ui_stack_url, ChildOptions::new())
        .await
        .expect("Failed to add UI realm.");

    // Route capabilities to the test UI realm.
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<LogSinkMarker>())
                .capability(Capability::protocol::<ProfileProviderMarker>())
                .capability(Capability::protocol::<AllocatorMarker>())
                .capability(Capability::protocol::<LoaderMarker>())
                .capability(Capability::protocol::<RegistryMarker>())
                .from(Ref::parent())
                .to(Ref::child(TEST_UI_STACK)),
        )
        .await
        .expect("Failed to route capabilities.");

    // Route capabilities from the test UI realm.
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<NotifierMarker>())
                .from(Ref::child(TEST_UI_STACK))
                .to(Ref::parent()),
        )
        .await
        .expect("Failed to route capabilities.");

    // Create the test realm.
    builder.build().await.expect("Failed to create test realm.")
}

#[test_case(
  "fuchsia-pkg://fuchsia.com/gfx-root-presenter-test-ui-stack#meta/test-ui-stack.cm"; "Standalone Input Pipeline variant"
)]
#[test_case(
  "fuchsia-pkg://fuchsia.com/gfx-scene-manager-test-ui-stack#meta/test-ui-stack.cm"; "GFX Scene Manager variant"
)]
#[fuchsia::test]
async fn enters_idle_state_without_activity(test_ui_stack_url: &str) {
    let realm = assemble_realm(test_ui_stack_url).await;

    // Subscribe to activity state, which serves "Active" initially.
    let notifier_proxy = realm
        .root
        .connect_to_protocol_at_exposed_dir::<NotifierMarker>()
        .expect("Failed to connect to fuchsia.input.interaction.Notifier.");
    let mut watch_state_stream = HangingGetStream::new(notifier_proxy, NotifierProxy::watch_state);
    assert_eq!(
        watch_state_stream
            .next()
            .await
            .expect("Expected initial state from watch_state()")
            .unwrap(),
        State::Active
    );

    // Do nothing. Activity service transitions to idle state in one minute.
    let activity_timeout_upper_bound = Timer::new(Time::after(TEST_TIMEOUT));
    match future::select(watch_state_stream.next(), activity_timeout_upper_bound).await {
        future::Either::Left((result, _)) => {
            assert_eq!(result.unwrap().expect("Expected state transition."), State::Idle);
        }
        future::Either::Right(_) => panic!("Timer expired before state transitioned."),
    }

    // Shut down input pipeline before dropping mocks, so that input pipeline
    // doesn't log errors about channels being closed.
    realm.destroy().await.expect("Failed to shut down realm.");
}
