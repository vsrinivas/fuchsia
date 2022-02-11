// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::{Client, EventQueue},
    crate::compositor::Surface,
    crate::object::{NewObjectExt, ObjectRef, ObjectRefSet, RequestReceiver},
    crate::relative_pointer::RelativePointer,
    anyhow::Error,
    fidl_fuchsia_ui_input3::{KeyEvent, KeyEventType},
    fidl_fuchsia_ui_pointer::EventPhase,
    fuchsia_trace as ftrace, fuchsia_wayland_core as wl,
    fuchsia_zircon::{self as zx, HandleBased},
    std::collections::BTreeSet,
    std::collections::HashSet,
    wayland_server_protocol::{
        wl_keyboard, wl_pointer, wl_seat, wl_touch, WlKeyboard, WlKeyboardEvent, WlKeyboardRequest,
        WlPointer, WlPointerRequest, WlSeat, WlSeatEvent, WlSeatRequest, WlTouch, WlTouchRequest,
    },
};

pub fn usb_to_linux_keycode(usb_keycode: u32) -> u16 {
    match usb_keycode {
        458756 => 30,  // A
        458757 => 48,  // B
        458758 => 46,  // C
        458759 => 32,  // D
        458760 => 18,  // E
        458761 => 33,  // F
        458762 => 34,  // G
        458763 => 35,  // H
        458764 => 23,  // I
        458765 => 36,  // J
        458766 => 37,  // K
        458767 => 38,  // L
        458768 => 50,  // M
        458769 => 49,  // N
        458770 => 24,  // O
        458771 => 25,  // P
        458772 => 16,  // Q
        458773 => 19,  // R
        458774 => 31,  // S
        458775 => 20,  // T
        458776 => 22,  // U
        458777 => 47,  // V
        458778 => 17,  // W
        458779 => 45,  // X
        458780 => 21,  // Y
        458781 => 44,  // Z
        458782 => 2,   // 1
        458783 => 3,   // 2
        458784 => 4,   // 3
        458785 => 5,   // 4
        458786 => 6,   // 5
        458787 => 7,   // 6
        458788 => 8,   // 7
        458789 => 9,   // 8
        458790 => 10,  // 9
        458791 => 11,  // 0
        458792 => 28,  // ENTER
        458793 => 1,   // ESCAPE
        458794 => 14,  // BACKSPACE
        458795 => 15,  // TAB
        458796 => 57,  // SPACE
        458797 => 12,  // MINUS
        458798 => 13,  // EQUAL
        458799 => 26,  // LEFTBRACE
        458800 => 27,  // RIGHTBRACE
        458801 => 43,  // BACKSLASH
        458802 => 0,   // NON_US_HASH?
        458803 => 39,  // SEMICOLON
        458804 => 40,  // APOSTROPHE
        458805 => 41,  // GRAVE
        458806 => 51,  // COMMA
        458807 => 52,  // DOT
        458808 => 53,  // SLASH
        458809 => 58,  // CAPS_LOCK
        458810 => 59,  // F1
        458811 => 60,  // F2
        458812 => 61,  // F3
        458813 => 62,  // F4
        458814 => 63,  // F5
        458815 => 64,  // F6
        458816 => 65,  // F7
        458817 => 66,  // F8
        458818 => 67,  // F9
        458819 => 68,  // F10
        458820 => 87,  // F11
        458821 => 88,  // F12
        458822 => 210, // PRINT_SCREEN
        458823 => 70,  // SCROLL_LOCK
        458824 => 119, // PAUSE
        458825 => 110, // INSERT
        458826 => 102, // HOME
        458827 => 104, // PAGE_UP
        458828 => 111, // DELETE
        458829 => 107, // END
        458830 => 109, // PAGE_DOWN
        458831 => 106, // RIGHT
        458832 => 105, // LEFT
        458833 => 108, // DOWN
        458834 => 103, // UP
        458835 => 69,  // NUM_LOCK
        458836 => 98,  // KEYPAD_SLASH
        458837 => 55,  // KEYPAD_ASTERISK
        458838 => 74,  // KEYPAD_MINUS
        458839 => 78,  // KEYPAD_PLUS
        458840 => 96,  // KEYPAD_ENTER
        458841 => 79,  // KEYPAD_1
        458842 => 80,  // KEYPAD_2
        458843 => 81,  // KEYPAD_3
        458844 => 75,  // KEYPAD_4
        458845 => 76,  // KEYPAD_5
        458846 => 77,  // KEYPAD_6
        458847 => 71,  // KEYPAD_7
        458848 => 72,  // KEYPAD_8
        458849 => 73,  // KEYPAD_9
        458850 => 82,  // KEYPAD_0
        458851 => 83,  // KEYPAD_DOT
        458852 => 0,   // NON_US_BACKSLASH?
        458855 => 117, // KEYPAD_EQUALS
        458870 => 139, // MENU
        458976 => 29,  // LEFT_CTRL
        458977 => 42,  // LEFT_SHIFT
        458978 => 56,  // LEFT_ALT
        458979 => 125, // LEFT_META
        458980 => 97,  // RIGHT_CTRL
        458981 => 54,  // RIGHT_SHIFT
        458982 => 100, // RIGHT_ALT
        458983 => 126, // RIGHT_META
        786658 => 113, // MEDIA_MUTE
        786665 => 115, // MEDIA_VOLUME_INCREMENT
        786666 => 114, // MEDIA_VOLUME_DECREMENT

        _ => 0,
    }
}

/// An implementation of the wl_seat global.
pub struct Seat {
    client_version: u32,
    keymap: zx::Vmo,
    keymap_len: u32,
}

impl Seat {
    /// Creates a new `Seat`.
    pub fn new(client_version: u32, keymap: zx::Vmo, keymap_len: u32) -> Self {
        Seat { client_version, keymap, keymap_len }
    }

    pub fn post_seat_info(
        &self,
        this: wl::ObjectId,
        client_version: u32,
        client: &mut Client,
    ) -> Result<(), Error> {
        // TODO(tjdetwiler): Ideally we can source capabilities from scenic.
        // For now we'll report we can support all input types that scenic
        // supports.
        client.event_queue().post(
            this,
            WlSeatEvent::Capabilities {
                capabilities: wl_seat::Capability::Pointer
                    | wl_seat::Capability::Keyboard
                    | wl_seat::Capability::Touch,
            },
        )?;
        if client_version >= 2 {
            client.event_queue().post(this, WlSeatEvent::Name { name: "unknown".to_string() })?;
        }
        Ok(())
    }
}

pub struct InputDispatcher {
    event_queue: EventQueue,
    pressed_keys: HashSet<fidl_fuchsia_input::Key>,
    modifiers: u32,
    pressed_buttons: BTreeSet<u8>,
    pointer_position: [f32; 2],
    /// The set of bound wl_pointer objects for this client.
    ///
    /// Note we're assuming a single wl_seat for now, so these all are pointers
    /// associated with that seat.
    pub pointers: ObjectRefSet<Pointer>,
    pub v5_pointers: ObjectRefSet<Pointer>,
    /// The set of bound zwp_relative_pointer objects for this client.
    ///
    /// Note we're assuming a single wl_seat for now, so these all are relative
    /// pointers associated with that seat.
    pub relative_pointers: ObjectRefSet<RelativePointer>,
    /// The set of bound wl_pointer objects for this client.
    ///
    /// Note we're assuming a single wl_seat for now, so these all are keyboards
    /// associated with that seat.
    pub keyboards: ObjectRefSet<Keyboard>,
    /// The set of bound wl_pointer objects for this client.
    ///
    /// Note we're assuming a single wl_seat for now, so these all are touches
    /// associated with that seat.
    pub touches: ObjectRefSet<Touch>,

    /// The current surface that has been advertised to have pointer focus.
    pub pointer_focus: Option<ObjectRef<Surface>>,

    /// The current surface that has been advertised to have keyboard focus.
    pub keyboard_focus: Option<ObjectRef<Surface>>,

    /// The current surface that is the source for incoming keyboard events.
    /// This is the surface associated with the view that the native
    /// input pipeline has given focus. This is different from the above
    /// field as it will not change when we determine that a child view
    /// should have focus.
    pub keyboard_focus_source: Option<ObjectRef<Surface>>,
}

fn modifiers_from_pressed_keys(pressed_keys: &HashSet<fidl_fuchsia_input::Key>) -> u32 {
    // XKB mod masks for the default keymap.
    const SHIFT_MASK: u32 = 1 << 0;
    const CONTROL_MASK: u32 = 1 << 2;
    const ALT_MASK: u32 = 1 << 3;

    let mut modifiers = 0;
    if pressed_keys.contains(&fidl_fuchsia_input::Key::LeftShift)
        || pressed_keys.contains(&fidl_fuchsia_input::Key::RightShift)
    {
        modifiers |= SHIFT_MASK;
    }
    if pressed_keys.contains(&fidl_fuchsia_input::Key::LeftAlt)
        || pressed_keys.contains(&fidl_fuchsia_input::Key::RightAlt)
    {
        modifiers |= ALT_MASK;
    }
    if pressed_keys.contains(&fidl_fuchsia_input::Key::LeftCtrl)
        || pressed_keys.contains(&fidl_fuchsia_input::Key::RightCtrl)
    {
        modifiers |= CONTROL_MASK;
    }

    modifiers
}

impl InputDispatcher {
    /// Returns true iff the given surface currently has keyboard focus.
    pub fn has_focus(&self, surface_ref: ObjectRef<Surface>) -> bool {
        self.keyboard_focus == Some(surface_ref)
    }

    /// Updates focus state in response to a surface being unmapped.
    ///
    /// Since a new surface may be created with the same object id as `surface`,
    /// we need to ensure our current focus is cleared so we can send new focus
    /// enter events for the new surface. This does not send focus leave
    /// events as these are implicit with the surface being destroyed.
    ///
    /// This has no effect if the surface being destroyed has no focus.
    pub fn clear_focus_on_surface_destroy(&mut self, surface: ObjectRef<Surface>) {
        if Some(surface) == self.pointer_focus {
            self.pointer_focus = None;
        }
        if Some(surface) == self.keyboard_focus {
            self.keyboard_focus = None;
        }
        if Some(surface) == self.keyboard_focus_source {
            self.keyboard_focus_source = None;
        }
    }
}

impl InputDispatcher {
    pub fn new(event_queue: EventQueue) -> Self {
        Self {
            event_queue,
            pressed_keys: HashSet::new(),
            modifiers: 0,
            pressed_buttons: BTreeSet::new(),
            pointer_position: [-1.0, -1.0],
            pointers: ObjectRefSet::new(),
            v5_pointers: ObjectRefSet::new(),
            relative_pointers: ObjectRefSet::new(),
            keyboards: ObjectRefSet::new(),
            touches: ObjectRefSet::new(),
            pointer_focus: None,
            keyboard_focus: None,
            keyboard_focus_source: None,
        }
    }

    fn add_keyboard_and_send_focus(&mut self, keyboard: ObjectRef<Keyboard>) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::add_keyboard_and_send_focus");
        if let Some(focus) = self.keyboard_focus {
            let serial = self.event_queue.next_serial();
            self.event_queue.post(
                keyboard.id(),
                wl_keyboard::Event::Enter { serial, surface: focus.id(), keys: wl::Array::new() },
            )?;
            self.event_queue.post(
                keyboard.id(),
                wl_keyboard::Event::Modifiers {
                    serial,
                    mods_depressed: self.modifiers,
                    mods_latched: 0,
                    mods_locked: 0,
                    group: 0,
                },
            )?;
        }
        assert!(self.keyboards.add(keyboard));
        Ok(())
    }

    fn send_keyboard_enter(&self, surface: ObjectRef<Surface>) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::send_keyboard_enter");
        let serial = self.event_queue.next_serial();
        self.keyboards.iter().try_for_each(|k| {
            self.event_queue.post(
                k.id(),
                wl_keyboard::Event::Enter { serial, surface: surface.id(), keys: wl::Array::new() },
            )
        })
    }

    fn send_keyboard_modifiers(&self, modifiers: u32) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::send_keyboard_modifiers");
        let serial = self.event_queue.next_serial();
        self.keyboards.iter().try_for_each(|k| {
            self.event_queue.post(
                k.id(),
                wl_keyboard::Event::Modifiers {
                    serial,
                    mods_depressed: modifiers,
                    mods_latched: 0,
                    mods_locked: 0,
                    group: 0,
                },
            )
        })
    }

    fn send_keyboard_leave(&self, surface: ObjectRef<Surface>) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::send_keyboard_leave");
        let serial = self.event_queue.next_serial();
        self.keyboards.iter().try_for_each(|k| {
            self.event_queue
                .post(k.id(), wl_keyboard::Event::Leave { serial, surface: surface.id() })
        })
    }

    fn send_key_event(
        &self,
        key: fidl_fuchsia_input::Key,
        time: u32,
        state: wl_keyboard::KeyState,
    ) -> Result<(), Error> {
        // Map usb keycodes to Linux because some apps don't use the provided keymap/assume Linux keycodes
        let linux_keycode = usb_to_linux_keycode(key as u32);
        ftrace::duration!("wayland", "InputDispatcher::send_key_event", "linux_keycode" => linux_keycode as u32);
        let serial = self.event_queue.next_serial();
        self.keyboards.iter().try_for_each(|k| {
            self.event_queue.post(
                k.id(),
                wl_keyboard::Event::Key { serial, time, key: linux_keycode as u32, state },
            )
        })
    }

    pub fn handle_key_event(
        &mut self,
        source: ObjectRef<Surface>,
        event: &KeyEvent,
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::handle_key_event");
        if Some(source) == self.keyboard_focus_source && self.keyboard_focus.is_some() {
            let key = event.key.unwrap();
            let time_in_ms = (event.timestamp.unwrap() / 1_000_000) as u32;
            match event.type_.unwrap() {
                KeyEventType::Pressed if event.repeat_sequence.is_none() => {
                    self.pressed_keys.insert(key);
                    self.send_key_event(key, time_in_ms, wl_keyboard::KeyState::Pressed)?;
                }
                KeyEventType::Released => {
                    self.pressed_keys.remove(&key);
                    self.send_key_event(key, time_in_ms, wl_keyboard::KeyState::Released)?;
                }
                KeyEventType::Cancel => {
                    self.pressed_keys.remove(&key);
                }
                _ => (),
            }
            let modifiers = modifiers_from_pressed_keys(&self.pressed_keys);
            if modifiers != self.modifiers {
                self.send_keyboard_modifiers(modifiers)?;
                self.modifiers = modifiers;
            }
        }
        Ok(())
    }

    fn update_keyboard_focus(
        &mut self,
        new_focus: Option<ObjectRef<Surface>>,
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::update_keyboard_focus");
        if new_focus == self.keyboard_focus {
            return Ok(());
        }
        if let Some(current_focus) = self.keyboard_focus {
            self.send_keyboard_leave(current_focus)?;
        }
        if let Some(focus) = new_focus {
            self.send_keyboard_enter(focus)?;
            self.send_keyboard_modifiers(0)?;
            self.keyboard_focus = Some(focus);
            self.pressed_keys.clear();
            self.modifiers = 0;
        }
        self.keyboard_focus = new_focus;
        Ok(())
    }

    pub fn handle_keyboard_focus(
        &mut self,
        source: ObjectRef<Surface>,
        target: ObjectRef<Surface>,
        focused: bool,
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::handle_keyboard_focus");
        let keyboard_focus = if focused {
            self.keyboard_focus_source = Some(source);
            Some(target)
        } else {
            self.keyboard_focus_source = None;
            None
        };
        self.update_keyboard_focus(keyboard_focus)
    }

    pub fn maybe_update_keyboard_focus(
        &mut self,
        source: ObjectRef<Surface>,
        new_target: ObjectRef<Surface>,
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::maybe_update_keyboard_focus");
        if Some(source) == self.keyboard_focus_source {
            self.update_keyboard_focus(Some(new_target))?;
        }
        Ok(())
    }

    fn send_pointer_enter(&self, surface: ObjectRef<Surface>, x: f32, y: f32) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::send_pointer_enter");
        let serial = self.event_queue.next_serial();
        self.pointers.iter().try_for_each(|p| {
            self.event_queue.post(
                p.id(),
                wl_pointer::Event::Enter {
                    serial,
                    surface: surface.id(),
                    surface_x: x.into(),
                    surface_y: y.into(),
                },
            )
        })
    }

    fn send_pointer_leave(&self, surface: ObjectRef<Surface>) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::send_pointer_leave");
        let serial = self.event_queue.next_serial();
        self.pointers.iter().try_for_each(|p| {
            self.event_queue
                .post(p.id(), wl_pointer::Event::Leave { serial, surface: surface.id() })
        })
    }

    fn update_pointer_focus(
        &mut self,
        new_focus: Option<ObjectRef<Surface>>,
        position: &[f32; 2],
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::update_pointer_focus");
        if new_focus == self.pointer_focus {
            return Ok(());
        }

        let mut needs_frame = false;
        if let Some(current_focus) = self.pointer_focus {
            needs_frame = true;
            self.send_pointer_leave(current_focus)?;
        }

        if let Some(new_focus) = new_focus {
            needs_frame = true;
            self.send_pointer_enter(new_focus, position[0], position[1])?;
            self.pointer_position = *position;
        }

        self.pointer_focus = new_focus;
        if needs_frame {
            self.send_pointer_frame()?;
        }
        Ok(())
    }

    fn send_pointer_frame(&self) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::send_pointer_frame");
        self.v5_pointers
            .iter()
            .try_for_each(|p| self.event_queue.post(p.id(), wl_pointer::Event::Frame))
    }

    fn send_touch_frame(&self) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::send_touch_frame");
        self.touches.iter().try_for_each(|p| self.event_queue.post(p.id(), wl_touch::Event::Frame))
    }

    pub fn handle_pointer_event(
        &mut self,
        surface: ObjectRef<Surface>,
        timestamp: i64,
        position: &[f32; 2],
        pressed_buttons: &Option<Vec<u8>>,
        relative_motion: &Option<[f32; 2]>,
        scroll_v: &Option<i64>,
        scroll_h: &Option<i64>,
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::handle_pointer_event");
        let time_in_ms = (timestamp / 1_000_000) as u32;
        let mut needs_frame = false;

        self.update_pointer_focus(Some(surface), position)?;

        if position != &self.pointer_position {
            self.pointers.iter().try_for_each(|p| {
                self.event_queue.post(
                    p.id(),
                    wl_pointer::Event::Motion {
                        time: time_in_ms,
                        surface_x: position[0].into(),
                        surface_y: position[1].into(),
                    },
                )
            })?;
            self.pointer_position = *position;
            needs_frame = true;
        }

        fn send_button_event(
            event_queue: &EventQueue,
            pointers: &ObjectRefSet<Pointer>,
            time_in_ms: u32,
            button: u32,
            state: wl_pointer::ButtonState,
        ) -> Result<(), Error> {
            // This is base value for wayland button events. Button value
            // for button two is this plus 1.
            const BUTTON_BASE: u32 = 0x10f;

            let serial = event_queue.next_serial();
            pointers.iter().try_for_each(|p| {
                event_queue.post(
                    p.id(),
                    wl_pointer::Event::Button {
                        serial,
                        time: time_in_ms,
                        button: BUTTON_BASE + button,
                        state,
                    },
                )
            })
        }

        let pressed_buttons = pressed_buttons.as_ref().map_or(BTreeSet::new(), |pressed_buttons| {
            pressed_buttons.iter().map(|b| *b).collect()
        });
        for button in self.pressed_buttons.difference(&pressed_buttons) {
            send_button_event(
                &self.event_queue,
                &self.pointers,
                time_in_ms,
                *button as u32,
                wl_pointer::ButtonState::Released,
            )?;
            needs_frame = true;
        }
        for button in pressed_buttons.difference(&self.pressed_buttons) {
            send_button_event(
                &self.event_queue,
                &self.pointers,
                time_in_ms,
                *button as u32,
                wl_pointer::ButtonState::Pressed,
            )?;
            needs_frame = true;
        }
        self.pressed_buttons = pressed_buttons;

        fn send_axis_event(
            event_queue: &EventQueue,
            pointers: &ObjectRefSet<Pointer>,
            time_in_ms: u32,
            axis: wl_pointer::Axis,
            value: f32,
        ) -> Result<(), Error> {
            pointers.iter().try_for_each(|p| {
                event_queue.post(
                    p.id(),
                    wl_pointer::Event::Axis { time: time_in_ms, axis, value: value.into() },
                )
            })
        }

        if let Some(scroll_v) = scroll_v {
            send_axis_event(
                &self.event_queue,
                &self.pointers,
                time_in_ms,
                wl_pointer::Axis::VerticalScroll,
                *scroll_v as f32,
            )?;
            needs_frame = true;
        }
        if let Some(scroll_h) = scroll_h {
            send_axis_event(
                &self.event_queue,
                &self.pointers,
                time_in_ms,
                wl_pointer::Axis::HorizontalScroll,
                *scroll_h as f32,
            )?;
            needs_frame = true;
        }

        if let Some(relative_motion) = relative_motion {
            let time_in_us = (timestamp / 1_000) as u64;
            self.relative_pointers.iter().try_for_each(|rp| {
                RelativePointer::post_relative_motion(
                    *rp,
                    &self.event_queue,
                    time_in_us,
                    relative_motion[0].into(),
                    relative_motion[1].into(),
                )
            })?;
        }

        if needs_frame {
            self.send_pointer_frame()?;
        }

        Ok(())
    }

    pub fn handle_touch_event(
        &mut self,
        surface: ObjectRef<Surface>,
        timestamp: i64,
        id: i32,
        position: &[f32; 2],
        phase: EventPhase,
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::handle_touch_event");
        let time_in_ms = (timestamp / 1_000_000) as u32;
        let mut needs_frame = false;
        match phase {
            EventPhase::Change => {
                self.touches.iter().try_for_each(|t| {
                    self.event_queue.post(
                        t.id(),
                        wl_touch::Event::Motion {
                            time: time_in_ms,
                            id,
                            x: position[0].into(),
                            y: position[1].into(),
                        },
                    )
                })?;
                needs_frame = true;
            }
            EventPhase::Add => {
                let serial = self.event_queue.next_serial();
                self.touches.iter().try_for_each(|t| {
                    self.event_queue.post(
                        t.id(),
                        wl_touch::Event::Down {
                            serial,
                            time: time_in_ms,
                            surface: surface.id(),
                            id,
                            x: position[0].into(),
                            y: position[1].into(),
                        },
                    )
                })?;
                needs_frame = true;
            }
            EventPhase::Remove => {
                let serial = self.event_queue.next_serial();
                self.touches.iter().try_for_each(|t| {
                    self.event_queue
                        .post(t.id(), wl_touch::Event::Up { serial, time: time_in_ms, id })
                })?;
                needs_frame = true;
            }
            _ => (),
        }

        if needs_frame {
            self.send_touch_frame()?;
        }

        Ok(())
    }
}

impl RequestReceiver<WlSeat> for Seat {
    fn receive(
        this: ObjectRef<Self>,
        request: WlSeatRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            WlSeatRequest::Release => {
                client.delete_id(this.id())?;
            }
            WlSeatRequest::GetPointer { id } => {
                let pointer = id.implement(client, Pointer)?;
                // Assert here because if we successfully implemented the
                // interface the given id is valid. Any failure here indicates
                // a coherence issue between the client object map and the set
                // of bound pointers.
                assert!(client.input_dispatcher.pointers.add(pointer));
                if this.get(client)?.client_version >= 5 {
                    assert!(client.input_dispatcher.v5_pointers.add(pointer));
                }
            }
            WlSeatRequest::GetKeyboard { id } => {
                let keyboard = id.implement(client, Keyboard::new(this))?;
                let (vmo, len) = {
                    let this = this.get(client)?;
                    (this.keymap.duplicate_handle(zx::Rights::SAME_RIGHTS)?, this.keymap_len)
                };
                Keyboard::post_keymap(keyboard, client, vmo.into(), len)?;
                client.input_dispatcher.add_keyboard_and_send_focus(keyboard)?;
            }
            WlSeatRequest::GetTouch { id } => {
                let touch = id.implement(client, Touch)?;
                assert!(client.input_dispatcher.touches.add(touch));
            }
        }
        Ok(())
    }
}

pub struct Pointer;

impl RequestReceiver<WlPointer> for Pointer {
    fn receive(
        this: ObjectRef<Self>,
        request: WlPointerRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            WlPointerRequest::Release => {
                client.input_dispatcher.pointers.remove(this);
                client.input_dispatcher.v5_pointers.remove(this);
                client.delete_id(this.id())?;
            }
            WlPointerRequest::SetCursor { .. } => {}
        }
        Ok(())
    }
}

const KEY_REPEAT_RATE: i32 = 30;
const KEY_REPEAT_DELAY: i32 = 225;

pub struct Keyboard {
    seat: ObjectRef<Seat>,
}

impl Keyboard {
    pub fn new(seat: ObjectRef<Seat>) -> Self {
        Self { seat }
    }

    fn post_keymap(
        this: ObjectRef<Self>,
        client: &mut Client,
        keymap: zx::Handle,
        keymap_len: u32,
    ) -> Result<(), Error> {
        println!("Posting keymap with len {}", keymap_len);
        client.event_queue().post(
            this.id(),
            WlKeyboardEvent::Keymap {
                format: wl_keyboard::KeymapFormat::XkbV1,
                fd: keymap,
                size: keymap_len,
            },
        )?;
        if this.get(client)?.seat.get(client)?.client_version >= 4 {
            client.event_queue().post(
                this.id(),
                WlKeyboardEvent::RepeatInfo { rate: KEY_REPEAT_RATE, delay: KEY_REPEAT_DELAY },
            )?;
        }
        Ok(())
    }
}

impl RequestReceiver<WlKeyboard> for Keyboard {
    fn receive(
        this: ObjectRef<Self>,
        request: WlKeyboardRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        let WlKeyboardRequest::Release = request;
        client.input_dispatcher.keyboards.remove(this);
        client.delete_id(this.id())?;
        Ok(())
    }
}

pub struct Touch;

impl RequestReceiver<WlTouch> for Touch {
    fn receive(
        this: ObjectRef<Self>,
        request: WlTouchRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        let WlTouchRequest::Release = request;
        client.input_dispatcher.touches.remove(this);
        client.delete_id(this.id())?;
        Ok(())
    }
}
