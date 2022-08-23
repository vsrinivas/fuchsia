// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_utils::hanging_get::client::HangingGetStream,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_input_interaction::{NotifierMarker, NotifierProxy, State},
    fidl_fuchsia_input_report::MouseInputReport,
    fidl_fuchsia_logger::LogSinkMarker,
    fidl_fuchsia_math::Vec_,
    fidl_fuchsia_scheduler::ProfileProviderMarker,
    fidl_fuchsia_sysmem::AllocatorMarker,
    fidl_fuchsia_tracing_provider::RegistryMarker,
    fidl_fuchsia_ui_test_input::{
        RegistryMarker as InputRegistryMarker, RegistryRegisterTouchScreenRequest,
        TouchScreenMarker, TouchScreenSimulateTapRequest,
    },
    fidl_fuchsia_vulkan_loader::LoaderMarker,
    fidl_test_inputsynthesis::{MouseMarker, TextMarker},
    fuchsia_async::{Time, Timer},
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route},
    fuchsia_zircon::Duration,
    futures::future,
    futures::StreamExt,
    test_case::test_case,
};

const TEST_UI_STACK: &str = "ui";
const ROOT_PRESENTER_UI_STACK_URL: &str =
    "fuchsia-pkg://fuchsia.com/gfx-root-presenter-test-ui-stack#meta/test-ui-stack.cm";
const GFX_SCENE_MANAGER_UI_STACK_URL: &str =
    "fuchsia-pkg://fuchsia.com/gfx-scene-manager-test-ui-stack#meta/test-ui-stack.cm";
const FLATLAND_UI_STACK_URL: &str =
    "fuchsia-pkg://fuchsia.com/flatland-scene-manager-test-ui-stack#meta/test-ui-stack.cm";

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
                .capability(Capability::protocol::<MouseMarker>())
                .capability(Capability::protocol::<TextMarker>())
                .capability(Capability::protocol::<InputRegistryMarker>())
                .from(Ref::child(TEST_UI_STACK))
                .to(Ref::parent()),
        )
        .await
        .expect("Failed to route capabilities.");

    // Create the test realm.
    builder.build().await.expect("Failed to create test realm.")
}

#[test_case(ROOT_PRESENTER_UI_STACK_URL; "Standalone Input Pipeline variant")]
#[test_case(GFX_SCENE_MANAGER_UI_STACK_URL; "GFX Scene Manager variant")]
#[test_case(FLATLAND_UI_STACK_URL; "Flatland Scene Manager variant")]
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

// Note that the root presenter variant is omitted here because keyboards
// are unsupported in that mode.
#[test_case(GFX_SCENE_MANAGER_UI_STACK_URL; "GFX Scene Manager variant")]
#[test_case(FLATLAND_UI_STACK_URL; "Flatland Scene Manager variant")]
#[fuchsia::test]
async fn enters_active_state_with_keyboard(test_ui_stack_url: &str) {
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

    // Inject keyboard input.
    let input_synthesis = realm
        .root
        .connect_to_protocol_at_exposed_dir::<TextMarker>()
        .expect("Failed to connect to test.inputsynthesis.Text.");
    let _ = input_synthesis.send_("a").await.expect("Failed to send text.");

    // Activity service transitions to active state.
    assert_eq!(
        watch_state_stream
            .next()
            .await
            .expect("Expected updated state from watch_state()")
            .unwrap(),
        State::Active
    );

    // Shut down input pipeline before dropping mocks, so that input pipeline
    // doesn't log errors about channels being closed.
    realm.destroy().await.expect("Failed to shut down realm.");
}

#[fuchsia::test]
async fn enters_active_state_with_mouse() {
    // We hardcode the Flatland UI stack because mouse is unsupported in all
    // other variations.
    let realm = assemble_realm(FLATLAND_UI_STACK_URL).await;

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

    // Inject mouse input.
    let input_synthesis = realm
        .root
        .connect_to_protocol_at_exposed_dir::<MouseMarker>()
        .expect("Failed to connect to test.inputsynthesis.Mouse.");
    let mouse_id = input_synthesis.add_device().await.expect("Failed to add mouse device.");
    let _ = input_synthesis
        .send_input_report(
            mouse_id,
            MouseInputReport {
                movement_x: Some(10),
                movement_y: Some(15),
                ..MouseInputReport::EMPTY
            },
            Time::now().into_nanos().try_into().unwrap(),
        )
        .await
        .expect("Failed to send mouse report.");

    // Activity service transitions to active state.
    assert_eq!(
        watch_state_stream
            .next()
            .await
            .expect("Expected updated state from watch_state()")
            .unwrap(),
        State::Active
    );

    // Shut down input pipeline before dropping mocks, so that input pipeline
    // doesn't log errors about channels being closed.
    realm.destroy().await.expect("Failed to shut down realm.");
}

#[test_case(ROOT_PRESENTER_UI_STACK_URL; "Standalone Input Pipeline variant")]
#[test_case(GFX_SCENE_MANAGER_UI_STACK_URL; "GFX Scene Manager variant")]
#[test_case(FLATLAND_UI_STACK_URL; "Flatland Scene Manager variant")]
#[fuchsia::test]
async fn enters_active_state_with_touchscreen(test_ui_stack_url: &str) {
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

    // Inject touch input.
    let (touchscreen_proxy, touchscreen_server) =
        create_proxy::<TouchScreenMarker>().expect("Failed to create TouchScreenProxy.");
    let input_registry = realm
        .root
        .connect_to_protocol_at_exposed_dir::<InputRegistryMarker>()
        .expect("Failed to connect to fuchsia.ui.test.input.Registry.");
    input_registry
        .register_touch_screen(RegistryRegisterTouchScreenRequest {
            device: Some(touchscreen_server),
            ..RegistryRegisterTouchScreenRequest::EMPTY
        })
        .await
        .expect("Failed to register touchscreen device.");
    touchscreen_proxy
        .simulate_tap(TouchScreenSimulateTapRequest {
            tap_location: Some(Vec_ { x: 0, y: 0 }),
            ..TouchScreenSimulateTapRequest::EMPTY
        })
        .await
        .expect("Failed to simulate tap at location (0, 0).");

    // Activity service transitions to active state.
    assert_eq!(
        watch_state_stream
            .next()
            .await
            .expect("Expected updated state from watch_state()")
            .unwrap(),
        State::Active
    );

    // Shut down input pipeline before dropping mocks, so that input pipeline
    // doesn't log errors about channels being closed.
    realm.destroy().await.expect("Failed to shut down realm.");
}
