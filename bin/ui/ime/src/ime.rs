use async;
use fidl::encoding2::OutOfLine;
use fidl_fuchsia_ui_input as uii;
use fidl_fuchsia_ui_input::InputMethodEditorRequest as ImeReq;
use futures::future;
use futures::prelude::*;
use std::char;
use std::ops::Range;

// TODO(lard): move constants into common, centralized location?
const HID_USAGE_KEY_BACKSPACE: u32 = 0x2a;
const HID_USAGE_KEY_RIGHT: u32 = 0x4f;
const HID_USAGE_KEY_LEFT: u32 = 0x50;
const HID_USAGE_KEY_ENTER: u32 = 0x28;

pub struct IME<I: 'static + uii::InputMethodEditorClientProxyInterface> {
    state: uii::TextInputState,
    client: I,
    keyboard_type: uii::KeyboardType,
    action: uii::InputMethodAction,
}

impl<I: 'static + uii::InputMethodEditorClientProxyInterface> IME<I> {
    pub fn new(
        keyboard_type: uii::KeyboardType, action: uii::InputMethodAction,
        initial_state: uii::TextInputState, client: I,
    ) -> IME<I> {
        IME {
            state: initial_state,
            client: client,
            keyboard_type: keyboard_type,
            action: action,
        }
    }

    pub fn bind(mut self, edit_stream: uii::InputMethodEditorRequestStream) {
        let stream_complete = edit_stream
            .try_for_each(move |edit_request| {
                self.handle(edit_request);
                future::ready(Ok(()))
            })
            .unwrap_or_else(|e| eprintln!("error running ime server: {:?}", e));
        async::spawn(stream_complete);
    }

    pub fn handle(&mut self, edit_request: ImeReq) {
        match edit_request {
            ImeReq::SetKeyboardType { keyboard_type, .. } => {
                self.keyboard_type = keyboard_type;
            }
            ImeReq::SetState { state, .. } => {
                self.set_state(state);
            }
            ImeReq::InjectInput { event, .. } => {
                self.inject_input(event);
            }
            ImeReq::Show { .. } => {
                // noop
            }
            ImeReq::Hide { .. } => {
                // noop
            }
        };
    }

    fn set_state(&mut self, state: uii::TextInputState) {
        self.state = state;
        // the old C++ IME implementation didn't call did_update_state here, so we won't either.
    }

    fn did_update_state(&mut self, e: uii::KeyboardEvent) {
        self.client
            .did_update_state(
                &mut self.state,
                Some(OutOfLine(&mut uii::InputEvent::Keyboard(e))),
            )
            .expect("IME service failed when attempting to notify IMEClient of updated state");
    }

    // gets start and len, and sets base/extent to start of string if don't exist
    fn selection(&mut self) -> Range<usize> {
        let s = &mut self.state.selection;
        s.base = s.base.max(0).min(self.state.text.len() as i64);
        s.extent = s.extent.max(0).min(self.state.text.len() as i64);
        let start = s.base.min(s.extent) as usize;
        let end = s.base.max(s.extent) as usize;
        (start..end)
    }

    fn inject_input(&mut self, event: uii::InputEvent) {
        let keyboard_event = match event {
            uii::InputEvent::Keyboard(e) => e,
            _ => return,
        };

        if keyboard_event.phase == uii::KeyboardEventPhase::Pressed
            || keyboard_event.phase == uii::KeyboardEventPhase::Repeat
        {
            if keyboard_event.code_point != 0 {
                self.type_keycode(keyboard_event.code_point);
                self.did_update_state(keyboard_event)
            } else {
                match keyboard_event.hid_usage {
                    HID_USAGE_KEY_BACKSPACE => {
                        self.delete_backward();
                        self.did_update_state(keyboard_event);
                    }
                    HID_USAGE_KEY_LEFT => {
                        self.cursor_horizontal_move(keyboard_event.modifiers, false);
                        self.did_update_state(keyboard_event);
                    }
                    HID_USAGE_KEY_RIGHT => {
                        self.cursor_horizontal_move(keyboard_event.modifiers, true);
                        self.did_update_state(keyboard_event);
                    }
                    HID_USAGE_KEY_ENTER => {
                        self.client
                            .on_action(self.action)
                            .expect("IME service failed when calling IMEClient action");
                    }
                    // we're ignoring many editing keys right now, this is where they would
                    // be added
                    _ => {
                        // not an editing key we recognize, so do nothing
                        ()
                    }
                }
            }
        }
    }

    fn type_keycode(&mut self, code_point: u32) {
        self.state.revision += 1;

        let replacement = match char::from_u32(code_point) {
            Some(v) => v.to_string(),
            None => return,
        };

        let selection = self.selection();
        self.state
            .text
            .replace_range(selection.clone(), &replacement);

        self.state.selection.base = selection.start as i64 + replacement.len() as i64;
        self.state.selection.extent = self.state.selection.base;
    }

    fn delete_backward(&mut self) {
        self.state.revision += 1;

        // set base and extent to 0 if either is -1, to ensure there is a selection/cursor
        self.selection();

        if self.state.selection.base == self.state.selection.extent {
            if self.state.selection.base > 0 {
                // Change cursor to 1-char selection, so that it can be uniformly handled
                // by the selection-deletion code below.
                self.state.selection.base -= 1;
            } else {
                // Cursor is at beginning of text; there is nothing previous to delete.
                return;
            }
        }

        // Delete the current selection.
        let selection = self.selection();
        self.state.text.replace_range(selection.clone(), "");
        self.state.selection.extent = selection.start as i64;
        self.state.selection.base = self.state.selection.extent;
    }

    fn cursor_horizontal_move(&mut self, modifiers: u32, go_right: bool) {
        self.state.revision += 1;

        let shift_pressed = modifiers & uii::MODIFIER_SHIFT != 0;
        let selection = self.selection();
        let text_is_selected = selection.start != selection.end;
        let mut new_position = self.state.selection.extent;

        if !shift_pressed && text_is_selected {
            // canceling selection, new position based on start/end of selection
            if go_right {
                new_position = selection.end as i64;
            } else {
                new_position = selection.start as i64;
            }
        } else {
            // new position based previous value of extent
            if go_right {
                new_position += 1
            } else {
                new_position -= 1
            }
            new_position = new_position.max(0).min(self.state.text.len() as i64);
        }

        self.state.selection.extent = new_position;
        if !shift_pressed {
            self.state.selection.base = new_position;
        }
        self.state.selection.affinity = uii::TextAffinity::Downstream;
    }
}

pub fn default_state() -> uii::TextInputState {
    uii::TextInputState {
        revision: 1,
        text: "".to_string(),
        selection: uii::TextSelection {
            base: -1,
            extent: -1,
            affinity: uii::TextAffinity::Upstream,
        },
        composing: uii::TextRange { start: -1, end: -1 },
    }
}

pub fn clone_state(state: &uii::TextInputState) -> uii::TextInputState {
    uii::TextInputState {
        revision: state.revision,
        text: state.text.clone(),
        selection: uii::TextSelection {
            base: state.selection.base,
            extent: state.selection.extent,
            affinity: state.selection.affinity,
        },
        composing: uii::TextRange {
            start: state.composing.start,
            end: state.composing.end,
        },
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl;
    use std::sync::mpsc::{channel, Receiver, Sender};
    use std::sync::Mutex;
    use std::time::Duration;

    fn set_up(
        text: &str, base: i64, extent: i64,
    ) -> (
        IME<MockImeClient>,
        Receiver<uii::TextInputState>,
        Receiver<uii::InputMethodAction>,
    ) {
        let (client, statechan, actionchan) = MockImeClient::new();
        let mut state = default_state();
        state.text = text.to_string();
        state.selection.base = base;
        state.selection.extent = extent;
        let ime = IME::new(
            uii::KeyboardType::Text,
            uii::InputMethodAction::Search,
            state,
            client,
        );
        (ime, statechan, actionchan)
    }

    fn simulate_keypress<K: Into<u32> + Copy>(
        ime: &mut IME<MockImeClient>, key: K, hid_key: bool, shift_pressed: bool,
    ) {
        let hid_usage =
            if hid_key { key.into() } else { 0 };
        let code_point =
            if hid_key { 0 } else { key.into() };
        ime.inject_input(uii::InputEvent::Keyboard(uii::KeyboardEvent {
            event_time: 0,
            device_id: 0,
            phase: uii::KeyboardEventPhase::Pressed,
            hid_usage: hid_usage,
            code_point: code_point,
            modifiers: if shift_pressed {
                uii::MODIFIER_SHIFT
            } else {
                0
            },
        }));
        ime.inject_input(uii::InputEvent::Keyboard(uii::KeyboardEvent {
            event_time: 0,
            device_id: 0,
            phase: uii::KeyboardEventPhase::Released,
            hid_usage: hid_usage,
            code_point: code_point,
            modifiers: if shift_pressed {
                uii::MODIFIER_SHIFT
            } else {
                0
            },
        }));
    }

    struct MockImeClient {
        pub state: Mutex<Sender<uii::TextInputState>>,
        pub action: Mutex<Sender<uii::InputMethodAction>>,
    }
    impl MockImeClient {
        fn new() -> (
            MockImeClient,
            Receiver<uii::TextInputState>,
            Receiver<uii::InputMethodAction>,
        ) {
            let (s_send, s_rec) = channel();
            let (a_send, a_rec) = channel();
            let client = MockImeClient {
                state: Mutex::new(s_send),
                action: Mutex::new(a_send),
            };
            (client, s_rec, a_rec)
        }
    }
    impl uii::InputMethodEditorClientProxyInterface for MockImeClient {
        fn did_update_state(
            &self, state: &mut uii::TextInputState,
            mut _event: Option<fidl::encoding2::OutOfLine<uii::InputEvent>>,
        ) -> Result<(), fidl::Error> {
            let state2 = clone_state(state);
            self.state.lock().unwrap().send(state2).unwrap();
            Ok(())
        }
        fn on_action(&self, action: uii::InputMethodAction) -> Result<(), fidl::Error> {
            self.action.lock().unwrap().send(action).unwrap();
            Ok(())
        }
    }

    #[test]
    fn test_mock_ime_channels() {
        let (client, statechan, actionchan) = MockImeClient::new();
        let mut ime = IME::new(
            uii::KeyboardType::Text,
            uii::InputMethodAction::Search,
            default_state(),
            client,
        );
        assert_eq!(true, statechan.try_recv().is_err());
        assert_eq!(true, actionchan.try_recv().is_err());
        simulate_keypress(&mut ime, 'a', false, false);
        assert_eq!(false, statechan.try_recv().is_err());
        assert_eq!(true, actionchan.try_recv().is_err());
    }

    #[test]
    fn test_delete_backward_empty_string() {
        let (mut ime, statechan, _actionchan) = set_up("", -1, -1);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);

        // a second delete still does nothing, but increments revision
        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_delete_backward_beginning_string() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 0, 0);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefghi", state.text);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_delete_first_char_selected() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 0, 1);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("bcdefghi", state.text);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_delete_backward_end_string() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 9, 9);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefgh", state.text);
        assert_eq!(8, state.selection.base);
        assert_eq!(8, state.selection.extent);
    }

    #[test]
    fn test_delete_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 3, 6);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcghi", state.text);
        assert_eq!(3, state.selection.base);
        assert_eq!(3, state.selection.extent);
    }

    #[test]
    fn test_delete_selection_inverted() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 6, 3);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcghi", state.text);
        assert_eq!(3, state.selection.base);
        assert_eq!(3, state.selection.extent);
    }

    #[test]
    fn test_delete_no_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", -1, -1);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefghi", state.text);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_delete_with_zero_width_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 3, 3);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abdefghi", state.text);
        assert_eq!(2, state.selection.base);
        assert_eq!(2, state.selection.extent);
    }

    #[test]
    fn test_delete_with_zero_width_selection_at_end() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 9, 9);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefgh", state.text);
        assert_eq!(8, state.selection.base);
        assert_eq!(8, state.selection.extent);
    }

    #[test]
    fn test_delete_selection_out_of_bounds() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 20, 24);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefgh", state.text);
        assert_eq!(8, state.selection.base);
        assert_eq!(8, state.selection.extent);
    }

    #[test]
    fn test_cursor_left_on_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 1, 5);

        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, true); // right with shift
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!(1, state.selection.base);
        assert_eq!(6, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!(1, state.selection.base);
        assert_eq!(1, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(4, state.revision);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(5, state.revision);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_cursor_left_on_inverted_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 6, 3);

        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!(3, state.selection.base);
        assert_eq!(3, state.selection.extent);
    }

    #[test]
    fn test_cursor_right_on_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 3, 9);

        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, true); // left with shift
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!(3, state.selection.base);
        assert_eq!(8, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!(8, state.selection.base);
        assert_eq!(8, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(4, state.revision);
        assert_eq!(9, state.selection.base);
        assert_eq!(9, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(5, state.revision);
        assert_eq!(9, state.selection.base);
        assert_eq!(9, state.selection.extent);
    }

    #[test]
    fn test_type_empty_string() {
        let (mut ime, statechan, _actionchan) = set_up("", 0, 0);

        simulate_keypress(&mut ime, 'a', false, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("a", state.text);
        assert_eq!(1, state.selection.base);
        assert_eq!(1, state.selection.extent);

        simulate_keypress(&mut ime, 'b', false, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!("ab", state.text);
        assert_eq!(2, state.selection.base);
        assert_eq!(2, state.selection.extent);
    }

    #[test]
    fn test_type_at_beginning() {
        let (mut ime, statechan, _actionchan) = set_up("cde", 0, 0);

        simulate_keypress(&mut ime, 'a', false, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("acde", state.text);
        assert_eq!(1, state.selection.base);
        assert_eq!(1, state.selection.extent);

        simulate_keypress(&mut ime, 'b', false, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!("abcde", state.text);
        assert_eq!(2, state.selection.base);
        assert_eq!(2, state.selection.extent);
    }

    #[test]
    fn test_type_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdef", 2, 5);

        simulate_keypress(&mut ime, 'x', false, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abxf", state.text);
        assert_eq!(3, state.selection.base);
        assert_eq!(3, state.selection.extent);
    }

    #[test]
    fn test_type_inverted_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdef", 5, 2);

        simulate_keypress(&mut ime, 'x', false, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abxf", state.text);
        assert_eq!(3, state.selection.base);
        assert_eq!(3, state.selection.extent);
    }

    #[test]
    fn test_type_invalid_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdef", -10, 1);

        simulate_keypress(&mut ime, 'x', false, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("xbcdef", state.text);
        assert_eq!(1, state.selection.base);
        assert_eq!(1, state.selection.extent);
    }

    #[test]
    fn test_set_state() {
        let (mut ime, statechan, _actionchan) = set_up("abcdef", 1, 1);

        let mut override_state = default_state();
        override_state.text = "meow?".to_string();
        override_state.selection.base = 4;
        override_state.selection.extent = 5;
        ime.set_state(override_state);
        simulate_keypress(&mut ime, '!', false, false);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("meow!", state.text);
        assert_eq!(5, state.selection.base);
        assert_eq!(5, state.selection.extent);
    }

    #[test]
    fn test_action() {
        let (mut ime, statechan, actionchan) = set_up("abcdef", 1, 1);

        simulate_keypress(&mut ime, HID_USAGE_KEY_ENTER, true, false);
        assert!(statechan.try_recv().is_err()); // assert did not update state
        assert!(actionchan.try_recv().is_ok()); // assert DID send action
    }

    // TODO(lard): fix unicode support so this passes
    // disabled: #[test]
    fn test_unicode_selection() {
        let (mut ime, statechan, actionchan) = set_up("m😸eow", 1, 1);

        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, true);
        assert!(statechan.try_recv().is_ok());

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, true);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("meow", state.text);
        assert_eq!(1, state.selection.base);
        assert_eq!(1, state.selection.extent);
    }

    // TODO(lard): fix unicode support so this passes
    // disabled: #[test]
    fn test_unicode_backspace() {
        let (mut ime, statechan, actionchan) = set_up("m😸eow", 2, 2);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, true);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("meow", state.text);
        assert_eq!(1, state.selection.base);
        assert_eq!(1, state.selection.extent);
    }
}
