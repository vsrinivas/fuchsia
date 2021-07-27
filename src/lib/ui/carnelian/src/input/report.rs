// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app::{
        strategies::framebuffer::{AutoRepeatContext, AutoRepeatTimer},
        Config,
    },
    drawing::DisplayRotation,
    geometry::LimitToBounds,
    input::{
        consumer_control, input3_key_to_hid_usage, keyboard, mouse, touch, Button, ButtonSet,
        DeviceId, Event, EventType, Modifiers,
    },
    IntPoint, IntRect, IntSize,
};
use euclid::{default::Transform2D, point2, size2, vec2};
use fidl_fuchsia_input_report as hid_input_report;
use keymaps::Keymap;
use std::{collections::HashSet, iter::FromIterator};

#[derive(Debug)]
pub struct TouchScale {
    pub target_size: IntSize,
    pub x: hid_input_report::Range,
    pub x_span: f32,
    pub y: hid_input_report::Range,
    pub y_span: f32,
}

fn restrict_to_range(value: i64, range: &hid_input_report::Range) -> i64 {
    if value < range.min {
        range.min
    } else if value > range.max {
        range.max
    } else {
        value
    }
}

fn scale_value(value: i64, span: f32, range: &hid_input_report::Range, value_max: i32) -> i32 {
    let value = restrict_to_range(value, range) - range.min;
    let value_fraction = value as f32 / span;
    (value_fraction * value_max as f32) as i32
}

impl TouchScale {
    fn calculate_span(range: &hid_input_report::Range) -> f32 {
        if range.max <= range.min {
            1.0
        } else {
            (range.max - range.min) as f32
        }
    }

    pub fn new(
        target_size: &IntSize,
        x: &hid_input_report::Range,
        y: &hid_input_report::Range,
    ) -> Self {
        Self {
            target_size: *target_size,
            x: *x,
            x_span: Self::calculate_span(x),
            y: *y,
            y_span: Self::calculate_span(y),
        }
    }

    pub fn scale(&self, pt: &IntPoint) -> IntPoint {
        let x = scale_value(pt.x as i64, self.x_span, &self.x, self.target_size.width);
        let y = scale_value(pt.y as i64, self.y_span, &self.y, self.target_size.height);
        point2(x, y)
    }
}

fn create_keyboard_event(
    event_time: u64,
    device_id: &DeviceId,
    phase: keyboard::Phase,
    key: fidl_fuchsia_input::Key,
    modifiers: &Modifiers,
    keymap: &Keymap<'_>,
) -> Event {
    let hid_usage = input3_key_to_hid_usage(key);
    let code_point =
        keymap.hid_usage_to_code_point_for_mods(hid_usage, modifiers.shift, modifiers.caps_lock);
    let keyboard_event = keyboard::Event { phase, code_point, hid_usage, modifiers: *modifiers };
    Event {
        event_time,
        device_id: device_id.clone(),
        event_type: EventType::Keyboard(keyboard_event),
    }
}

pub(crate) struct InputReportHandler<'a> {
    device_id: DeviceId,
    view_size: IntSize,
    display_rotation: DisplayRotation,
    touch_scale: Option<TouchScale>,
    keymap: &'a Keymap<'a>,
    repeating: Option<fidl_fuchsia_input::Key>,
    cursor_position: IntPoint,
    pressed_mouse_buttons: HashSet<u8>,
    pressed_keys: HashSet<fidl_fuchsia_input::Key>,
    raw_contacts: HashSet<touch::RawContact>,
    pressed_consumer_control_buttons: HashSet<hid_input_report::ConsumerControlButton>,
}

impl<'a> InputReportHandler<'a> {
    pub fn new(
        device_id: DeviceId,
        size: IntSize,
        display_rotation: DisplayRotation,
        device_descriptor: &hid_input_report::DeviceDescriptor,
        keymap: &'a Keymap<'a>,
    ) -> Self {
        let touch_scale = device_descriptor
            .touch
            .as_ref()
            .and_then(|touch| touch.input.as_ref())
            .and_then(|input_descriptor| input_descriptor.contacts.as_ref())
            .and_then(|contacts| contacts.first())
            .and_then(|contact_input_descriptor| {
                if contact_input_descriptor.position_x.is_some()
                    && contact_input_descriptor.position_y.is_some()
                {
                    Some(TouchScale::new(
                        &size,
                        &contact_input_descriptor.position_x.as_ref().expect("position_x").range,
                        &contact_input_descriptor.position_y.as_ref().expect("position_y").range,
                    ))
                } else {
                    None
                }
            });
        Self::new_with_scale(device_id, size, display_rotation, touch_scale, keymap)
    }

    pub fn new_with_scale(
        device_id: DeviceId,
        size: IntSize,
        display_rotation: DisplayRotation,
        touch_scale: Option<TouchScale>,
        keymap: &'a Keymap<'a>,
    ) -> Self {
        Self {
            device_id: device_id.clone(),
            view_size: size,
            display_rotation,
            keymap,
            repeating: None,
            touch_scale,
            cursor_position: IntPoint::zero(),
            pressed_mouse_buttons: HashSet::new(),
            pressed_keys: HashSet::new(),
            raw_contacts: HashSet::new(),
            pressed_consumer_control_buttons: HashSet::new(),
        }
    }

    fn handle_mouse_input_report(
        &mut self,
        event_time: u64,
        device_id: &DeviceId,
        mouse: &hid_input_report::MouseInputReport,
    ) -> Vec<Event> {
        fn create_mouse_event(
            event_time: u64,
            device_id: &DeviceId,
            button_set: &ButtonSet,
            cursor_position: IntPoint,
            transform: &Transform2D<f32>,
            phase: mouse::Phase,
        ) -> Event {
            let cursor_position = transform.transform_point(cursor_position.to_f32()).to_i32();
            let mouse_event =
                mouse::Event { buttons: button_set.clone(), phase, location: cursor_position };
            Event {
                event_time,
                device_id: device_id.clone(),
                event_type: EventType::Mouse(mouse_event),
            }
        }

        let transform = self.display_rotation.inv_transform(&self.view_size.to_f32());
        let new_cursor_position = self.cursor_position
            + vec2(mouse.movement_x.unwrap_or(0) as i32, mouse.movement_y.unwrap_or(0) as i32);
        let m = self.view_size;
        let bounds = IntRect::new(IntPoint::zero(), m);
        let new_cursor_position = bounds.limit_to_bounds(new_cursor_position);
        let pressed_buttons: HashSet<u8> = if let Some(ref pressed_buttons) = mouse.pressed_buttons
        {
            let pressed_buttons_set = pressed_buttons.iter().cloned().collect();
            pressed_buttons_set
        } else {
            HashSet::new()
        };

        let button_set = ButtonSet::new(&pressed_buttons);

        let move_event = if new_cursor_position != self.cursor_position {
            let event = create_mouse_event(
                event_time,
                device_id,
                &button_set,
                new_cursor_position,
                &transform,
                mouse::Phase::Moved,
            );
            Some(event)
        } else {
            None
        };

        self.cursor_position = new_cursor_position;

        let newly_pressed = pressed_buttons.difference(&self.pressed_mouse_buttons).map(|button| {
            create_mouse_event(
                event_time,
                device_id,
                &button_set,
                new_cursor_position,
                &transform,
                mouse::Phase::Down(Button(*button)),
            )
        });

        let released = self.pressed_mouse_buttons.difference(&pressed_buttons).map(|button| {
            create_mouse_event(
                event_time,
                device_id,
                &button_set,
                new_cursor_position,
                &transform,
                mouse::Phase::Up(Button(*button)),
            )
        });
        let events = newly_pressed.chain(move_event).chain(released).collect();
        self.pressed_mouse_buttons = pressed_buttons;
        events
    }

    fn handle_keyboard_input_report(
        &mut self,
        event_time: u64,
        device_id: &DeviceId,
        keyboard: &hid_input_report::KeyboardInputReport,
        context: &mut dyn AutoRepeatTimer,
    ) -> Vec<Event> {
        let pressed_keys: HashSet<fidl_fuchsia_input::Key> =
            if let Some(ref pressed_keys) = keyboard.pressed_keys3 {
                HashSet::from_iter(pressed_keys.iter().map(|key| *key))
            } else {
                HashSet::new()
            };

        let modifiers = Modifiers::from_pressed_keys_3(&pressed_keys);

        let mut first_non_modifier: Option<fidl_fuchsia_input::Key> = None;

        let newly_pressed = pressed_keys.difference(&self.pressed_keys).map(|key| {
            if first_non_modifier.is_none() && !Modifiers::is_modifier(key) {
                first_non_modifier = Some(*key);
            }
            create_keyboard_event(
                event_time,
                device_id,
                keyboard::Phase::Pressed,
                *key,
                &modifiers,
                self.keymap,
            )
        });

        let mut repeating: Option<fidl_fuchsia_input::Key> = self.repeating.clone();

        let released = self.pressed_keys.difference(&pressed_keys).map(|key| {
            if repeating.as_ref() == Some(key) {
                repeating = None;
            }

            create_keyboard_event(
                event_time,
                device_id,
                keyboard::Phase::Released,
                *key,
                &modifiers,
                self.keymap,
            )
        });

        let events = newly_pressed.chain(released).collect();
        self.pressed_keys = pressed_keys;
        self.repeating = first_non_modifier.or(repeating);
        if Config::get().keyboard_autorepeat && self.repeating.is_some() {
            context.schedule_autorepeat_timer(&self.device_id);
        }
        events
    }

    pub fn handle_keyboard_autorepeat(
        &mut self,
        device_id: &DeviceId,
        context: &mut AutoRepeatContext,
    ) -> Vec<Event> {
        if let Some(key) = self.repeating.as_ref() {
            let repeat_time = fuchsia_zircon::Time::get_monotonic();
            let modifiers = Modifiers::from_pressed_keys_3(&self.pressed_keys);
            context.continue_autorepeat_timer(&self.device_id);
            let repeat = create_keyboard_event(
                repeat_time.into_nanos() as u64,
                device_id,
                keyboard::Phase::Repeat,
                *key,
                &modifiers,
                self.keymap,
            );
            vec![repeat]
        } else {
            context.cancel_autorepeat_timer();
            Vec::new()
        }
    }

    fn handle_touch_input_report(
        &mut self,
        event_time: u64,
        device_id: &DeviceId,
        touch: &hid_input_report::TouchInputReport,
    ) -> Vec<Event> {
        if self.touch_scale.is_none() {
            return Vec::new();
        }

        let pressed_buttons: HashSet<u8> = if let Some(ref pressed_buttons) = touch.pressed_buttons
        {
            let pressed_buttons_set = pressed_buttons.iter().cloned().collect();
            pressed_buttons_set
        } else {
            HashSet::new()
        };

        let button_set = ButtonSet::new(&pressed_buttons);

        let raw_contacts: HashSet<touch::RawContact> = if let Some(ref contacts) = touch.contacts {
            let id_iter = contacts.iter().filter_map(|contact| {
                if contact.position_x.is_none() || contact.position_y.is_none() {
                    return None;
                }
                let contact_id = contact.contact_id.expect("contact_id");
                let contact_size =
                    if contact.contact_width.is_none() || contact.contact_height.is_none() {
                        None
                    } else {
                        Some(size2(
                            contact.contact_width.expect("contact_width") as i32,
                            contact.contact_height.expect("contact_height") as i32,
                        ))
                    };
                Some(touch::RawContact {
                    contact_id,
                    position: point2(
                        contact.position_x.expect("position_x") as i32,
                        contact.position_y.expect("position_y") as i32,
                    ),
                    contact_size,
                    pressure: contact.pressure,
                })
            });
            HashSet::from_iter(id_iter)
        } else {
            HashSet::new()
        };

        let transform = self.display_rotation.inv_transform(&self.view_size.to_f32());
        let touch_scale = self.touch_scale.as_ref().expect("touch_scale");

        let t = |point: IntPoint| transform.transform_point(point.to_f32()).to_i32();

        let maintained_contacts =
            self.raw_contacts.intersection(&raw_contacts).map(|raw_contact| touch::Contact {
                contact_id: touch::ContactId(raw_contact.contact_id),
                phase: touch::Phase::Moved(
                    t(touch_scale.scale(&raw_contact.position)),
                    raw_contact.contact_size.unwrap_or_else(|| IntSize::zero()),
                ),
            });

        let new_contacts =
            raw_contacts.difference(&self.raw_contacts).map(|raw_contact| touch::Contact {
                contact_id: touch::ContactId(raw_contact.contact_id),
                phase: touch::Phase::Down(
                    t(touch_scale.scale(&raw_contact.position)),
                    raw_contact.contact_size.unwrap_or_else(|| IntSize::zero()),
                ),
            });

        let ended_contacts =
            self.raw_contacts.difference(&raw_contacts).map(|raw_contact| touch::Contact {
                contact_id: touch::ContactId(raw_contact.contact_id),
                phase: touch::Phase::Up,
            });

        let contacts: Vec<touch::Contact> =
            new_contacts.chain(maintained_contacts).chain(ended_contacts).collect();

        self.raw_contacts = raw_contacts;

        let touch_event = touch::Event { contacts: contacts, buttons: button_set };
        let event = Event {
            event_time,
            device_id: device_id.clone(),
            event_type: EventType::Touch(touch_event),
        };
        vec![event]
    }

    fn handle_consumer_control_report(
        &mut self,
        event_time: u64,
        device_id: &DeviceId,
        consumer_control: &hid_input_report::ConsumerControlInputReport,
    ) -> Vec<Event> {
        fn create_consumer_control_event(
            event_time: u64,
            device_id: &DeviceId,
            phase: consumer_control::Phase,
            button: hid_input_report::ConsumerControlButton,
        ) -> Event {
            let consumer_control_event = consumer_control::Event { phase, button };
            Event {
                event_time,
                device_id: device_id.clone(),
                event_type: EventType::ConsumerControl(consumer_control_event),
            }
        }

        let pressed_consumer_control_buttons: HashSet<hid_input_report::ConsumerControlButton> =
            if let Some(ref pressed_buttons) = consumer_control.pressed_buttons {
                let pressed_buttons_set = pressed_buttons.iter().cloned().collect();
                pressed_buttons_set
            } else {
                HashSet::new()
            };
        let newly_pressed = pressed_consumer_control_buttons
            .difference(&self.pressed_consumer_control_buttons)
            .map(|button| {
                create_consumer_control_event(
                    event_time,
                    device_id,
                    consumer_control::Phase::Down,
                    *button,
                )
            });

        let released = self
            .pressed_consumer_control_buttons
            .difference(&pressed_consumer_control_buttons)
            .map(|button| {
                create_consumer_control_event(
                    event_time,
                    device_id,
                    consumer_control::Phase::Up,
                    *button,
                )
            });
        let events = newly_pressed.chain(released).collect();
        self.pressed_consumer_control_buttons = pressed_consumer_control_buttons;
        events
    }

    pub fn handle_input_report(
        &mut self,
        device_id: &DeviceId,
        input_report: &hid_input_report::InputReport,
        context: &mut dyn AutoRepeatTimer,
    ) -> Vec<Event> {
        let mut events = Vec::new();
        let event_time = input_report.event_time.unwrap_or(0) as u64;
        if let Some(mouse) = input_report.mouse.as_ref() {
            events.extend(self.handle_mouse_input_report(event_time, device_id, mouse));
        }
        if let Some(keyboard) = input_report.keyboard.as_ref() {
            events.extend(
                self.handle_keyboard_input_report(event_time, &device_id, keyboard, context),
            );
        }
        if let Some(touch) = input_report.touch.as_ref() {
            events.extend(self.handle_touch_input_report(event_time, &device_id, touch));
        }
        if let Some(consumer_control) = input_report.consumer_control.as_ref() {
            events.extend(self.handle_consumer_control_report(
                event_time,
                &device_id,
                consumer_control,
            ));
        }
        events
    }
}
