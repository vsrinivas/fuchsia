// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::{Client, EventQueue},
    crate::compositor::Surface,
    crate::object::{NewObjectExt, ObjectRef, ObjectRefSet, RequestReceiver},
    anyhow::Error,
    fuchsia_wayland_core as wl,
    fuchsia_zircon::{self as zx, HandleBased},
    wayland::{
        wl_keyboard, wl_seat, WlKeyboard, WlKeyboardEvent, WlKeyboardRequest, WlPointer,
        WlPointerRequest, WlSeat, WlSeatEvent, WlSeatRequest, WlTouch, WlTouchRequest,
    },
};

#[cfg(not(feature = "flatland"))]
use {
    fidl_fuchsia_ui_input::{
        FocusEvent, InputEvent, PointerEvent, PointerEventPhase, PointerEventType,
    },
    fidl_fuchsia_ui_input3::{KeyEvent, KeyEventType},
    fuchsia_trace as ftrace,
    std::collections::HashSet,
    wayland::{wl_pointer, wl_touch},
};

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

    pub fn post_seat_info(&self, this: wl::ObjectId, client: &mut Client) -> Result<(), Error> {
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
        client.event_queue().post(this, WlSeatEvent::Name { name: "unknown".to_string() })?;
        Ok(())
    }
}

pub struct InputDispatcher {
    // TODO(fxb/72068): Enable the 3 fields below for Flatland when we have proper
    // input upport.
    #[cfg(not(feature = "flatland"))]
    event_queue: EventQueue,
    #[cfg(not(feature = "flatland"))]
    pressed_keys: HashSet<fidl_fuchsia_input::Key>,
    #[cfg(not(feature = "flatland"))]
    modifiers: u32,
    /// The set of bound wl_pointer objects for this client.
    ///
    /// Note we're assuming a single wl_seat for now, so these all are pointers
    /// associated with that seat.
    pub pointers: ObjectRefSet<Pointer>,
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
}

#[cfg(not(feature = "flatland"))]
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

#[cfg(not(feature = "flatland"))]
fn pointer_trace_hack(fa: f32, fb: f32) -> u64 {
    let ia: u64 = fa.to_bits().into();
    let ib: u64 = fb.to_bits().into();
    ia << 32 | ib
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
    }
}

#[cfg(feature = "flatland")]
impl InputDispatcher {
    pub fn new(_event_queue: EventQueue) -> Self {
        Self {
            pointers: ObjectRefSet::new(),
            keyboards: ObjectRefSet::new(),
            touches: ObjectRefSet::new(),
            pointer_focus: None,
            keyboard_focus: None,
        }
    }
}

#[cfg(not(feature = "flatland"))]
impl InputDispatcher {
    pub fn new(event_queue: EventQueue) -> Self {
        Self {
            event_queue,
            pressed_keys: HashSet::new(),
            modifiers: 0,
            pointers: ObjectRefSet::new(),
            keyboards: ObjectRefSet::new(),
            touches: ObjectRefSet::new(),
            pointer_focus: None,
            keyboard_focus: None,
        }
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

    fn handle_focus_event(
        &mut self,
        surface: ObjectRef<Surface>,
        focus: &FocusEvent,
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::handle_focus_event");
        if focus.focused {
            if let Some(current_focus) = self.keyboard_focus {
                self.send_keyboard_leave(current_focus)?;
            }
            self.send_keyboard_enter(surface)?;
            self.send_keyboard_modifiers(0)?;
            self.keyboard_focus = Some(surface);
            self.pressed_keys.clear();
            self.modifiers = 0;
        } else if self.keyboard_focus == Some(surface) {
            // Send key leave on
            self.send_keyboard_leave(surface)?;
            self.keyboard_focus = None;
        }
        Ok(())
    }

    fn handle_mouse_event(
        &self,
        pointer: &PointerEvent,
        translation: (f32, f32),
        pixel_scale: (f32, f32),
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::handle_mouse_event");
        match pointer.phase {
            PointerEventPhase::Move => {
                self.pointers.iter().try_for_each(|p| {
                    self.event_queue.post(
                        p.id(),
                        wl_pointer::Event::Motion {
                            time: (pointer.event_time / 1_000_000) as u32,
                            surface_x: ((pointer.x + translation.0) * pixel_scale.0).into(),
                            surface_y: ((pointer.y + translation.1) * pixel_scale.1).into(),
                        },
                    )
                })?;
            }
            PointerEventPhase::Up | PointerEventPhase::Down => {
                let state = if pointer.phase == PointerEventPhase::Up {
                    wl_pointer::ButtonState::Released
                } else {
                    wl_pointer::ButtonState::Pressed
                };
                const BUTTON_MAP: &[(u32, u32)] = &[
                    (fidl_fuchsia_ui_input::MOUSE_BUTTON_PRIMARY, 0x110),
                    (fidl_fuchsia_ui_input::MOUSE_BUTTON_SECONDARY, 0x111),
                    (fidl_fuchsia_ui_input::MOUSE_BUTTON_TERTIARY, 0x112),
                ];
                for button in BUTTON_MAP {
                    if pointer.buttons & button.0 != 0 {
                        let serial = self.event_queue.next_serial();
                        self.pointers.iter().try_for_each(|p| {
                            self.event_queue.post(
                                p.id(),
                                wl_pointer::Event::Button {
                                    serial,
                                    time: (pointer.event_time / 1_000_000) as u32,
                                    button: button.1,
                                    state,
                                },
                            )
                        })?;
                    }
                }
            }
            _ => (),
        }
        Ok(())
    }

    fn handle_touch_event(
        &self,
        surface: ObjectRef<Surface>,
        touch: &PointerEvent,
        translation: (f32, f32),
        pixel_scale: (f32, f32),
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::handle_touch_event");
        match touch.phase {
            PointerEventPhase::Move => {
                self.touches.iter().try_for_each(|p| {
                    self.event_queue.post(
                        p.id(),
                        wl_touch::Event::Motion {
                            time: (touch.event_time / 1_000_000) as u32,
                            id: touch.pointer_id as i32,
                            x: ((touch.x + translation.0) * pixel_scale.0).into(),
                            y: ((touch.y + translation.1) * pixel_scale.1).into(),
                        },
                    )
                })?;
            }
            PointerEventPhase::Down => {
                let serial = self.event_queue.next_serial();
                self.touches.iter().try_for_each(|p| {
                    self.event_queue.post(
                        p.id(),
                        wl_touch::Event::Down {
                            serial,
                            time: (touch.event_time / 1_000_000) as u32,
                            surface: surface.id(),
                            id: touch.pointer_id as i32,
                            x: ((touch.x + translation.0) * pixel_scale.0).into(),
                            y: ((touch.y + translation.1) * pixel_scale.1).into(),
                        },
                    )
                })?;
            }
            PointerEventPhase::Up => {
                let serial = self.event_queue.next_serial();
                self.touches.iter().try_for_each(|p| {
                    self.event_queue.post(
                        p.id(),
                        wl_touch::Event::Up {
                            serial,
                            time: (touch.event_time / 1_000_000) as u32,
                            id: touch.pointer_id as i32,
                        },
                    )
                })?;
            }
            _ => (),
        }
        Ok(())
    }

    fn send_key_event(
        &self,
        key: fidl_fuchsia_input::Key,
        time: u32,
        state: wl_keyboard::KeyState,
    ) -> Result<(), Error> {
        let hid_usage = (key as u32) & 0xffff;
        ftrace::duration!("wayland", "InputDispatcher::send_key_event", "hid_usage" => hid_usage);
        let serial = self.event_queue.next_serial();
        self.keyboards.iter().try_for_each(|k| {
            self.event_queue
                .post(k.id(), wl_keyboard::Event::Key { serial, time, key: hid_usage, state })
        })
    }

    pub fn handle_key_event(
        &mut self,
        surface: ObjectRef<Surface>,
        event: &KeyEvent,
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::handle_key_event");
        // TODO(fxb/79741): Enable or remove this assert.
        // assert!(self.has_focus(surface), "Received key event without focus!");
        if self.has_focus(surface) {
            let key = event.key.unwrap();
            let time = (event.timestamp.unwrap() / 1_000_000) as u32;
            match event.type_.unwrap() {
                KeyEventType::Pressed => {
                    self.pressed_keys.insert(key);
                    self.send_key_event(key, time, wl_keyboard::KeyState::Pressed)?;
                }
                KeyEventType::Released => {
                    self.pressed_keys.remove(&key);
                    self.send_key_event(key, time, wl_keyboard::KeyState::Released)?;
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

    fn send_pointer_frame(&self) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::send_pointer_frame");
        self.pointers
            .iter()
            .try_for_each(|p| self.event_queue.post(p.id(), wl_pointer::Event::Frame))
    }

    fn send_touch_frame(&self) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::send_touch_frame");
        self.touches.iter().try_for_each(|p| self.event_queue.post(p.id(), wl_touch::Event::Frame))
    }

    fn send_pointer_enter(
        &self,
        surface: ObjectRef<Surface>,
        pointer: &PointerEvent,
        translation: (f32, f32),
        pixel_scale: (f32, f32),
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::send_pointer_enter");
        let serial = self.event_queue.next_serial();
        self.pointers.iter().try_for_each(|p| {
            self.event_queue.post(
                p.id(),
                wl_pointer::Event::Enter {
                    serial,
                    surface: surface.id(),
                    surface_x: ((pointer.x + translation.0) * pixel_scale.0).into(),
                    surface_y: ((pointer.y + translation.1) * pixel_scale.1).into(),
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
        pointer: &PointerEvent,
        translation: (f32, f32),
        pixel_scale: (f32, f32),
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
            // TODO: skip a motion event if we've updated focus (since it's in
            // the pointer event).
            self.send_pointer_enter(new_focus, pointer, translation, pixel_scale)?;
        }

        self.pointer_focus = new_focus;
        if needs_frame {
            self.send_pointer_frame()?;
        }
        Ok(())
    }

    /// Reads the set of Scenic `events` sent to a surface, converts them into
    /// as set of 0 or more wayland events, and sends those messages to the
    /// client.
    ///
    /// If a `pointer_translation` is provided, this value will be added to the
    /// pointer events. This can be used if the Scenic coordinates do not match
    /// the client coordinates (as is the case with xdg_surface's
    /// set_window_geometry request).
    ///
    /// The `pixel_scale` is also applied to transform the Scenic coordinate
    /// space into the pixel space exposed to the client.
    pub fn handle_input_events(
        &mut self,
        surface: ObjectRef<Surface>,
        events: &[InputEvent],
        pointer_translation: (i32, i32),
        pixel_scale: (f32, f32),
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "InputDispatcher::handle_input_events");
        let pointer_translation = (pointer_translation.0 as f32, pointer_translation.1 as f32);
        for event in events {
            match event {
                InputEvent::Pointer(pointer) if pointer.type_ == PointerEventType::Mouse => {
                    ftrace::flow_end!(
                        "input",
                        "dispatch_event_to_client",
                        pointer_trace_hack(pointer.radius_major, pointer.radius_minor)
                    );
                    // We send pointer focus here since pointer events will be
                    // delivered to whatever view the mouse cursor is over
                    // regardless of if a scenic Focus event has been sent.
                    self.update_pointer_focus(
                        Some(surface),
                        pointer,
                        pointer_translation,
                        pixel_scale,
                    )?;
                    self.handle_mouse_event(pointer, pointer_translation, pixel_scale)?;
                    self.send_pointer_frame()?;
                }
                InputEvent::Pointer(pointer) if pointer.type_ == PointerEventType::Touch => {
                    ftrace::flow_end!(
                        "input",
                        "dispatch_event_to_client",
                        pointer_trace_hack(pointer.radius_major, pointer.radius_minor)
                    );
                    self.handle_touch_event(surface, pointer, pointer_translation, pixel_scale)?;
                    self.send_touch_frame()?;
                }
                InputEvent::Focus(focus) => {
                    self.handle_focus_event(surface, focus)?;
                }
                // TODO: Implement these.
                InputEvent::Pointer(_pointer) => {}
                // Deprecated, keyboard used fuchsia.ui.input3 instead.
                InputEvent::Keyboard(_) => {}
            }
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
            }
            WlSeatRequest::GetKeyboard { id } => {
                let keyboard = id.implement(client, Keyboard::new(this))?;
                assert!(client.input_dispatcher.keyboards.add(keyboard));
                let (vmo, len) = {
                    let this = this.get(client)?;
                    (this.keymap.duplicate_handle(zx::Rights::SAME_RIGHTS)?, this.keymap_len)
                };
                Keyboard::post_keymap(keyboard, client, vmo.into(), len)?;
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
                client.delete_id(this.id())?;
            }
            WlPointerRequest::SetCursor { .. } => {}
        }
        Ok(())
    }
}

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
            client
                .event_queue()
                .post(this.id(), WlKeyboardEvent::RepeatInfo { rate: 1, delay: 10000 })?;
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
