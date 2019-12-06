// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains tests for `LegacyIme`.

use super::handler::*;
use super::*;
use crate::fidl_helpers::clone_state;
use crate::fidl_helpers::default_state;
use crate::ime_service::ImeService;
use fidl;
use fidl_fuchsia_ui_input as uii;
use fuchsia_async::run_until_stalled;
use fuchsia_zircon as zx;
use parking_lot::Mutex as SyncMutex;
use std::sync::mpsc::{channel, Receiver, Sender};

async fn set_up(
    text: &str,
    base: i64,
    extent: i64,
) -> (LegacyIme, Receiver<uii::TextInputState>, Receiver<uii::InputMethodAction>) {
    let (client, statechan, actionchan) = MockImeClient::new();
    let ime = LegacyIme::new(
        uii::KeyboardType::Text,
        uii::InputMethodAction::Search,
        default_state(),
        client,
        ImeService::new(),
    );
    let mut state = default_state();
    state.text = text.to_string();
    state.selection.base = base;
    state.selection.extent = extent;
    // set state through set_state fn, so we can test codeunit->byte transaction works as expected
    ime.set_state(state).await;
    (ime, statechan, actionchan)
}

fn measure_utf16(s: &str) -> usize {
    s.chars().map(|c| c.len_utf16()).sum::<usize>()
}

async fn simulate_keypress<K: Into<u32> + Copy + 'static>(
    ime: &mut LegacyIme,
    key: K,
    hid_key: bool,
    modifiers: u32,
) {
    let hid_usage = if hid_key { key.into() } else { 0 };
    let code_point = if hid_key { 0 } else { key.into() };
    ime.inject_input(uii::KeyboardEvent {
        event_time: 0,
        device_id: 0,
        phase: uii::KeyboardEventPhase::Pressed,
        hid_usage,
        code_point,
        modifiers,
    })
    .await;
    ime.inject_input(uii::KeyboardEvent {
        event_time: 0,
        device_id: 0,
        phase: uii::KeyboardEventPhase::Released,
        hid_usage,
        code_point,
        modifiers,
    })
    .await;
}

struct MockImeClient {
    pub state: SyncMutex<Sender<uii::TextInputState>>,
    pub action: SyncMutex<Sender<uii::InputMethodAction>>,
}
impl MockImeClient {
    fn new() -> (MockImeClient, Receiver<uii::TextInputState>, Receiver<uii::InputMethodAction>) {
        let (s_send, s_rec) = channel();
        let (a_send, a_rec) = channel();
        let client =
            MockImeClient { state: SyncMutex::new(s_send), action: SyncMutex::new(a_send) };
        (client, s_rec, a_rec)
    }
}
impl uii::InputMethodEditorClientProxyInterface for MockImeClient {
    fn did_update_state(
        &self,
        state: &mut uii::TextInputState,
        mut _event: Option<&mut uii::InputEvent>,
    ) -> Result<(), fidl::Error> {
        let state2 = clone_state(state);
        self.state
            .lock()
            .send(state2)
            .map_err(|_| fidl::Error::ClientWrite(zx::Status::PEER_CLOSED))
    }
    fn on_action(&self, action: uii::InputMethodAction) -> Result<(), fidl::Error> {
        self.action
            .lock()
            .send(action)
            .map_err(|_| fidl::Error::ClientWrite(zx::Status::PEER_CLOSED))
    }
}

#[run_until_stalled(test)]
async fn test_mock_ime_channels() {
    let (client, statechan, actionchan) = MockImeClient::new();
    let mut ime = LegacyIme::new(
        uii::KeyboardType::Text,
        uii::InputMethodAction::Search,
        default_state(),
        client,
        ImeService::new(),
    );
    assert_eq!(true, statechan.try_recv().is_err());
    assert_eq!(true, actionchan.try_recv().is_err());
    simulate_keypress(&mut ime, 'a', false, uii::MODIFIER_NONE).await;
    assert_eq!(false, statechan.try_recv().is_err());
    assert_eq!(true, actionchan.try_recv().is_err());
}

#[run_until_stalled(test)]
async fn test_delete_backward_empty_string() {
    let (mut ime, statechan, _actionchan) = set_up("", -1, -1).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);

    // a second delete still does nothing, but increments revision
    simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(3, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_delete_forward_empty_string() {
    let (mut ime, statechan, _actionchan) = set_up("", -1, -1).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_DELETE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);

    // a second delete still does nothing, but increments revision
    simulate_keypress(&mut ime, HID_USAGE_KEY_DELETE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(3, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_delete_backward_beginning_string() {
    let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 0, 0).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghi", state.text);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_delete_forward_beginning_string() {
    let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 0, 0).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_DELETE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("bcdefghi", state.text);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_delete_backward_first_char_selected() {
    let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 0, 1).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("bcdefghi", state.text);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_delete_forward_last_char_selected() {
    let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 8, 9).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_DELETE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("abcdefgh", state.text);
    assert_eq!(8, state.selection.base);
    assert_eq!(8, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_delete_backward_end_string() {
    let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 9, 9).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("abcdefgh", state.text);
    assert_eq!(8, state.selection.base);
    assert_eq!(8, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_delete_forward_end_string() {
    let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 9, 9).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_DELETE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghi", state.text);
    assert_eq!(9, state.selection.base);
    assert_eq!(9, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_delete_backward_combining_diacritic() {
    // U+0301: combining acute accent. 2 bytes.
    let text = "abcdefghi\u{0301}";
    let len = measure_utf16(text) as i64;
    let (mut ime, statechan, _actionchan) = set_up(text, len, len).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghi", state.text);
    assert_eq!(9, state.selection.base);
    assert_eq!(9, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_delete_forward_combining_diacritic() {
    // U+0301: combining acute accent. 2 bytes.
    let (mut ime, statechan, _actionchan) = set_up("abcdefghi\u{0301}jkl", 8, 8).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_DELETE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghjkl", state.text);
    assert_eq!(8, state.selection.base);
    assert_eq!(8, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_delete_backward_emoji() {
    // Emoji with a color modifier.
    let text = "abcdefghi👦🏻";
    let len = measure_utf16(text) as i64;
    let (mut ime, statechan, _actionchan) = set_up(text, len, len).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghi", state.text);
    assert_eq!(9, state.selection.base);
    assert_eq!(9, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_delete_forward_emoji() {
    // Emoji with a color modifier.
    let text = "abcdefghi👦🏻";
    let (mut ime, statechan, _actionchan) = set_up(text, 9, 9).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_DELETE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghi", state.text);
    assert_eq!(9, state.selection.base);
    assert_eq!(9, state.selection.extent);
}

/// Flags are more complicated because they consist of two REGIONAL INDICATOR SYMBOL LETTERs.
#[run_until_stalled(test)]
async fn test_delete_backward_flag() {
    // French flag
    let text = "abcdefghi🇫🇷";
    let len = measure_utf16(text) as i64;
    let (mut ime, statechan, _actionchan) = set_up(text, len, len).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghi", state.text);
    assert_eq!(9, state.selection.base);
    assert_eq!(9, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_delete_forward_flag() {
    // French flag
    let text = "abcdefghi🇫🇷";
    let (mut ime, statechan, _actionchan) = set_up(text, 9, 9).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_DELETE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghi", state.text);
    assert_eq!(9, state.selection.base);
    assert_eq!(9, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_delete_selection() {
    let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 3, 6).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("abcghi", state.text);
    assert_eq!(3, state.selection.base);
    assert_eq!(3, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_delete_selection_inverted() {
    let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 6, 3).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("abcghi", state.text);
    assert_eq!(3, state.selection.base);
    assert_eq!(3, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_delete_no_selection() {
    let (mut ime, statechan, _actionchan) = set_up("abcdefghi", -1, -1).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghi", state.text);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_delete_with_zero_width_selection() {
    let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 3, 3).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("abdefghi", state.text);
    assert_eq!(2, state.selection.base);
    assert_eq!(2, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_delete_with_zero_width_selection_at_end() {
    let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 9, 9).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("abcdefgh", state.text);
    assert_eq!(8, state.selection.base);
    assert_eq!(8, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_delete_selection_out_of_bounds() {
    let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 20, 24).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("abcdefghi", state.text);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_cursor_left_on_selection() {
    let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 1, 5).await;

    // right with shift
    simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_SHIFT).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!(1, state.selection.base);
    assert_eq!(6, state.selection.extent);

    simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(3, state.revision);
    assert_eq!(1, state.selection.base);
    assert_eq!(1, state.selection.extent);

    simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(4, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);

    simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(5, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_cursor_left_on_inverted_selection() {
    let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 6, 3).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!(3, state.selection.base);
    assert_eq!(3, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_cursor_right_on_selection() {
    let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 3, 9).await;

    // left with shift
    simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_SHIFT).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!(3, state.selection.base);
    assert_eq!(8, state.selection.extent);

    simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(3, state.revision);
    assert_eq!(8, state.selection.base);
    assert_eq!(8, state.selection.extent);

    simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(4, state.revision);
    assert_eq!(9, state.selection.base);
    assert_eq!(9, state.selection.extent);

    simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(5, state.revision);
    assert_eq!(9, state.selection.base);
    assert_eq!(9, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_cursor_word_left_no_words() {
    let (mut ime, statechan, _actionchan) = set_up("¿ - _ - ?", 5, 5).await;

    // left with control
    simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_CONTROL).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_cursor_word_left() {
    let (mut ime, statechan, _actionchan) = set_up("4.2 . foobar", 7, 7).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_CONTROL).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!(6, state.selection.base);
    assert_eq!(6, state.selection.extent);

    simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_CONTROL).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(3, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_cursor_word_right_no_words() {
    let text = "¿ - _ - ?";
    let text_len = measure_utf16(text) as i64;
    let (mut ime, statechan, _actionchan) = set_up(text, 5, 5).await;

    // right with control
    simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_CONTROL).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!(text_len, state.selection.base);
    assert_eq!(text_len, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_cursor_word_right() {
    let (mut ime, statechan, _actionchan) = set_up("4.2 . foobar", 1, 1).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_CONTROL).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!(3, state.selection.base);
    assert_eq!(3, state.selection.extent);

    simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_CONTROL).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(3, state.revision);
    assert_eq!(12, state.selection.base);
    assert_eq!(12, state.selection.extent);

    // Try to navigate off text limits.
    simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_CONTROL).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(4, state.revision);
    assert_eq!(12, state.selection.base);
    assert_eq!(12, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_cursor_word_off_limits() {
    let (mut ime, statechan, _actionchan) = set_up("word", 1, 1).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_CONTROL).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!(4, state.selection.base);
    assert_eq!(4, state.selection.extent);

    simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_CONTROL).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(3, state.revision);
    assert_eq!(4, state.selection.base);
    assert_eq!(4, state.selection.extent);

    simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_CONTROL).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(4, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);

    simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_CONTROL).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(5, state.revision);
    assert_eq!(0, state.selection.base);
    assert_eq!(0, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_cursor_word() {
    let start_idx = measure_utf16("a.c   2.") as i64;
    let (mut ime, statechan, _actionchan) = set_up("a.c   2.2 ¿? x yz", start_idx, start_idx).await;

    simulate_keypress(
        &mut ime,
        HID_USAGE_KEY_RIGHT,
        true,
        uii::MODIFIER_CONTROL | uii::MODIFIER_SHIFT,
    )
    .await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!(start_idx, state.selection.base);
    assert_eq!(measure_utf16("a.c   2.2") as i64, state.selection.extent);

    simulate_keypress(
        &mut ime,
        HID_USAGE_KEY_RIGHT,
        true,
        uii::MODIFIER_CONTROL | uii::MODIFIER_SHIFT,
    )
    .await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(3, state.revision);
    assert_eq!(start_idx, state.selection.base);
    assert_eq!(measure_utf16("a.c   2.2 ¿? x") as i64, state.selection.extent);

    simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_CONTROL).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(4, state.revision);
    assert_eq!(measure_utf16("a.c   ") as i64, state.selection.base);
    assert_eq!(measure_utf16("a.c   ") as i64, state.selection.extent);

    simulate_keypress(
        &mut ime,
        HID_USAGE_KEY_LEFT,
        true,
        uii::MODIFIER_CONTROL | uii::MODIFIER_SHIFT,
    )
    .await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(5, state.revision);
    assert_eq!(measure_utf16("a.c   ") as i64, state.selection.base);
    assert_eq!(0, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_type_empty_string() {
    let (mut ime, statechan, _actionchan) = set_up("", 0, 0).await;

    simulate_keypress(&mut ime, 'a', false, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("a", state.text);
    assert_eq!(1, state.selection.base);
    assert_eq!(1, state.selection.extent);

    simulate_keypress(&mut ime, 'b', false, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(3, state.revision);
    assert_eq!("ab", state.text);
    assert_eq!(2, state.selection.base);
    assert_eq!(2, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_type_at_beginning() {
    let (mut ime, statechan, _actionchan) = set_up("cde", 0, 0).await;

    simulate_keypress(&mut ime, 'a', false, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("acde", state.text);
    assert_eq!(1, state.selection.base);
    assert_eq!(1, state.selection.extent);

    simulate_keypress(&mut ime, 'b', false, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(3, state.revision);
    assert_eq!("abcde", state.text);
    assert_eq!(2, state.selection.base);
    assert_eq!(2, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_type_selection() {
    let (mut ime, statechan, _actionchan) = set_up("abcdef", 2, 5).await;

    simulate_keypress(&mut ime, 'x', false, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("abxf", state.text);
    assert_eq!(3, state.selection.base);
    assert_eq!(3, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_type_inverted_selection() {
    let (mut ime, statechan, _actionchan) = set_up("abcdef", 5, 2).await;

    simulate_keypress(&mut ime, 'x', false, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("abxf", state.text);
    assert_eq!(3, state.selection.base);
    assert_eq!(3, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_type_invalid_selection() {
    let (mut ime, statechan, _actionchan) = set_up("abcdef", -10, 1).await;

    simulate_keypress(&mut ime, 'x', false, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("xbcdef", state.text);
    assert_eq!(1, state.selection.base);
    assert_eq!(1, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_set_state() {
    let (mut ime, statechan, _actionchan) = set_up("abcdef", 1, 1).await;

    let mut override_state = default_state();
    override_state.text = "meow?".to_string();
    override_state.selection.base = 4;
    override_state.selection.extent = 5;
    ime.set_state(override_state).await;
    simulate_keypress(&mut ime, '!', false, uii::MODIFIER_NONE).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("meow!", state.text);
    assert_eq!(5, state.selection.base);
    assert_eq!(5, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_action() {
    let (mut ime, statechan, actionchan) = set_up("abcdef", 1, 1).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_ENTER, true, uii::MODIFIER_NONE).await;
    assert!(statechan.try_recv().is_err()); // assert did not update state
    assert!(actionchan.try_recv().is_ok()); // assert DID send action
}

#[run_until_stalled(test)]
async fn test_unicode_selection() {
    let (mut ime, statechan, _actionchan) = set_up("m😸eow", 1, 1).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_SHIFT).await;
    assert!(statechan.try_recv().is_ok());

    simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_SHIFT).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(3, state.revision);
    assert_eq!("meow", state.text);
    assert_eq!(1, state.selection.base);
    assert_eq!(1, state.selection.extent);
}

#[run_until_stalled(test)]
async fn test_unicode_backspace() {
    let base: i64 = measure_utf16("m😸") as i64;
    let (mut ime, statechan, _actionchan) = set_up("m😸eow", base, base).await;

    simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_SHIFT).await;
    let state = statechan.try_recv().unwrap();
    assert_eq!(2, state.revision);
    assert_eq!("meow", state.text);
    assert_eq!(1, state.selection.base);
    assert_eq!(1, state.selection.extent);
}
