// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![cfg(test)]

use {
    anyhow::{Context as _, Result},
    fidl::endpoints::{create_request_stream, ProtocolMarker as _},
    fidl_fuchsia_input as input, fidl_fuchsia_ui_input3 as ui_input3,
    fidl_fuchsia_ui_keyboard_focus as fidl_focus, fidl_fuchsia_ui_views as ui_views,
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs, ServiceObj},
    },
    fuchsia_scenic as scenic,
    fuchsia_syslog::*,
    fuchsia_zircon as zx,
    futures::FutureExt,
    futures::{
        future,
        stream::{FusedStream, StreamExt},
    },
    matches::assert_matches,
    test_case::test_case,
    test_helpers::create_key_event,
};

const URL_TEXT_MANAGER: &str = "fuchsia-pkg://fuchsia.com/keyboard_test#meta/text_manager.cmx";

/// Wrapper for a `NestedEnvironment` that exposes the protocols offered by `text_manager.cmx`.
/// Running each test method in its own environment allows the tests to be run in parallel.
struct TestEnvironment {
    _text_manager_app: App,
    _fs_task: fasync::Task<()>,
    env: NestedEnvironment,
}

impl TestEnvironment {
    /// Creates a new `NestedEnvironment` exposing services from `text_manager.cmx`, which is
    /// immediately launched.
    #[must_use]
    fn new() -> Result<Self> {
        let mut text_manager_builder = AppBuilder::new(URL_TEXT_MANAGER);
        let text_manager_dir =
            text_manager_builder.directory_request().context("directory_request")?.to_owned();

        let mut fs: ServiceFs<ServiceObj<'static, ()>> = ServiceFs::new();
        fs.add_proxy_service_to::<fidl_fuchsia_ui_input::ImeServiceMarker, _>(
            text_manager_dir.clone(),
        )
        .add_proxy_service_to::<ui_input3::KeyboardMarker, _>(text_manager_dir.clone())
        .add_proxy_service_to::<ui_input3::KeyEventInjectorMarker, _>(text_manager_dir.clone())
        .add_proxy_service_to::<fidl_focus::ControllerMarker, _>(text_manager_dir.clone());

        let env = fs
            .create_salted_nested_environment("test")
            .context("create_salted_nested_environment")?;

        let _text_manager_app =
            text_manager_builder.spawn(env.launcher()).context("Launching text_manager")?;
        let _fs_task = fasync::Task::spawn(fs.collect());

        Ok(TestEnvironment { _text_manager_app, _fs_task, env })
    }

    /// Connects to the given discoverable service in the `NestedEnvironment`, with a readable
    /// context on error.
    fn connect_to_env_service<P>(&self) -> Result<P>
    where
        P: fidl::endpoints::Proxy,
        P::Protocol: fidl::endpoints::DiscoverableProtocolMarker,
    {
        fx_log_debug!("Connecting to nested environment's {}", P::Protocol::DEBUG_NAME);
        self.env.connect_to_protocol::<P::Protocol>().with_context(|| {
            format!("Failed to connect to nested environment's {}", P::Protocol::DEBUG_NAME)
        })
    }

    fn connect_to_focus_controller(&self) -> Result<fidl_focus::ControllerProxy> {
        self.connect_to_env_service::<_>()
    }

    fn connect_to_keyboard_service(&self) -> Result<ui_input3::KeyboardProxy> {
        self.connect_to_env_service::<_>()
    }

    fn connect_to_key_event_injector(&self) -> Result<ui_input3::KeyEventInjectorProxy> {
        self.connect_to_env_service::<_>()
    }
}

fn create_key_down_event(key: input::Key, modifiers: ui_input3::Modifiers) -> ui_input3::KeyEvent {
    ui_input3::KeyEvent {
        timestamp: Some(0),
        key: Some(key),
        modifiers: Some(modifiers),
        type_: Some(ui_input3::KeyEventType::Pressed),
        ..ui_input3::KeyEvent::EMPTY
    }
}

fn create_key_up_event(key: input::Key, modifiers: ui_input3::Modifiers) -> ui_input3::KeyEvent {
    ui_input3::KeyEvent {
        timestamp: Some(0),
        key: Some(key),
        modifiers: Some(modifiers),
        type_: Some(ui_input3::KeyEventType::Released),
        ..ui_input3::KeyEvent::EMPTY
    }
}

async fn expect_key_event(
    listener: &mut ui_input3::KeyboardListenerRequestStream,
) -> ui_input3::KeyEvent {
    let listener_request = listener.next().await;
    if let Some(Ok(ui_input3::KeyboardListenerRequest::OnKeyEvent { event, responder, .. })) =
        listener_request
    {
        responder.send(ui_input3::KeyEventStatus::Handled).expect("responding from key listener");
        event
    } else {
        panic!("Expected key event, got {:?}", listener_request);
    }
}

async fn dispatch_and_expect_key_event<'a>(
    key_dispatcher: &'a test_helpers::KeySimulator<'a>,
    listener: &mut ui_input3::KeyboardListenerRequestStream,
    event: ui_input3::KeyEvent,
) -> Result<()> {
    let (was_handled, event_result) =
        future::join(key_dispatcher.dispatch(event.clone()), expect_key_event(listener)).await;

    assert_eq!(was_handled?, true);
    assert_eq!(event_result.key, event.key);
    assert_eq!(event_result.type_, event.type_);
    Ok(())
}

async fn expect_key_and_modifiers(
    listener: &mut ui_input3::KeyboardListenerRequestStream,
    key: input::Key,
    modifiers: ui_input3::Modifiers,
) {
    let event = expect_key_event(listener).await;
    assert_eq!(event.key, Some(key));
    assert_eq!(event.modifiers, Some(modifiers));
}

async fn test_disconnecting_keyboard_client_disconnects_listener_with_connections(
    focus_ctl: fidl_focus::ControllerProxy,
    key_simulator: &'_ test_helpers::KeySimulator<'_>,
    keyboard_service_client: ui_input3::KeyboardProxy,
    keyboard_service_other_client: &ui_input3::KeyboardProxy,
) -> Result<()> {
    fx_log_debug!("test_disconnecting_keyboard_client_disconnects_listener_with_connections");

    // Create fake client.
    let (listener_client_end, mut listener) =
        fidl::endpoints::create_request_stream::<ui_input3::KeyboardListenerMarker>()?;
    let view_ref = scenic::ViewRefPair::new()?.view_ref;

    keyboard_service_client
        .add_listener(&mut scenic::duplicate_view_ref(&view_ref)?, listener_client_end)
        .await
        .expect("add_listener for first client");

    // Create another fake client.
    let (other_listener_client_end, mut other_listener) =
        fidl::endpoints::create_request_stream::<ui_input3::KeyboardListenerMarker>()?;
    let other_view_ref = scenic::ViewRefPair::new()?.view_ref;

    keyboard_service_other_client
        .add_listener(&mut scenic::duplicate_view_ref(&other_view_ref)?, other_listener_client_end)
        .await
        .expect("add_listener for another client");

    // Focus second client.
    focus_ctl.notify(&mut scenic::duplicate_view_ref(&other_view_ref)?).await?;

    // Drop proxy, emulating first client disconnecting from it.
    std::mem::drop(keyboard_service_client);

    // Expect disconnected client key event listener to be disconnected as well.
    assert_matches!(listener.next().await, None);
    assert_matches!(listener.is_terminated(), true);

    // Ensure that the other client is still connected.
    let (key, modifiers) = (input::Key::A, ui_input3::Modifiers::CapsLock);
    let dispatched_event = create_key_down_event(key, modifiers);

    let (was_handled, _) = future::join(
        key_simulator.dispatch(dispatched_event),
        expect_key_and_modifiers(&mut other_listener, key, modifiers),
    )
    .await;

    assert_eq!(was_handled?, true);

    let dispatched_event = create_key_up_event(key, modifiers);
    let (was_handled, _) = future::join(
        key_simulator.dispatch(dispatched_event),
        expect_key_and_modifiers(&mut other_listener, key, modifiers),
    )
    .await;

    assert_eq!(was_handled?, true);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
#[ignore] // TODO(fxbug.dev/85005) debug and re-enable
async fn test_disconnecting_keyboard_client_disconnects_listener_via_key_event_injector(
) -> Result<()> {
    fuchsia_syslog::init_with_tags(&["keyboard3_integration_test"])
        .expect("syslog init should not fail");

    let test_env = TestEnvironment::new()?;

    let key_event_injector = test_env.connect_to_key_event_injector()?;

    let key_dispatcher =
        test_helpers::KeyEventInjectorDispatcher { key_event_injector: &key_event_injector };
    let key_simulator = test_helpers::KeySimulator::new(&key_dispatcher);

    let keyboard_service_client_a = test_env.connect_to_keyboard_service().context("client_a")?;

    let keyboard_service_client_b = test_env.connect_to_keyboard_service().context("client_b")?;

    test_disconnecting_keyboard_client_disconnects_listener_with_connections(
        test_env.connect_to_focus_controller()?,
        &key_simulator,
        // This one will be dropped as part of the test, so needs to be moved.
        keyboard_service_client_a,
        &keyboard_service_client_b,
    )
    .await?;

    Ok(())
}

async fn test_sync_cancel_with_connections(
    focus_ctl: fidl_focus::ControllerProxy,
    key_simulator: &'_ test_helpers::KeySimulator<'_>,
    keyboard_service_client_a: &ui_input3::KeyboardProxy,
    keyboard_service_client_b: &ui_input3::KeyboardProxy,
) -> Result<()> {
    // Create fake client.
    let (listener_client_end_a, mut listener_a) =
        fidl::endpoints::create_request_stream::<ui_input3::KeyboardListenerMarker>()?;
    let view_ref_a = scenic::ViewRefPair::new()?.view_ref;

    keyboard_service_client_a
        .add_listener(&mut scenic::duplicate_view_ref(&view_ref_a)?, listener_client_end_a)
        .await
        .expect("add_listener for first client");

    // Create another fake client.
    let (listener_client_end_b, mut listener_b) =
        fidl::endpoints::create_request_stream::<ui_input3::KeyboardListenerMarker>()?;
    let view_ref_b = scenic::ViewRefPair::new()?.view_ref;

    keyboard_service_client_b
        .add_listener(&mut scenic::duplicate_view_ref(&view_ref_b)?, listener_client_end_b)
        .await
        .expect("add_listener for another client");

    let key1 = input::Key::A;
    let event1_press = ui_input3::KeyEvent {
        timestamp: Some(0),
        key: Some(key1),
        type_: Some(ui_input3::KeyEventType::Pressed),
        ..ui_input3::KeyEvent::EMPTY
    };
    let event1_release = ui_input3::KeyEvent {
        timestamp: Some(0),
        key: Some(key1),
        type_: Some(ui_input3::KeyEventType::Released),
        ..ui_input3::KeyEvent::EMPTY
    };

    // Focus client A.
    focus_ctl.notify(&mut scenic::duplicate_view_ref(&view_ref_a)?).await?;

    // Press the key and expect client A to receive the event.
    dispatch_and_expect_key_event(&key_simulator, &mut listener_a, event1_press).await?;

    assert!(listener_b.next().now_or_never().is_none(), "listener_b should have no events yet");

    // Focus client B.
    // Expect a cancel event for client A and a sync event for the client B.
    let (focus_result, client_a_event, client_b_event) = future::join3(
        focus_ctl.notify(&mut scenic::duplicate_view_ref(&view_ref_b)?),
        expect_key_event(&mut listener_a),
        expect_key_event(&mut listener_b),
    )
    .await;

    focus_result?;

    assert_eq!(
        ui_input3::KeyEvent {
            timestamp: Some(0i64),
            key: Some(input::Key::A),
            type_: Some(ui_input3::KeyEventType::Cancel),
            ..ui_input3::KeyEvent::EMPTY
        },
        client_a_event
    );

    assert_eq!(
        ui_input3::KeyEvent {
            timestamp: Some(0i64),
            key: Some(input::Key::A),
            type_: Some(ui_input3::KeyEventType::Sync),
            ..ui_input3::KeyEvent::EMPTY
        },
        client_b_event
    );

    // Release the key and expect client B to receive an event.
    dispatch_and_expect_key_event(&key_simulator, &mut listener_b, event1_release).await?;

    assert!(listener_a.next().now_or_never().is_none(), "listener_a should have no more events");

    // Focus client A again.
    focus_ctl.notify(&mut scenic::duplicate_view_ref(&view_ref_a)?).await?;

    assert!(
        listener_a.next().now_or_never().is_none(),
        "listener_a should have no more events after receiving focus"
    );

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_sync_cancel_via_key_event_injector() -> Result<()> {
    fuchsia_syslog::init_with_tags(&["keyboard3_integration_test"])
        .expect("syslog init should not fail");

    let test_env = TestEnvironment::new()?;

    // This test dispatches keys via KeyEventInjector.
    let key_event_injector = test_env.connect_to_key_event_injector()?;

    let key_dispatcher =
        test_helpers::KeyEventInjectorDispatcher { key_event_injector: &key_event_injector };
    let key_simulator = test_helpers::KeySimulator::new(&key_dispatcher);

    let keyboard_service_client_a = test_env.connect_to_keyboard_service().context("client_a")?;

    let keyboard_service_client_b = test_env.connect_to_keyboard_service().context("client_b")?;

    test_sync_cancel_with_connections(
        test_env.connect_to_focus_controller()?,
        &key_simulator,
        &keyboard_service_client_a,
        &keyboard_service_client_b,
    )
    .await
}

struct TestHandles {
    _test_env: TestEnvironment,
    _keyboard_service: ui_input3::KeyboardProxy,
    listener_stream: ui_input3::KeyboardListenerRequestStream,
    injector_service: ui_input3::KeyEventInjectorProxy,
    _view_ref: ui_views::ViewRef,
}

impl TestHandles {
    async fn new() -> Result<TestHandles> {
        let _test_env = TestEnvironment::new()?;

        // Create fake client.
        let (listener_client_end, listener_stream) = create_request_stream::<
            ui_input3::KeyboardListenerMarker,
        >()
        .with_context(|| {
            format!("create_request_stream for {}", ui_input3::KeyboardListenerMarker::DEBUG_NAME)
        })?;
        let _view_ref = scenic::ViewRefPair::new()?.view_ref;

        let _keyboard_service: ui_input3::KeyboardProxy =
            _test_env.connect_to_keyboard_service()?;
        _keyboard_service
            .add_listener(&mut scenic::duplicate_view_ref(&_view_ref)?, listener_client_end)
            .await
            .expect("add_listener");

        let focus_controller = _test_env.connect_to_focus_controller()?;
        focus_controller.notify(&mut scenic::duplicate_view_ref(&_view_ref)?).await?;

        let injector_service = _test_env.connect_to_key_event_injector()?;

        Ok(TestHandles {
            _test_env,
            _keyboard_service,
            listener_stream,
            injector_service,
            _view_ref,
        })
    }
}

fn inject_key_and_receive_keyboard_protocol_message(
    event: ui_input3::KeyEvent,
) -> Result<(Result<ui_input3::KeyEventStatus, fidl::Error>, ui_input3::KeyEvent)> {
    fasync::TestExecutor::new()?.run_singlethreaded(async {
        let mut handles = TestHandles::new().await?;

        let (was_handled, received_event) = future::join(
            handles.injector_service.inject(event),
            expect_key_event(&mut handles.listener_stream),
        )
        .await;
        Ok((was_handled, received_event))
    })
}

#[test_case(input::Key::A, None => (Some(input::Key::A), None); "without_key_meaning")]
#[test_case(input::Key::A, 'a'
             => (Some(input::Key::A), Some(ui_input3::KeyMeaning::Codepoint(0x61)));
            "key_and_meaning")]
#[test_case(None, 'a'
            => (None, Some(ui_input3::KeyMeaning::Codepoint(0x61)));
            "only_key_meaning_lower_alpha")]
#[test_case(None, 'A'
            => (None, Some(ui_input3::KeyMeaning::Codepoint(0x41)));
            "only_key_meaning_upper_alpha")]
#[test_case(None, '1'
            => (None, Some(ui_input3::KeyMeaning::Codepoint(0x31)));
            "only_key_meaning_numeric")]
#[test_case(None, ui_input3::KeyMeaning::NonPrintableKey(ui_input3::NonPrintableKey::Enter)
            => (Some(input::Key::Enter),
                Some(ui_input3::KeyMeaning::NonPrintableKey(ui_input3::NonPrintableKey::Enter)));
            "only_key_meaning_enter")]
#[test_case(None, ui_input3::KeyMeaning::NonPrintableKey(ui_input3::NonPrintableKey::Tab)
            => (Some(input::Key::Tab),
                Some(ui_input3::KeyMeaning::NonPrintableKey(ui_input3::NonPrintableKey::Tab)));
            "only_key_meaning_tab")]
#[test_case(None, ui_input3::KeyMeaning::NonPrintableKey(ui_input3::NonPrintableKey::Backspace)
            => (Some(input::Key::Backspace),
                Some(ui_input3::KeyMeaning::NonPrintableKey(ui_input3::NonPrintableKey::Backspace)));
            "only_key_meaning_backspace")]
fn test_inject_key_yields_expected_key_and_key_meaning(
    key: impl Into<Option<input::Key>>,
    key_meaning: impl Into<test_helpers::KeyMeaningWrapper>,
) -> (Option<input::Key>, Option<ui_input3::KeyMeaning>) {
    let (was_handled, received_event) = inject_key_and_receive_keyboard_protocol_message(
        create_key_event(zx::Time::ZERO, ui_input3::KeyEventType::Pressed, key, None, key_meaning),
    )
    .expect("injection failed");
    matches::assert_matches!(was_handled, Ok(ui_input3::KeyEventStatus::Handled));
    (received_event.key, received_event.key_meaning)
}
