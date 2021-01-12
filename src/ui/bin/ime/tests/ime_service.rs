// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_input as input, fidl_fuchsia_ui_input as ui_input,
    fidl_fuchsia_ui_input3 as ui_input3,
    fuchsia_async::{self as fasync},
    fuchsia_component::client::connect_to_service,
    futures::{StreamExt, TryStreamExt},
    input_synthesis::usages::Usages,
};

fn default_state() -> ui_input::TextInputState {
    ui_input::TextInputState {
        revision: 1,
        text: "".to_string(),
        selection: ui_input::TextSelection {
            base: 0,
            extent: 0,
            affinity: ui_input::TextAffinity::Upstream,
        },
        composing: ui_input::TextRange { start: -1, end: -1 },
    }
}

fn bind_editor(
    ime_service: &ui_input::ImeServiceProxy,
) -> Result<(ui_input::InputMethodEditorProxy, ui_input::InputMethodEditorClientRequestStream), Error>
{
    let (ime, ime_server_end) =
        fidl::endpoints::create_proxy::<ui_input::InputMethodEditorMarker>().unwrap();
    let (editor_client_end, editor_request_stream) =
        fidl::endpoints::create_request_stream().unwrap();
    ime_service.get_input_method_editor(
        ui_input::KeyboardType::Text,
        ui_input::InputMethodAction::Done,
        &mut default_state(),
        editor_client_end,
        ime_server_end,
    )?;

    Ok((ime, editor_request_stream))
}

async fn simulate_keypress(
    ime_service: &ui_input::ImeServiceProxy,
    key: input::Key,
) -> Result<(), Error> {
    ime_service
        .dispatch_key3(ui_input3::KeyEvent {
            type_: Some(ui_input3::KeyEventType::Pressed),
            key: Some(key),
            ..ui_input3::KeyEvent::EMPTY
        })
        .await?;
    ime_service
        .dispatch_key3(ui_input3::KeyEvent {
            type_: Some(ui_input3::KeyEventType::Released),
            key: Some(key),
            ..ui_input3::KeyEvent::EMPTY
        })
        .await?;

    Ok(())
}

async fn get_state_update(
    editor_stream: &mut ui_input::InputMethodEditorClientRequestStream,
) -> (ui_input::TextInputState, Option<ui_input::KeyboardEvent>) {
    let msg = editor_stream
        .try_next()
        .await
        .expect("expected working event stream")
        .expect("ime should have sent message");
    if let ui_input::InputMethodEditorClientRequest::DidUpdateState { state, event, .. } = msg {
        let keyboard_event = event.map(|e| {
            if let ui_input::InputEvent::Keyboard(keyboard_event) = *e {
                keyboard_event
            } else {
                panic!("expected DidUpdateState to only send Keyboard events");
            }
        });
        (state, keyboard_event)
    } else {
        panic!("request should be DidUpdateState");
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_visibility_service_sends_initial_update() -> Result<(), Error> {
    let visibility_service = connect_to_service::<ui_input::ImeVisibilityServiceMarker>()
        .context("Failed to connect to ImeVisibilityService")?;
    let mut visiblity_event_stream = visibility_service.take_event_stream();

    let ui_input::ImeVisibilityServiceEvent::OnKeyboardVisibilityChanged { visible } =
        visiblity_event_stream
            .try_next()
            .await
            .expect("expected working event stream")
            .expect("visibility service should have sent message");
    assert_eq!(visible, false);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_visibility_service_resends_update_after_closing_keyboard() -> Result<(), Error> {
    let visibility_service = connect_to_service::<ui_input::ImeVisibilityServiceMarker>()
        .context("Failed to connect to ImeVisibilityService")?;
    let ime_service = connect_to_service::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let visiblity_event_stream = visibility_service.take_event_stream();

    // Hide keyboard using `fuchsia.ui.input.ImeService`.
    ime_service.hide_keyboard()?;

    let ui_input::ImeVisibilityServiceEvent::OnKeyboardVisibilityChanged { visible } =
        visiblity_event_stream
            .skip(1)
            .try_next()
            .await
            .expect("expected working event stream")
            .expect("visibility service should have sent message");
    assert_eq!(visible, false);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_visibility_service_resends_update_after_opening_keyboard() -> Result<(), Error> {
    let visibility_service = connect_to_service::<ui_input::ImeVisibilityServiceMarker>()
        .context("Failed to connect to ImeVisibilityService")?;
    let ime_service = connect_to_service::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let visiblity_event_stream = visibility_service.take_event_stream();

    // Show keyboard using `fuchsia.ui.input.ImeService`.
    ime_service.show_keyboard()?;

    let ui_input::ImeVisibilityServiceEvent::OnKeyboardVisibilityChanged { visible } =
        visiblity_event_stream
            .skip(1)
            .try_next()
            .await
            .expect("expected working event stream")
            .expect("visibility service should have sent message");
    assert_eq!(visible, true);

    // Call `hide_keyboard()` to restore state.
    ime_service.hide_keyboard()?;

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_open_and_close_from_keyboard() -> Result<(), Error> {
    let visibility_service = connect_to_service::<ui_input::ImeVisibilityServiceMarker>()
        .context("Failed to connect to ImeVisibilityService")?;
    let ime_service = connect_to_service::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    // Note that the initial message from the visibility service is skipped.
    let mut visiblity_event_stream = visibility_service.take_event_stream().skip(1);

    let (ime, _editor_stream) = bind_editor(&ime_service)?;

    // Show keyboard using `fuchsia.ui.input.InputMethodEditor`.
    ime.show()?;

    let ui_input::ImeVisibilityServiceEvent::OnKeyboardVisibilityChanged { visible } =
        visiblity_event_stream
            .try_next()
            .await
            .expect("expected working event stream")
            .expect("visibility service should have sent message");
    assert_eq!(visible, true);

    // Hide keyboard using `fuchsia.ui.input.InputMethodEditor`.
    ime.hide()?;

    let ui_input::ImeVisibilityServiceEvent::OnKeyboardVisibilityChanged { visible } =
        visiblity_event_stream
            .try_next()
            .await
            .expect("expected working event stream")
            .expect("visibility service should have sent message");
    assert_eq!(visible, false);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_input_updates_ime_state() -> Result<(), Error> {
    let ime_service = connect_to_service::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (_ime, mut editor_stream) = bind_editor(&ime_service)?;

    simulate_keypress(&ime_service, input::Key::A).await?;

    // get first message with keypress event but no state update
    let (state, event) = get_state_update(&mut editor_stream).await;
    let event = event.expect("expected event to be set");
    assert_eq!(event.phase, ui_input::KeyboardEventPhase::Pressed);
    assert_eq!(event.code_point, 97);
    assert_eq!(state.text, "");

    // get second message with state update
    let (state, event) = get_state_update(&mut editor_stream).await;
    assert!(event.is_none());
    assert_eq!(state.text, "a");
    assert_eq!(state.selection.base, 1);
    assert_eq!(state.selection.extent, 1);

    // get third message with keyrelease event but no state update
    let (state, event) = get_state_update(&mut editor_stream).await;
    let event = event.expect("expected event to be set");
    assert_eq!(event.phase, ui_input::KeyboardEventPhase::Released);
    assert_eq!(event.code_point, 97);
    assert_eq!(state.text, "a");

    // press left arrow
    simulate_keypress(&ime_service, input::Key::Left).await?;

    // get first message with keypress event but no state update
    let (state, event) = get_state_update(&mut editor_stream).await;
    let event = event.expect("expected event to be set");
    assert_eq!(event.phase, ui_input::KeyboardEventPhase::Pressed);
    assert_eq!(event.code_point, 0);
    assert_eq!(event.hid_usage, Usages::HidUsageKeyLeft as u32);
    assert_eq!(state.text, "a");

    // get second message with state update
    let (state, event) = get_state_update(&mut editor_stream).await;
    assert!(event.is_none());
    assert_eq!(state.text, "a");
    assert_eq!(state.selection.base, 0);
    assert_eq!(state.selection.extent, 0);

    // get first message with keyrelease event but no state update
    let (state, event) = get_state_update(&mut editor_stream).await;
    let event = event.expect("expected event to be set");
    assert_eq!(event.phase, ui_input::KeyboardEventPhase::Released);
    assert_eq!(event.code_point, 0);
    assert_eq!(event.hid_usage, Usages::HidUsageKeyLeft as u32);
    assert_eq!(state.text, "a");

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_input_triggers_action() -> Result<(), Error> {
    let ime_service = connect_to_service::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (_ime, mut editor_stream) = bind_editor(&ime_service)?;

    // send key events
    simulate_keypress(&ime_service, input::Key::Enter).await?;

    // get first message with keypress event
    let (_state, event) = get_state_update(&mut editor_stream).await;
    let event = event.expect("expected event to be set");
    assert_eq!(event.phase, ui_input::KeyboardEventPhase::Pressed);
    assert_eq!(event.code_point, 0);
    assert_eq!(event.hid_usage, Usages::HidUsageKeyEnter as u32);

    // get second message with onaction event
    let msg = editor_stream
        .try_next()
        .await
        .expect("expected working event stream")
        .expect("ime should have sent message");
    if let ui_input::InputMethodEditorClientRequest::OnAction { action, .. } = msg {
        assert_eq!(action, ui_input::InputMethodAction::Done);
    } else {
        panic!("request should be OnAction");
    };

    Ok(())
}
