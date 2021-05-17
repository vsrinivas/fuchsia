// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_input as input, fidl_fuchsia_ui_input as ui_input,
    fuchsia_async::{self as fasync},
    fuchsia_component::client::connect_to_protocol,
};

use test_helpers::{
    bind_editor, default_state, get_action, get_state_update, measure_utf16, setup_ime,
    simulate_ime_keypress, simulate_ime_keypress_with_held_keys,
};

#[fasync::run_singlethreaded(test)]
async fn test_delete_backward_empty_string() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "", -1, -1).await?;

    simulate_ime_keypress(&mut ime, input::Key::Backspace).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);

    // a second delete still does nothing, but increments revision
    simulate_ime_keypress(&mut ime, input::Key::Backspace).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(3, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_delete_forward_empty_string() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "", -1, -1).await?;

    simulate_ime_keypress(&mut ime, input::Key::Delete).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);

    // a second delete still does nothing, but increments revision
    simulate_ime_keypress(&mut ime, input::Key::Delete).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(3, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_delete_backward_beginning_string() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdefghi", 0, 0).await?;

    simulate_ime_keypress(&mut ime, input::Key::Backspace).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghi", state.text);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_delete_forward_beginning_string() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdefghi", 0, 0).await?;

    simulate_ime_keypress(&mut ime, input::Key::Delete).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("bcdefghi", state.text);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_delete_backward_first_char_selected() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdefghi", 0, 1).await?;

    simulate_ime_keypress(&mut ime, input::Key::Backspace).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("bcdefghi", state.text);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_delete_forward_last_char_selected() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdefghi", 8, 9).await?;

    simulate_ime_keypress(&mut ime, input::Key::Delete).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("abcdefgh", state.text);
    assert_eq!(8, state.selection.base);
    assert_eq!(8, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_delete_backward_end_string() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdefghi", 9, 9).await?;

    simulate_ime_keypress(&mut ime, input::Key::Backspace).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("abcdefgh", state.text);
    assert_eq!(8, state.selection.base);
    assert_eq!(8, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_delete_forward_end_string() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdefghi", 9, 9).await?;

    simulate_ime_keypress(&mut ime, input::Key::Delete).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghi", state.text);
    assert_eq!(9, state.selection.base);
    assert_eq!(9, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_delete_backward_combining_diacritic() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    // U+0301: combining acute accent. 2 bytes.
    let text = "abcdefghi\u{0301}";
    let len = measure_utf16(text) as i64;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, text, len, len).await?;

    simulate_ime_keypress(&mut ime, input::Key::Backspace).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghi", state.text);
    assert_eq!(9, state.selection.base);
    assert_eq!(9, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_delete_forward_combining_diacritic() -> Result<(), Error> {
    // U+0301: combining acute accent. 2 bytes.
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdefghi\u{0301}jkl", 8, 8).await?;

    simulate_ime_keypress(&mut ime, input::Key::Delete).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghjkl", state.text);
    assert_eq!(8, state.selection.base);
    assert_eq!(8, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_delete_backward_emoji() -> Result<(), Error> {
    // Emoji with a color modifier.
    let text = "abcdefghi👦🏻";
    let len = measure_utf16(text) as i64;
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, text, len, len).await?;

    simulate_ime_keypress(&mut ime, input::Key::Backspace).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghi", state.text);
    assert_eq!(9, state.selection.base);
    assert_eq!(9, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_delete_forward_emoji() -> Result<(), Error> {
    // Emoji with a color modifier.
    let text = "abcdefghi👦🏻";
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, text, 9, 9).await?;

    simulate_ime_keypress(&mut ime, input::Key::Delete).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghi", state.text);
    assert_eq!(9, state.selection.base);
    assert_eq!(9, state.selection.extent);

    Ok(())
}

/// Flags are more complicated because they consist of two REGIONAL INDICATOR SYMBOL LETTERs.
#[fasync::run_singlethreaded(test)]
async fn test_delete_backward_flag() -> Result<(), Error> {
    // French flag
    let text = "abcdefghi🇫🇷";
    let len = measure_utf16(text) as i64;
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, text, len, len).await?;

    simulate_ime_keypress(&mut ime, input::Key::Backspace).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghi", state.text);
    assert_eq!(9, state.selection.base);
    assert_eq!(9, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_delete_forward_flag() -> Result<(), Error> {
    // French flag
    let text = "abcdefghi🇫🇷";
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, text, 9, 9).await?;

    simulate_ime_keypress(&mut ime, input::Key::Delete).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghi", state.text);
    assert_eq!(9, state.selection.base);
    assert_eq!(9, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_delete_selection() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdefghi", 3, 6).await?;

    simulate_ime_keypress(&mut ime, input::Key::Backspace).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("abcghi", state.text);
    assert_eq!(3, state.selection.base);
    assert_eq!(3, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_delete_selection_inverted() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdefghi", 6, 3).await?;

    simulate_ime_keypress(&mut ime, input::Key::Backspace).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("abcghi", state.text);
    assert_eq!(3, state.selection.base);
    assert_eq!(3, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_delete_no_selection() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdefghi", -1, -1).await?;

    simulate_ime_keypress(&mut ime, input::Key::Backspace).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghi", state.text);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_delete_with_zero_width_selection() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdefghi", 3, 3).await?;

    simulate_ime_keypress(&mut ime, input::Key::Backspace).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("abdefghi", state.text);
    assert_eq!(2, state.selection.base);
    assert_eq!(2, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_delete_with_zero_width_selection_at_end() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdefghi", 9, 9).await?;

    simulate_ime_keypress(&mut ime, input::Key::Backspace).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("abcdefgh", state.text);
    assert_eq!(8, state.selection.base);
    assert_eq!(8, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_delete_selection_out_of_bounds() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdefghi", 20, 24).await?;

    simulate_ime_keypress(&mut ime, input::Key::Backspace).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghi", state.text);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_cursor_left_on_selection() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdefghi", 1, 5).await?;

    // right with shift
    simulate_ime_keypress_with_held_keys(&mut ime, input::Key::Right, vec![input::Key::LeftShift])
        .await;

    // Skip the update for the shift key stroke.
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!(1, state.selection.base);
    assert_eq!(6, state.selection.extent);

    simulate_ime_keypress(&mut ime, input::Key::Left).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(3, state.revision);
    assert_eq!(1, state.selection.base);
    assert_eq!(1, state.selection.extent);

    simulate_ime_keypress(&mut ime, input::Key::Left).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(4, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);

    simulate_ime_keypress(&mut ime, input::Key::Left).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(5, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_cursor_left_on_inverted_selection() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdefghi", 6, 3).await?;

    simulate_ime_keypress(&mut ime, input::Key::Left).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!(3, state.selection.base);
    assert_eq!(3, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_cursor_right_on_selection() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdefghi", 3, 9).await?;

    // left with shift
    simulate_ime_keypress_with_held_keys(&mut ime, input::Key::Left, vec![input::Key::LeftShift])
        .await;

    // Skip the update for the shift key stroke.
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!(3, state.selection.base);
    assert_eq!(8, state.selection.extent);

    simulate_ime_keypress(&mut ime, input::Key::Right).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(3, state.revision);
    assert_eq!(8, state.selection.base);
    assert_eq!(8, state.selection.extent);

    simulate_ime_keypress(&mut ime, input::Key::Right).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(4, state.revision);
    assert_eq!(9, state.selection.base);
    assert_eq!(9, state.selection.extent);

    simulate_ime_keypress(&mut ime, input::Key::Right).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(5, state.revision);
    assert_eq!(9, state.selection.base);
    assert_eq!(9, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_cursor_word_left_no_words() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "¿ - _ - ?", 5, 5).await?;

    // left with control
    simulate_ime_keypress_with_held_keys(&mut ime, input::Key::Left, vec![input::Key::LeftCtrl])
        .await;

    // Skip the update for the ctrl key stroke.
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_cursor_word_left() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "4.2 . foobar", 7, 7).await?;

    simulate_ime_keypress_with_held_keys(&mut ime, input::Key::Left, vec![input::Key::LeftCtrl])
        .await;

    // Skip the update for the ctrl key stroke.
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!(6, state.selection.base);
    assert_eq!(6, state.selection.extent);

    simulate_ime_keypress_with_held_keys(&mut ime, input::Key::Left, vec![input::Key::LeftCtrl])
        .await;

    // Skip the update for the ctrl key stroke.
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(3, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_cursor_word_right_no_words() -> Result<(), Error> {
    let text = "¿ - _ - ?";
    let text_len = measure_utf16(text) as i64;
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, text, 5, 5).await?;

    // right with control
    simulate_ime_keypress_with_held_keys(&mut ime, input::Key::Right, vec![input::Key::LeftCtrl])
        .await;

    // Skip the update for the ctrl key stroke.
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!(text_len, state.selection.base);
    assert_eq!(text_len, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_cursor_word_right() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "4.2 . foobar", 1, 1).await?;

    simulate_ime_keypress_with_held_keys(&mut ime, input::Key::Right, vec![input::Key::LeftCtrl])
        .await;

    // Skip the update for the ctrl key stroke.
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!(3, state.selection.base);
    assert_eq!(3, state.selection.extent);

    simulate_ime_keypress_with_held_keys(&mut ime, input::Key::Right, vec![input::Key::LeftCtrl])
        .await;

    // Skip the update for the ctrl key stroke.
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(3, state.revision);
    assert_eq!(12, state.selection.base);
    assert_eq!(12, state.selection.extent);

    // Try to navigate off text limits.
    simulate_ime_keypress_with_held_keys(&mut ime, input::Key::Right, vec![input::Key::LeftCtrl])
        .await;

    // Skip the update for the ctrl key stroke.
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(4, state.revision);
    assert_eq!(12, state.selection.base);
    assert_eq!(12, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_cursor_word_off_limits() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "word", 1, 1).await?;

    simulate_ime_keypress_with_held_keys(&mut ime, input::Key::Right, vec![input::Key::LeftCtrl])
        .await;

    // Skip the update for the ctrl key stroke.
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!(4, state.selection.base);
    assert_eq!(4, state.selection.extent);

    simulate_ime_keypress_with_held_keys(&mut ime, input::Key::Right, vec![input::Key::LeftCtrl])
        .await;

    // Skip the update for the ctrl key stroke.
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(3, state.revision);
    assert_eq!(4, state.selection.base);
    assert_eq!(4, state.selection.extent);

    simulate_ime_keypress_with_held_keys(&mut ime, input::Key::Left, vec![input::Key::LeftCtrl])
        .await;

    // Skip the update for the ctrl key stroke.
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(4, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);

    simulate_ime_keypress_with_held_keys(&mut ime, input::Key::Left, vec![input::Key::LeftCtrl])
        .await;

    // Skip the update for the ctrl key stroke.
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(5, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_cursor_word() -> Result<(), Error> {
    let start_idx = measure_utf16("a.c   2.") as i64;
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "a.c   2.2 ¿? x yz", start_idx, start_idx).await?;

    simulate_ime_keypress_with_held_keys(
        &mut ime,
        input::Key::Right,
        vec![input::Key::LeftShift, input::Key::LeftCtrl],
    )
    .await;

    // Skip the update for the shift and the ctrl key stroke.
    get_state_update(&mut editor_stream).await?;
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!(start_idx, state.selection.base);
    assert_eq!(measure_utf16("a.c   2.2") as i64, state.selection.extent);

    simulate_ime_keypress_with_held_keys(
        &mut ime,
        input::Key::Right,
        vec![input::Key::LeftShift, input::Key::LeftCtrl],
    )
    .await;

    // Skip the update for the shift and the ctrl key stroke.
    get_state_update(&mut editor_stream).await?;
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(3, state.revision);
    assert_eq!(start_idx, state.selection.base);
    assert_eq!(measure_utf16("a.c   2.2 ¿? x") as i64, state.selection.extent);

    simulate_ime_keypress_with_held_keys(&mut ime, input::Key::Left, vec![input::Key::LeftCtrl])
        .await;

    // Skip the update for the ctrl key stroke.
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(4, state.revision);
    assert_eq!(measure_utf16("a.c   ") as i64, state.selection.base);
    assert_eq!(measure_utf16("a.c   ") as i64, state.selection.extent);

    simulate_ime_keypress_with_held_keys(
        &mut ime,
        input::Key::Left,
        vec![input::Key::LeftShift, input::Key::LeftCtrl],
    )
    .await;

    // Skip the update for the shift and the ctrl key stroke.
    get_state_update(&mut editor_stream).await?;
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(5, state.revision);
    assert_eq!(measure_utf16("a.c   ") as i64, state.selection.base);
    assert_eq!(0, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_type_empty_string() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "", 0, 0).await?;

    simulate_ime_keypress(&mut ime, input::Key::A).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("a", state.text);
    assert_eq!(1, state.selection.base);
    assert_eq!(1, state.selection.extent);

    simulate_ime_keypress(&mut ime, input::Key::B).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(3, state.revision);
    assert_eq!("ab", state.text);
    assert_eq!(2, state.selection.base);
    assert_eq!(2, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_type_at_beginning() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "cde", 0, 0).await?;

    simulate_ime_keypress(&mut ime, input::Key::A).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("acde", state.text);
    assert_eq!(1, state.selection.base);
    assert_eq!(1, state.selection.extent);

    simulate_ime_keypress(&mut ime, input::Key::B).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(3, state.revision);
    assert_eq!("abcde", state.text);
    assert_eq!(2, state.selection.base);
    assert_eq!(2, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_type_selection() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdef", 2, 5).await?;

    simulate_ime_keypress(&mut ime, input::Key::X).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("abxf", state.text);
    assert_eq!(3, state.selection.base);
    assert_eq!(3, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_type_inverted_selection() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdef", 5, 2).await?;

    simulate_ime_keypress(&mut ime, input::Key::X).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("abxf", state.text);
    assert_eq!(3, state.selection.base);
    assert_eq!(3, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_type_invalid_selection() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdef", -10, 1).await?;

    simulate_ime_keypress(&mut ime, input::Key::X).await;
    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("xbcdef", state.text);
    assert_eq!(1, state.selection.base);
    assert_eq!(1, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_set_state() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdef", 1, 1).await?;

    let mut override_state = default_state();
    override_state.text = "meow?".to_string();
    override_state.selection.base = 4;
    override_state.selection.extent = 5;
    ime.set_state(&mut override_state)?;

    simulate_ime_keypress_with_held_keys(&mut ime, input::Key::Key1, vec![input::Key::LeftShift])
        .await;

    // Skip the update for the shift key stroke.
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("meow!", state.text);
    assert_eq!(5, state.selection.base);
    assert_eq!(5, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_action() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "abcdef", 1, 1).await?;

    simulate_ime_keypress(&mut ime, input::Key::Enter).await;
    // assert DID send action
    assert!(get_action(&mut editor_stream).await.is_ok());

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_unicode_selection() -> Result<(), Error> {
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "m😸eow", 1, 1).await?;

    simulate_ime_keypress_with_held_keys(&mut ime, input::Key::Right, vec![input::Key::LeftShift])
        .await;

    // Skip the update for the shift key stroke.
    get_state_update(&mut editor_stream).await?;

    let (_state, _event) = get_state_update(&mut editor_stream).await?;

    simulate_ime_keypress_with_held_keys(
        &mut ime,
        input::Key::Backspace,
        vec![input::Key::LeftShift],
    )
    .await;

    // Skip the update for the shift key stroke.
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(3, state.revision);
    assert_eq!("meow", state.text);
    assert_eq!(1, state.selection.base);
    assert_eq!(1, state.selection.extent);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_unicode_backspace() -> Result<(), Error> {
    let base: i64 = measure_utf16("m😸") as i64;

    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let (mut ime, mut editor_stream) = bind_editor(&ime_service)?;
    setup_ime(&ime, "m😸eow", base, base).await?;

    simulate_ime_keypress_with_held_keys(
        &mut ime,
        input::Key::Backspace,
        vec![input::Key::LeftShift],
    )
    .await;

    // Skip the update for the shift key stroke.
    get_state_update(&mut editor_stream).await?;

    let (state, _event) = get_state_update(&mut editor_stream).await?;
    assert_eq!(2, state.revision);
    assert_eq!("meow", state.text);
    assert_eq!(1, state.selection.base);
    assert_eq!(1, state.selection.extent);

    Ok(())
}
