// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_input as input, fidl_fuchsia_ui_input as ui_input,
    fuchsia_async::{self as fasync},
    fuchsia_component::client::connect_to_service,
    futures::{StreamExt, TryStreamExt},
    input_synthesis::usages::Usages,
};

use crate::test_helpers::{bind_editor, get_state_update, simulate_keypress};

mod test_helpers;

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
    let (state, event) = get_state_update(&mut editor_stream).await?;
    let event = event.expect("expected event to be set");
    assert_eq!(event.phase, ui_input::KeyboardEventPhase::Pressed);
    assert_eq!(event.code_point, 97);
    assert_eq!(state.text, "");

    // get second message with state update
    let (state, event) = get_state_update(&mut editor_stream).await?;
    assert!(event.is_none());
    assert_eq!(state.text, "a");
    assert_eq!(state.selection.base, 1);
    assert_eq!(state.selection.extent, 1);

    // get third message with keyrelease event but no state update
    let (state, event) = get_state_update(&mut editor_stream).await?;
    let event = event.expect("expected event to be set");
    assert_eq!(event.phase, ui_input::KeyboardEventPhase::Released);
    assert_eq!(event.code_point, 97);
    assert_eq!(state.text, "a");

    // press left arrow
    simulate_keypress(&ime_service, input::Key::Left).await?;

    // get first message with keypress event but no state update
    let (state, event) = get_state_update(&mut editor_stream).await?;
    let event = event.expect("expected event to be set");
    assert_eq!(event.phase, ui_input::KeyboardEventPhase::Pressed);
    assert_eq!(event.code_point, 0);
    assert_eq!(event.hid_usage, Usages::HidUsageKeyLeft as u32);
    assert_eq!(state.text, "a");

    // get second message with state update
    let (state, event) = get_state_update(&mut editor_stream).await?;
    assert!(event.is_none());
    assert_eq!(state.text, "a");
    assert_eq!(state.selection.base, 0);
    assert_eq!(state.selection.extent, 0);

    // get first message with keyrelease event but no state update
    let (state, event) = get_state_update(&mut editor_stream).await?;
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
    let (_state, event) = get_state_update(&mut editor_stream).await?;
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
