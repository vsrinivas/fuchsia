// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app::{InternalSender, MessageInternal},
    drawing::DisplayRotation,
    geometry::{IntPoint, IntRect, IntSize, LimitToBounds, Size},
    view::ViewKey,
};
use anyhow::{format_err, Error};
use euclid::default::{Transform2D, Vector2D};
use fidl::endpoints::create_proxy;
use fidl_fuchsia_input_report as hid_input_report;
use fuchsia_async::{self as fasync, Time, TimeoutExt};
use fuchsia_zircon::{self as zx, Duration};
use futures::TryFutureExt;
use input_synthesis::{
    keymaps::QWERTY_MAP,
    usages::{input3_key_to_hid_usage, key_to_hid_usage},
};
use std::{
    collections::{HashMap, HashSet},
    fs::{self, DirEntry},
    hash::{Hash, Hasher},
    iter::FromIterator,
};

#[derive(Clone, Debug, Default, Eq, PartialEq, Hash)]
pub struct Button(pub u8);

const PRIMARY_BUTTON: u8 = 1;

impl Button {
    pub fn is_primary(&self) -> bool {
        self.0 == PRIMARY_BUTTON
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct ButtonSet {
    buttons: HashSet<Button>,
}

impl ButtonSet {
    pub fn new(buttons: &HashSet<u8>) -> ButtonSet {
        ButtonSet { buttons: buttons.iter().map(|button| Button(*button)).collect() }
    }

    pub fn new_from_flags(flags: u32) -> ButtonSet {
        let buttons: HashSet<u8> = (0..2)
            .filter_map(|index| {
                let mask = 1 << index;
                if flags & mask != 0 {
                    Some(index + 1)
                } else {
                    None
                }
            })
            .collect();
        ButtonSet::new(&buttons)
    }

    pub fn primary_button_is_down(&self) -> bool {
        self.buttons.contains(&Button(PRIMARY_BUTTON))
    }
}

#[derive(Debug, Default, PartialEq, Clone, Copy)]
pub struct Modifiers {
    pub shift: bool,
    pub alt: bool,
    pub control: bool,
    pub caps_lock: bool,
}

impl Modifiers {
    pub(crate) fn from_pressed_keys(pressed_keys: &HashSet<fidl_fuchsia_ui_input2::Key>) -> Self {
        Self {
            shift: pressed_keys.contains(&fidl_fuchsia_ui_input2::Key::LeftShift)
                || pressed_keys.contains(&fidl_fuchsia_ui_input2::Key::RightShift),
            alt: pressed_keys.contains(&fidl_fuchsia_ui_input2::Key::LeftAlt)
                || pressed_keys.contains(&fidl_fuchsia_ui_input2::Key::RightAlt),
            control: pressed_keys.contains(&fidl_fuchsia_ui_input2::Key::LeftCtrl)
                || pressed_keys.contains(&fidl_fuchsia_ui_input2::Key::RightCtrl),
            caps_lock: pressed_keys.contains(&fidl_fuchsia_ui_input2::Key::CapsLock),
        }
    }

    pub(crate) fn from_pressed_keys_3(pressed_keys: &HashSet<fidl_fuchsia_input::Key>) -> Self {
        Self {
            shift: pressed_keys.contains(&fidl_fuchsia_input::Key::LeftShift)
                || pressed_keys.contains(&fidl_fuchsia_input::Key::RightShift),
            alt: pressed_keys.contains(&fidl_fuchsia_input::Key::LeftAlt)
                || pressed_keys.contains(&fidl_fuchsia_input::Key::RightAlt),
            control: pressed_keys.contains(&fidl_fuchsia_input::Key::LeftCtrl)
                || pressed_keys.contains(&fidl_fuchsia_input::Key::RightCtrl),
            caps_lock: pressed_keys.contains(&fidl_fuchsia_input::Key::CapsLock),
        }
    }
}

pub mod mouse {
    use super::*;

    #[derive(Debug, PartialEq, Clone)]
    pub enum Phase {
        Down(Button),
        Up(Button),
        Moved,
    }

    #[derive(Debug, PartialEq, Clone)]
    pub struct Event {
        pub buttons: ButtonSet,
        pub phase: Phase,
        pub location: IntPoint,
    }
}

#[cfg(test)]
mod mouse_tests {
    use super::*;

    pub fn create_test_mouse_event(button: u8) -> Event {
        let mouse_event = mouse::Event {
            buttons: ButtonSet::default(),
            phase: mouse::Phase::Down(Button(button)),
            location: IntPoint::zero(),
        };
        Event {
            event_time: 0,
            device_id: device_id_tests::create_test_device_id(),
            event_type: EventType::Mouse(mouse_event),
        }
    }
}

fn code_point_from_usage(hid_usage: usize, shift: bool) -> Option<u32> {
    if hid_usage < QWERTY_MAP.len() {
        if let Some(map_entry) = QWERTY_MAP[hid_usage] {
            if shift {
                map_entry.1.and_then(|shifted_char| Some(shifted_char as u32))
            } else {
                Some(map_entry.0 as u32)
            }
        } else {
            None
        }
    } else {
        None
    }
}

pub mod keyboard {
    use super::*;

    #[derive(Clone, Copy, Debug, PartialEq)]
    pub enum Phase {
        Pressed,
        Released,
        Cancelled,
        Repeat,
    }

    #[derive(Debug, PartialEq, Clone)]
    pub struct Event {
        pub phase: Phase,
        pub code_point: Option<u32>,
        pub hid_usage: u32,
        pub modifiers: Modifiers,
    }
}

pub mod touch {
    use super::*;

    #[derive(Debug, Eq)]
    pub(crate) struct RawContact {
        pub contact_id: u32,
        pub position: IntPoint,
        pub pressure: Option<i64>,
        pub contact_size: Option<IntSize>,
    }

    impl PartialEq for RawContact {
        fn eq(&self, rhs: &Self) -> bool {
            self.contact_id == rhs.contact_id
        }
    }

    impl Hash for RawContact {
        fn hash<H: Hasher>(&self, state: &mut H) {
            self.contact_id.hash(state);
        }
    }

    #[derive(Clone, Copy, Debug, Eq, Ord, PartialOrd, PartialEq)]
    pub struct ContactId(pub u32);

    #[derive(Debug, PartialEq, Clone)]
    pub enum Phase {
        Down(IntPoint, IntSize),
        Moved(IntPoint, IntSize),
        Up,
        Remove,
        Cancel,
    }

    #[derive(Debug, Clone, PartialEq)]
    pub struct Contact {
        pub contact_id: ContactId,
        pub phase: Phase,
    }

    #[derive(Debug, PartialEq, Clone)]
    pub struct Event {
        pub contacts: Vec<Contact>,
        pub buttons: ButtonSet,
    }
}

#[cfg(test)]
mod touch_tests {
    use super::*;

    pub fn create_test_contact() -> touch::Contact {
        touch::Contact {
            contact_id: touch::ContactId(100),
            phase: touch::Phase::Down(IntPoint::zero(), IntSize::zero()),
        }
    }
}

pub mod pointer {
    use super::*;

    #[derive(Debug, PartialEq, Clone)]
    pub enum Phase {
        Down(IntPoint),
        Moved(IntPoint),
        Up,
        Remove,
        Cancel,
    }

    #[derive(Clone, Debug, Eq, Ord, PartialOrd, PartialEq)]
    pub enum PointerId {
        Mouse(DeviceId),
        Contact(touch::ContactId),
    }

    #[derive(Debug, PartialEq, Clone)]
    pub struct Event {
        pub phase: Phase,
        pub pointer_id: PointerId,
    }

    impl Event {
        pub fn new_from_mouse_event(
            device_id: &DeviceId,
            mouse_event: &mouse::Event,
        ) -> Option<Self> {
            match &mouse_event.phase {
                mouse::Phase::Down(button) => {
                    if button.is_primary() {
                        Some(pointer::Phase::Down(mouse_event.location))
                    } else {
                        None
                    }
                }
                mouse::Phase::Moved => {
                    if mouse_event.buttons.primary_button_is_down() {
                        Some(pointer::Phase::Moved(mouse_event.location))
                    } else {
                        None
                    }
                }
                mouse::Phase::Up(button) => {
                    if button.is_primary() {
                        Some(pointer::Phase::Up)
                    } else {
                        None
                    }
                }
            }
            .and_then(|phase| Some(Self { phase, pointer_id: PointerId::Mouse(device_id.clone()) }))
        }

        pub fn new_from_contact(contact: &touch::Contact) -> Self {
            let phase = match contact.phase {
                touch::Phase::Down(location, ..) => pointer::Phase::Down(location),
                touch::Phase::Moved(location, ..) => pointer::Phase::Moved(location),
                touch::Phase::Up => pointer::Phase::Up,
                touch::Phase::Remove => pointer::Phase::Remove,
                touch::Phase::Cancel => pointer::Phase::Cancel,
            };
            Self { phase, pointer_id: PointerId::Contact(contact.contact_id) }
        }
    }
}

#[cfg(test)]
mod pointer_tests {
    use super::*;

    #[test]
    fn test_pointer_from_mouse() {
        for button in 1..3 {
            let event = mouse_tests::create_test_mouse_event(button);
            match event.event_type {
                EventType::Mouse(mouse_event) => {
                    let pointer_event =
                        pointer::Event::new_from_mouse_event(&event.device_id, &mouse_event);
                    assert_eq!(pointer_event.is_some(), button == 1);
                }
                _ => panic!("I asked for a mouse event"),
            }
        }
    }

    #[test]
    fn test_pointer_from_contact() {
        let contact = touch_tests::create_test_contact();
        let pointer_event = pointer::Event::new_from_contact(&contact);
        match pointer_event.phase {
            pointer::Phase::Down(location) => {
                assert_eq!(location, IntPoint::zero());
            }
            _ => panic!("This should have been a down pointer event"),
        }
    }
}

pub mod consumer_control {
    use super::*;

    #[derive(Debug, PartialEq, Clone, Copy)]
    pub enum Phase {
        Down,
        Up,
    }

    #[derive(Debug, PartialEq, Clone)]
    pub struct Event {
        pub phase: Phase,
        pub button: hid_input_report::ConsumerControlButton,
    }
}

#[derive(Clone, Debug, Default, Eq, PartialEq, Hash, PartialOrd, Ord)]
pub struct DeviceId(pub String);

#[cfg(test)]
mod device_id_tests {
    use super::*;

    pub fn create_test_device_id() -> DeviceId {
        DeviceId("test-device-id-1".to_string())
    }
}

#[derive(Debug, PartialEq, Clone)]
pub enum EventType {
    Mouse(mouse::Event),
    Keyboard(keyboard::Event),
    Touch(touch::Event),
    ConsumerControl(consumer_control::Event),
}

#[derive(Debug, PartialEq, Clone)]
pub struct Event {
    pub event_time: u64,
    pub device_id: DeviceId,
    pub event_type: EventType,
}

fn device_id_for_event(event: &fidl_fuchsia_ui_input::PointerEvent) -> DeviceId {
    let id_string = match event.type_ {
        fidl_fuchsia_ui_input::PointerEventType::Touch => "touch",
        fidl_fuchsia_ui_input::PointerEventType::Mouse => "mouse",
        fidl_fuchsia_ui_input::PointerEventType::Stylus => "stylus",
        fidl_fuchsia_ui_input::PointerEventType::InvertedStylus => "inverted-stylus",
    };
    DeviceId(format!("{}-{}", id_string, event.device_id))
}

async fn listen_to_entry(
    entry: &DirEntry,
    view_key: ViewKey,
    internal_sender: &InternalSender,
) -> Result<(), Error> {
    let (client, server) = zx::Channel::create()?;
    fdio::service_connect(entry.path().to_str().expect("bad path"), server)?;
    let client = fasync::Channel::from_channel(client)?;
    let device = hid_input_report::InputDeviceProxy::new(client);
    let descriptor = device
        .get_descriptor()
        .map_err(|err| format_err!("FIDL error on get_descriptor: {:?}", err))
        .on_timeout(Time::after(Duration::from_millis(200)), || {
            Err(format_err!("FIDL timeout on get_descriptor"))
        })
        .await?;
    let device_id = entry.file_name().to_string_lossy().to_string();
    internal_sender
        .unbounded_send(MessageInternal::RegisterDevice(DeviceId(device_id.clone()), descriptor))
        .expect("unbounded_send");
    let input_report_sender = internal_sender.clone();
    let (input_reports_reader_proxy, input_reports_reader_request) = create_proxy()?;
    device.get_input_reports_reader(input_reports_reader_request)?;
    fasync::Task::local(async move {
        let _device = device;
        loop {
            let reports_res = input_reports_reader_proxy.read_input_reports().await;
            match reports_res {
                Ok(r) => match r {
                    Ok(reports) => {
                        for report in reports {
                            input_report_sender
                                .unbounded_send(MessageInternal::InputReport(
                                    DeviceId(device_id.clone()),
                                    view_key,
                                    report,
                                ))
                                .expect("unbounded_send");
                        }
                    }
                    Err(err) => {
                        eprintln!("Error report from read_input_reports: {}: {}", device_id, err);
                    }
                },
                Err(err) => {
                    eprintln!("Error report from read_input_reports: {}: {}", device_id, err);
                }
            }
        }
    })
    .detach();
    Ok(())
}

pub(crate) async fn listen_for_user_input(
    view_key: ViewKey,
    internal_sender: InternalSender,
) -> Result<(), Error> {
    let input_devices_directory = "/dev/class/input-report";
    let path = std::path::Path::new(input_devices_directory);
    let entries = fs::read_dir(path)?;
    for entry in entries {
        let entry = entry?;
        match listen_to_entry(&entry, view_key, &internal_sender).await {
            Err(err) => {
                eprintln!("Error: {}: {}", entry.file_name().to_string_lossy().to_string(), err)
            }
            _ => (),
        }
    }
    Ok(())
}

#[derive(Debug)]
struct TouchScale {
    target_size: IntSize,
    x: hid_input_report::Range,
    x_span: f32,
    y: hid_input_report::Range,
    y_span: f32,
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
        IntPoint::new(x, y)
    }
}

#[derive(Default)]
pub(crate) struct InputReportHandler {
    view_size: IntSize,
    display_rotation: DisplayRotation,
    touch_scale: Option<TouchScale>,
    cursor_position: IntPoint,
    pressed_mouse_buttons: HashSet<u8>,
    pressed_keys: HashSet<fidl_fuchsia_ui_input2::Key>,
    raw_contacts: HashSet<touch::RawContact>,
    pressed_consumer_control_buttons: HashSet<hid_input_report::ConsumerControlButton>,
}

impl InputReportHandler {
    pub fn new(
        size: IntSize,
        display_rotation: DisplayRotation,
        device_descriptor: &hid_input_report::DeviceDescriptor,
    ) -> InputReportHandler {
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
                        &contact_input_descriptor.position_y.as_ref().expect("position_x").range,
                    ))
                } else {
                    None
                }
            });
        Self::new_with_scale(size, display_rotation, touch_scale)
    }

    fn new_with_scale(
        size: IntSize,
        display_rotation: DisplayRotation,
        touch_scale: Option<TouchScale>,
    ) -> Self {
        Self { view_size: size, display_rotation, touch_scale, ..InputReportHandler::default() }
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
            + Vector2D::new(
                mouse.movement_x.unwrap_or(0) as i32,
                mouse.movement_y.unwrap_or(0) as i32,
            );
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
    ) -> Vec<Event> {
        fn create_keyboard_event(
            event_time: u64,
            device_id: &DeviceId,
            phase: keyboard::Phase,
            key: fidl_fuchsia_ui_input2::Key,
            modifiers: &Modifiers,
        ) -> Event {
            let hid_usage = key_to_hid_usage(key);
            let hid_usage_size = hid_usage as usize;
            let code_point = code_point_from_usage(hid_usage_size, modifiers.shift);
            let keyboard_event =
                keyboard::Event { phase, code_point, hid_usage, modifiers: Modifiers::default() };
            Event {
                event_time,
                device_id: device_id.clone(),
                event_type: EventType::Keyboard(keyboard_event),
            }
        }

        let pressed_keys: HashSet<fidl_fuchsia_ui_input2::Key> =
            if let Some(ref pressed_keys) = keyboard.pressed_keys {
                HashSet::from_iter(pressed_keys.iter().map(|key| *key))
            } else {
                HashSet::new()
            };

        let modifiers = Modifiers::from_pressed_keys(&pressed_keys);

        let newly_pressed = pressed_keys.difference(&self.pressed_keys).map(|key| {
            create_keyboard_event(event_time, device_id, keyboard::Phase::Pressed, *key, &modifiers)
        });

        let released = self.pressed_keys.difference(&pressed_keys).map(|key| {
            create_keyboard_event(
                event_time,
                device_id,
                keyboard::Phase::Released,
                *key,
                &modifiers,
            )
        });

        let events = newly_pressed.chain(released).collect();
        self.pressed_keys = pressed_keys;
        events
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
                        Some(IntSize::new(
                            contact.contact_width.expect("contact_width") as i32,
                            contact.contact_height.expect("contact_height") as i32,
                        ))
                    };
                Some(touch::RawContact {
                    contact_id,
                    position: IntPoint::new(
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
    ) -> Vec<Event> {
        let mut events = Vec::new();
        let event_time = input_report.event_time.unwrap_or(0) as u64;
        if let Some(mouse) = input_report.mouse.as_ref() {
            events.extend(self.handle_mouse_input_report(event_time, device_id, mouse));
        }
        if let Some(keyboard) = input_report.keyboard.as_ref() {
            events.extend(self.handle_keyboard_input_report(event_time, &device_id, keyboard));
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

#[cfg(test)]
mod input_report_tests {
    use super::*;
    use itertools::assert_equal;

    fn make_input_handler() -> InputReportHandler {
        let test_size = IntSize::new(1024, 768);
        let touch_scale = TouchScale {
            target_size: test_size,
            x: fidl_fuchsia_input_report::Range { min: 0, max: 4095 },
            x_span: 4095.0,
            y: fidl_fuchsia_input_report::Range { min: 0, max: 4095 },
            y_span: 4095.0,
        };
        InputReportHandler::new_with_scale(test_size, DisplayRotation::Deg0, Some(touch_scale))
    }

    #[test]
    fn test_typed_string() {
        let reports = test_data::hello_world_keyboard_reports();

        let mut input_handler = make_input_handler();

        let device_id = DeviceId("keyboard-1".to_string());
        let chars_from_events = reports
            .iter()
            .map(|input_report| input_handler.handle_input_report(&device_id, input_report))
            .flatten()
            .filter_map(|event| match event.event_type {
                EventType::Keyboard(keyboard_event) => match keyboard_event.phase {
                    keyboard::Phase::Pressed => keyboard_event
                        .code_point
                        .and_then(|code_point| Some(code_point as u8 as char)),
                    _ => None,
                },
                _ => None,
            });

        assert_equal("Hello World".chars(), chars_from_events);
    }

    #[test]
    fn test_touch_drag() {
        let reports = test_data::touch_drag_input_reports();

        let device_id = DeviceId("touch-1".to_string());

        let mut input_handler = make_input_handler();

        let events = reports
            .iter()
            .map(|input_report| input_handler.handle_input_report(&device_id, input_report))
            .flatten();

        let mut start_point = IntPoint::zero();
        let mut end_point = IntPoint::zero();
        let mut move_count = 0;
        for event in events {
            match event.event_type {
                EventType::Touch(touch_event) => {
                    let contact = touch_event.contacts.iter().nth(0).expect("first contact");
                    match contact.phase {
                        touch::Phase::Down(location, _) => {
                            start_point = location;
                        }
                        touch::Phase::Moved(location, _) => {
                            end_point = location;
                            move_count += 1;
                        }
                        _ => (),
                    }
                }
                _ => (),
            }
        }

        assert_eq!(start_point, IntPoint::new(302, 491));
        assert_eq!(end_point, IntPoint::new(637, 21));
        assert_eq!(move_count, 15);
    }

    #[test]
    fn test_mouse_drag() {
        let reports = test_data::mouse_drag_input_reports();

        let device_id = DeviceId("touch-1".to_string());

        let mut input_handler = make_input_handler();
        let events = reports
            .iter()
            .map(|input_report| input_handler.handle_input_report(&device_id, input_report))
            .flatten();

        let mut start_point = IntPoint::zero();
        let mut end_point = IntPoint::zero();
        let mut move_count = 0;
        let mut down_button = None;
        for event in events {
            match event.event_type {
                EventType::Mouse(mouse_event) => match mouse_event.phase {
                    mouse::Phase::Down(button) => {
                        assert!(down_button.is_none());
                        assert!(button.is_primary());
                        start_point = mouse_event.location;
                        down_button = Some(button);
                    }
                    mouse::Phase::Moved => {
                        end_point = mouse_event.location;
                        move_count += 1;
                    }
                    mouse::Phase::Up(button) => {
                        assert!(button.is_primary());
                    }
                },
                _ => (),
            }
        }

        assert!(down_button.expect("down_button").is_primary());
        assert_eq!(start_point, IntPoint::new(129, 44));
        assert_eq!(end_point, IntPoint::new(616, 213));
        assert_eq!(move_count, 181);
    }

    #[test]
    fn test_consumer_control() {
        use hid_input_report::ConsumerControlButton::{VolumeDown, VolumeUp};
        let reports = test_data::consumer_control_input_reports();

        let device_id = DeviceId("cc-1".to_string());

        let mut input_handler = make_input_handler();
        let events: Vec<(consumer_control::Phase, hid_input_report::ConsumerControlButton)> =
            reports
                .iter()
                .map(|input_report| input_handler.handle_input_report(&device_id, input_report))
                .flatten()
                .filter_map(|event| match event.event_type {
                    EventType::ConsumerControl(consumer_control_event) => {
                        Some((consumer_control_event.phase, consumer_control_event.button))
                    }
                    _ => None,
                })
                .collect();

        let expected = [
            (consumer_control::Phase::Down, VolumeUp),
            (consumer_control::Phase::Up, VolumeUp),
            (consumer_control::Phase::Down, VolumeDown),
            (consumer_control::Phase::Up, VolumeDown),
        ];
        assert_eq!(events, expected);
    }
}

// Scenic has a logical/physical coordinate system scheme that Carnelian does not currently
// want to expose.
fn to_physical_point(x: f32, y: f32, metrics: &Size) -> IntPoint {
    let x = x * metrics.width;
    let y = y * metrics.height;
    IntPoint::new(x as i32, y as i32)
}

#[derive(Default)]
pub(crate) struct ScenicInputHandler {
    contacts: HashMap<u32, touch::Contact>,
    keyboard_device_id: DeviceId,
    pressed_keys: HashSet<fidl_fuchsia_input::Key>,
}

impl ScenicInputHandler {
    pub fn new() -> Self {
        Self {
            keyboard_device_id: DeviceId("scenic-keyboard".to_string()),
            contacts: HashMap::new(),
            pressed_keys: HashSet::new(),
        }
    }

    fn convert_scenic_mouse_phase(
        &self,
        event: &fidl_fuchsia_ui_input::PointerEvent,
    ) -> Option<mouse::Phase> {
        let buttons = event.buttons;
        let pressed_buttons: HashSet<u8> = (0..3)
            .filter_map(|index| if buttons & 1 << index != 0 { Some(index + 1) } else { None })
            .collect();
        match event.phase {
            fidl_fuchsia_ui_input::PointerEventPhase::Down => pressed_buttons
                .iter()
                .nth(0)
                .and_then(|button| Some(mouse::Phase::Down(Button(*button)))),
            fidl_fuchsia_ui_input::PointerEventPhase::Move => Some(mouse::Phase::Moved),
            fidl_fuchsia_ui_input::PointerEventPhase::Up => pressed_buttons
                .iter()
                .nth(0)
                .and_then(|button| Some(mouse::Phase::Up(Button(*button)))),
            _ => None,
        }
    }

    fn handle_touch_event(
        &mut self,
        metrics: &Size,
        event: &fidl_fuchsia_ui_input::PointerEvent,
    ) -> Option<Event> {
        let device_id = device_id_for_event(event);
        let location = to_physical_point(event.x, event.y, metrics);
        let phase = match event.phase {
            fidl_fuchsia_ui_input::PointerEventPhase::Down => {
                Some(touch::Phase::Down(location, IntSize::zero()))
            }
            fidl_fuchsia_ui_input::PointerEventPhase::Move => {
                Some(touch::Phase::Moved(location, IntSize::zero()))
            }
            fidl_fuchsia_ui_input::PointerEventPhase::Up => Some(touch::Phase::Up),
            fidl_fuchsia_ui_input::PointerEventPhase::Remove => Some(touch::Phase::Remove),
            fidl_fuchsia_ui_input::PointerEventPhase::Cancel => Some(touch::Phase::Cancel),
            _ => None,
        };

        if let Some(phase) = phase {
            let contact = touch::Contact { contact_id: touch::ContactId(event.pointer_id), phase };
            self.contacts.insert(event.pointer_id, contact);
            let contacts: Vec<touch::Contact> = self.contacts.values().map(|v| v.clone()).collect();
            self.contacts.retain(|_, contact| match contact.phase {
                touch::Phase::Remove => false,
                touch::Phase::Cancel => false,
                _ => true,
            });
            let buttons = ButtonSet::new_from_flags(event.buttons);
            let touch_event = touch::Event { buttons, contacts };
            let new_event = Event {
                event_type: EventType::Touch(touch_event),
                device_id: device_id,
                event_time: event.event_time,
            };
            Some(new_event)
        } else {
            None
        }
    }

    fn handle_scenic_pointer_event(
        &mut self,
        metrics: &Size,
        event: &fidl_fuchsia_ui_input::PointerEvent,
    ) -> Vec<Event> {
        let mut events = Vec::new();
        let location = to_physical_point(event.x, event.y, metrics);
        match event.type_ {
            fidl_fuchsia_ui_input::PointerEventType::Touch => {
                let new_event = self.handle_touch_event(metrics, event);
                events.extend(new_event);
            }
            fidl_fuchsia_ui_input::PointerEventType::Mouse => {
                if let Some(phase) = self.convert_scenic_mouse_phase(&event) {
                    let device_id = device_id_for_event(event);
                    let mouse_input = mouse::Event {
                        location,
                        buttons: ButtonSet::new_from_flags(event.buttons),
                        phase: phase,
                    };
                    let new_event = Event {
                        event_type: EventType::Mouse(mouse_input),
                        device_id: device_id,
                        event_time: event.event_time,
                    };
                    events.push(new_event);
                }
            }
            _ => (),
        }
        events
    }

    pub fn handle_scenic_input_event(
        &mut self,
        metrics: &Size,
        event: &fidl_fuchsia_ui_input::InputEvent,
    ) -> Vec<Event> {
        let mut events = Vec::new();
        match event {
            fidl_fuchsia_ui_input::InputEvent::Pointer(pointer_event) => {
                events.extend(self.handle_scenic_pointer_event(metrics, &pointer_event));
            }
            _ => (),
        }
        events
    }

    pub fn handle_scenic_key_event(
        &mut self,
        event: &fidl_fuchsia_ui_input3::KeyEvent,
    ) -> Vec<Event> {
        if event.type_.is_none() || event.timestamp.is_none() || event.key.is_none() {
            println!("Malformed scenic key event {:?}", event);
            return vec![];
        }
        let event_type = event.type_.unwrap();
        let timestamp = event.timestamp.unwrap() as u64;
        let key = event.key.unwrap();
        let phase: Option<keyboard::Phase> = match event_type {
            fidl_fuchsia_ui_input3::KeyEventType::Pressed => Some(keyboard::Phase::Pressed),
            fidl_fuchsia_ui_input3::KeyEventType::Released => Some(keyboard::Phase::Released),
            fidl_fuchsia_ui_input3::KeyEventType::Cancel => Some(keyboard::Phase::Cancelled),
            // The input3 sync feature is not supported
            fidl_fuchsia_ui_input3::KeyEventType::Sync => None,
        };
        if phase.is_none() {
            return vec![];
        }
        let phase = phase.unwrap();

        match phase {
            keyboard::Phase::Pressed => {
                self.pressed_keys.insert(key);
            }
            keyboard::Phase::Released | keyboard::Phase::Cancelled => {
                self.pressed_keys.remove(&key);
            }
            _ => (),
        }

        let device_id = self.keyboard_device_id.clone();
        let hid_usage = input3_key_to_hid_usage(key);
        let modifiers = Modifiers::from_pressed_keys_3(&self.pressed_keys);
        let code_point = code_point_from_usage(hid_usage as usize, modifiers.shift);
        let keyboard_event = keyboard::Event { code_point, hid_usage, modifiers, phase };

        let event = Event {
            event_time: timestamp,
            event_type: EventType::Keyboard(keyboard_event),
            device_id,
        };

        vec![event]
    }
}

#[cfg(test)]
mod scenic_input_tests {
    use super::*;
    use itertools::assert_equal;

    #[test]
    fn test_typed_string() {
        let scenic_events = test_data::hello_world_scenic_input_events();

        let mut scenic_input_handler = ScenicInputHandler::new();
        let chars_from_events = scenic_events
            .iter()
            .map(|event| scenic_input_handler.handle_scenic_key_event(event))
            .flatten()
            .filter_map(|event| match event.event_type {
                EventType::Keyboard(keyboard_event) => match keyboard_event.phase {
                    keyboard::Phase::Pressed => keyboard_event
                        .code_point
                        .and_then(|code_point| Some(code_point as u8 as char)),
                    _ => None,
                },
                _ => None,
            });

        assert_equal("Hello World".chars(), chars_from_events);
    }

    #[test]
    fn test_control_r() {
        let scenic_events = test_data::control_r_scenic_events();

        // make sure there's one and only one keydown even of the r
        // key with the control modifier set.
        let mut scenic_input_handler = ScenicInputHandler::new();
        let expected_event_count: usize = scenic_events
            .iter()
            .map(|event| scenic_input_handler.handle_scenic_key_event(event))
            .flatten()
            .filter_map(|event| match event.event_type {
                EventType::Keyboard(keyboard_event) => match keyboard_event.phase {
                    keyboard::Phase::Pressed => {
                        if keyboard_event.hid_usage
                            == input_synthesis::usages::Usages::HidUsageKeyR as u32
                            && keyboard_event.modifiers.control
                        {
                            Some(())
                        } else {
                            None
                        }
                    }
                    _ => None,
                },
                _ => None,
            })
            .count();

        assert_eq!(expected_event_count, 1);
    }

    #[test]
    fn test_touch_drag() {
        let scenic_events = test_data::touch_drag_scenic_events();
        let mut scenic_input_handler = ScenicInputHandler::new();
        let metrics = Size::new(1.0, 1.0);
        let input_events = scenic_events
            .iter()
            .map(|event| scenic_input_handler.handle_scenic_input_event(&metrics, event))
            .flatten();

        let mut start_point = IntPoint::zero();
        let mut end_point = IntPoint::zero();
        let mut move_count = 0;
        for event in input_events {
            match event.event_type {
                EventType::Touch(touch_event) => {
                    let contact = touch_event.contacts.iter().nth(0).expect("first contact");
                    match contact.phase {
                        touch::Phase::Down(location, _) => {
                            start_point = location;
                        }
                        touch::Phase::Moved(location, _) => {
                            end_point = location;
                            move_count += 1;
                        }
                        _ => (),
                    }
                }
                _ => (),
            }
        }

        assert_eq!(start_point, IntPoint::new(193, 107));
        assert_eq!(end_point, IntPoint::new(269, 157));
        assert_eq!(move_count, 8);
    }

    #[test]
    fn test_mouse_drag() {
        let scenic_events = test_data::mouse_drag_scenic_events();
        let mut scenic_input_handler = ScenicInputHandler::new();
        let metrics = Size::new(1.0, 1.0);
        let input_events = scenic_events
            .iter()
            .map(|event| scenic_input_handler.handle_scenic_input_event(&metrics, event))
            .flatten();

        let mut start_point = IntPoint::zero();
        let mut end_point = IntPoint::zero();
        let mut move_count = 0;
        let mut down_button = None;
        for event in input_events {
            match event.event_type {
                EventType::Mouse(mouse_event) => match mouse_event.phase {
                    mouse::Phase::Down(button) => {
                        assert!(down_button.is_none());
                        assert!(button.is_primary());
                        start_point = mouse_event.location;
                        down_button = Some(button);
                    }
                    mouse::Phase::Moved => {
                        end_point = mouse_event.location;
                        move_count += 1;
                    }
                    mouse::Phase::Up(button) => {
                        assert!(button.is_primary());
                    }
                },
                _ => (),
            }
        }

        assert!(down_button.expect("down_button").is_primary());
        assert_eq!(start_point, IntPoint::new(67, 62));
        assert_eq!(end_point, IntPoint::new(128, 136));
        assert_eq!(move_count, 36);
    }
}

#[cfg(test)]
mod test_data {
    pub fn consumer_control_input_reports() -> Vec<fidl_fuchsia_input_report::InputReport> {
        use fidl_fuchsia_input_report::{
            ConsumerControlButton::{VolumeDown, VolumeUp},
            ConsumerControlInputReport, InputReport,
        };
        vec![
            InputReport {
                event_time: Some(66268999833),
                mouse: None,
                trace_id: Some(2),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: Some(ConsumerControlInputReport {
                    pressed_buttons: Some([VolumeUp].to_vec()),
                    ..ConsumerControlInputReport::EMPTY
                }),
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(66434561666),
                mouse: None,
                trace_id: Some(3),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: Some(ConsumerControlInputReport {
                    pressed_buttons: Some([].to_vec()),
                    ..ConsumerControlInputReport::EMPTY
                }),
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(358153537000),
                mouse: None,
                trace_id: Some(4),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: Some(ConsumerControlInputReport {
                    pressed_buttons: Some([VolumeDown].to_vec()),
                    ..ConsumerControlInputReport::EMPTY
                }),
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(358376260958),
                mouse: None,
                trace_id: Some(5),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: Some(ConsumerControlInputReport {
                    pressed_buttons: Some([].to_vec()),
                    ..ConsumerControlInputReport::EMPTY
                }),
                ..InputReport::EMPTY
            },
        ]
    }
    pub fn hello_world_keyboard_reports() -> Vec<fidl_fuchsia_input_report::InputReport> {
        use {
            fidl_fuchsia_input_report::{InputReport, KeyboardInputReport},
            fidl_fuchsia_ui_input2::Key::*,
        };
        vec![
            InputReport {
                event_time: Some(85446402710730),
                mouse: None,
                trace_id: Some(189),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![LeftShift]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85446650713601),
                mouse: None,
                trace_id: Some(191),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![LeftShift, H]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85446738712880),
                mouse: None,
                trace_id: Some(193),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![LeftShift]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85446794702907),
                mouse: None,
                trace_id: Some(195),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85446970709193),
                mouse: None,
                trace_id: Some(197),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![E]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85447090710657),
                mouse: None,
                trace_id: Some(199),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85447330708990),
                mouse: None,
                trace_id: Some(201),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![L]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85447394712460),
                mouse: None,
                trace_id: Some(203),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85447508813465),
                mouse: None,
                trace_id: Some(205),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![L]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85447618712982),
                mouse: None,
                trace_id: Some(207),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85447810705156),
                mouse: None,
                trace_id: Some(209),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![O]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85447898703263),
                mouse: None,
                trace_id: Some(211),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85450082706011),
                mouse: None,
                trace_id: Some(213),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![Space]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85450156060503),
                mouse: None,
                trace_id: Some(215),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85450418710803),
                mouse: None,
                trace_id: Some(217),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![LeftShift]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85450594712232),
                mouse: None,
                trace_id: Some(219),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![LeftShift, W]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85450746707982),
                mouse: None,
                trace_id: Some(221),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![W]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85450794706822),
                mouse: None,
                trace_id: Some(223),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85450962706591),
                mouse: None,
                trace_id: Some(225),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![O]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85451050703903),
                mouse: None,
                trace_id: Some(227),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85451282710803),
                mouse: None,
                trace_id: Some(229),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![R]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85451411293149),
                mouse: None,
                trace_id: Some(231),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85451842714565),
                mouse: None,
                trace_id: Some(233),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![L]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85451962704075),
                mouse: None,
                trace_id: Some(235),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85452906710709),
                mouse: None,
                trace_id: Some(237),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![D]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85453034711602),
                mouse: None,
                trace_id: Some(239),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85454778708461),
                mouse: None,
                trace_id: Some(241),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![Enter]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85454858706151),
                mouse: None,
                trace_id: Some(243),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys: Some(vec![]),
                    pressed_keys3: None,
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
        ]
    }

    pub fn touch_drag_input_reports() -> Vec<fidl_fuchsia_input_report::InputReport> {
        use fidl_fuchsia_input_report::{ContactInputReport, InputReport, TouchInputReport};

        vec![
            InputReport {
                event_time: Some(2129689875195),
                mouse: None,
                trace_id: Some(294),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(1211),
                        position_y: Some(2621),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129715875833),
                mouse: None,
                trace_id: Some(295),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(1211),
                        position_y: Some(2621),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129741874822),
                mouse: None,
                trace_id: Some(296),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(1223),
                        position_y: Some(2607),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129767876545),
                mouse: None,
                trace_id: Some(297),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(1267),
                        position_y: Some(2539),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129793872236),
                mouse: None,
                trace_id: Some(298),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(1391),
                        position_y: Some(2300),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129818875839),
                mouse: None,
                trace_id: Some(299),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(1523),
                        position_y: Some(2061),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129844873276),
                mouse: None,
                trace_id: Some(300),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(1675),
                        position_y: Some(1781),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129870884557),
                mouse: None,
                trace_id: Some(301),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(1743),
                        position_y: Some(1652),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129896870474),
                mouse: None,
                trace_id: Some(302),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(1875),
                        position_y: Some(1399),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129922876931),
                mouse: None,
                trace_id: Some(303),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(2015),
                        position_y: Some(1174),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129948875990),
                mouse: None,
                trace_id: Some(304),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(2143),
                        position_y: Some(935),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129973877732),
                mouse: None,
                trace_id: Some(305),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(2275),
                        position_y: Some(682),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129998870634),
                mouse: None,
                trace_id: Some(306),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(2331),
                        position_y: Some(566),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2130023872212),
                mouse: None,
                trace_id: Some(307),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(2439),
                        position_y: Some(314),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2130048871365),
                mouse: None,
                trace_id: Some(308),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(2551),
                        position_y: Some(116),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2130071873308),
                mouse: None,
                trace_id: Some(309),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(2643),
                        position_y: Some(54),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2130110871653),
                mouse: None,
                trace_id: Some(310),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
        ]
    }

    pub fn mouse_drag_input_reports() -> Vec<fidl_fuchsia_input_report::InputReport> {
        use fidl_fuchsia_input_report::{InputReport, MouseInputReport};
        vec![
            InputReport {
                event_time: Some(101114216676),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(1),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101122479286),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(3),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101130223338),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(4),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101139198674),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(5),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101154621806),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(6),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101162221969),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(7),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101170222632),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(8),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101178218319),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(9),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101195538881),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(10),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101202218423),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(11),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101210236557),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(12),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101218244736),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(13),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101226633284),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(14),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101235789939),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(15),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101242227234),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(16),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101250552651),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(17),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101258523666),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(18),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101266879375),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(19),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101279470078),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(20),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101282237222),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(21),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101290229686),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(22),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101298227434),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(23),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101306236833),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(24),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101314225440),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(25),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101322221224),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(26),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101330220567),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(27),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101338229995),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(28),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101346226157),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(29),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101354223947),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(30),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101362223006),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(31),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101370218719),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(32),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101378220583),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(33),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101386213038),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(34),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101394217453),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(35),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101402219904),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(36),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101410221107),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(37),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101418222560),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(38),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101434218357),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(39),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101442218953),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(40),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101450217289),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(41),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101458214227),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(42),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101466225708),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(43),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101474215177),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(44),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101482221526),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(45),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101490219532),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(46),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101498222281),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(47),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101506214971),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(48),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101514219490),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(49),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101522217217),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(50),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101530217381),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(51),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101538212289),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(52),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101554216328),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(53),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103242211673),
                mouse: Some(MouseInputReport {
                    movement_x: Some(0),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(54),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103330219916),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(55),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103338210706),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(56),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103346224236),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(8),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(57),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103354212884),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(10),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(58),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103362215662),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(10),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(59),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103370214381),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(11),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(60),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103378214091),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(11),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(61),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103386209918),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(12),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(62),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103394217896),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(12),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(63),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103402213295),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(13),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(64),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103410215085),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(13),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(65),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103418219723),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(12),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(66),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103426211988),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(12),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(67),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103434211330),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(12),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(68),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103442219232),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(10),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(69),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103450211768),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(9),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(70),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103458211814),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(9),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(71),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103466216581),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(8),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(72),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103474215898),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(73),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103482215147),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(74),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103490216112),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(75),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103498215973),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(76),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103506213277),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(77),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103514218088),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(78),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103522217065),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(79),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103530210262),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(80),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103578218194),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(81),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103586213104),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(82),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103594216835),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(83),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103602487409),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(84),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103610212817),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(85),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103618214151),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(86),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103626214410),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(87),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103634212067),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(88),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103642212545),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(89),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103650212962),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(-5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(90),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103658212822),
                mouse: Some(MouseInputReport {
                    movement_x: Some(8),
                    movement_y: Some(-5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(91),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103666210198),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(-5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(92),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103674217073),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(-6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(93),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103682212701),
                mouse: Some(MouseInputReport {
                    movement_x: Some(9),
                    movement_y: Some(-7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(94),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103690210927),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(-7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(95),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103698216512),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(-6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(96),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103706213176),
                mouse: Some(MouseInputReport {
                    movement_x: Some(8),
                    movement_y: Some(-6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(97),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103714212778),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(-6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(98),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103722213889),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(99),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103730210581),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(-7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(100),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103738214789),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(101),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103746216817),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(102),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103754216490),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(103),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103762214303),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(-2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(104),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103770212491),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(105),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103778217308),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(-2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(106),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103786212710),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(-2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(107),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103794217315),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(108),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103802211383),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(109),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103810216190),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(110),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103834222367),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(111),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103954219855),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(112),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103962217418),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(113),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103970214839),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(114),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103978214040),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(9),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(115),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103986213448),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(116),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103994211708),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(8),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(117),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104002212585),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(8),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(118),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104010210902),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(10),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(119),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104018211093),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(120),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104026216997),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(9),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(121),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104034211539),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(10),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(122),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104042222246),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(123),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104050216094),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(10),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(124),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104058215037),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(125),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104066221081),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(126),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104074216757),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(127),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104082216368),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(8),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(128),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104090217281),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(9),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(129),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104098212452),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(8),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(130),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104106216109),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(131),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104114266027),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(132),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104122212879),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(133),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104130216506),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(134),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104138217516),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(135),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104146210328),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(136),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104154216601),
                mouse: Some(MouseInputReport {
                    movement_x: Some(0),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(137),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104162216056),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(138),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104170215445),
                mouse: Some(MouseInputReport {
                    movement_x: Some(0),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(139),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104178211471),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(140),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104186213147),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(141),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104194212256),
                mouse: Some(MouseInputReport {
                    movement_x: Some(0),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(142),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104202213946),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(143),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104210212892),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(144),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104218214234),
                mouse: Some(MouseInputReport {
                    movement_x: Some(0),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(145),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104226215241),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(146),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104234215524),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(147),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104282215440),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(148),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104290210105),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(149),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104298226745),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(150),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104306215865),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(151),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104314217045),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(152),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104322334192),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(153),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104330216276),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(154),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104338214799),
                mouse: Some(MouseInputReport {
                    movement_x: Some(8),
                    movement_y: Some(-6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(155),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104346215946),
                mouse: Some(MouseInputReport {
                    movement_x: Some(10),
                    movement_y: Some(-7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(156),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104354214863),
                mouse: Some(MouseInputReport {
                    movement_x: Some(8),
                    movement_y: Some(-6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(157),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104362215296),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(158),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104370214666),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(159),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104378215593),
                mouse: Some(MouseInputReport {
                    movement_x: Some(9),
                    movement_y: Some(-6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(160),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104386215460),
                mouse: Some(MouseInputReport {
                    movement_x: Some(9),
                    movement_y: Some(-7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(161),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104394217072),
                mouse: Some(MouseInputReport {
                    movement_x: Some(9),
                    movement_y: Some(-7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(162),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104402213289),
                mouse: Some(MouseInputReport {
                    movement_x: Some(8),
                    movement_y: Some(-6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(163),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104410215719),
                mouse: Some(MouseInputReport {
                    movement_x: Some(9),
                    movement_y: Some(-7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(164),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104418216898),
                mouse: Some(MouseInputReport {
                    movement_x: Some(9),
                    movement_y: Some(-8),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(165),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104426215292),
                mouse: Some(MouseInputReport {
                    movement_x: Some(8),
                    movement_y: Some(-5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(166),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104434215345),
                mouse: Some(MouseInputReport {
                    movement_x: Some(8),
                    movement_y: Some(-7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(167),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104442217176),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(-6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(168),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104450214083),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(-5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(169),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104458213546),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(170),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104466216290),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(171),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104474215684),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(172),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104482216348),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(173),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104490211575),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(174),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104498215305),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(-2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(175),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104506212563),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(176),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104514213178),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(-2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(177),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104522213190),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(-2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(178),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104530216023),
                mouse: Some(MouseInputReport {
                    movement_x: Some(0),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(179),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104866221719),
                mouse: Some(MouseInputReport {
                    movement_x: Some(0),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(180),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(105266217002),
                mouse: Some(MouseInputReport {
                    movement_x: Some(-2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(181),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(105274246358),
                mouse: Some(MouseInputReport {
                    movement_x: Some(-1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(182),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(105282216030),
                mouse: Some(MouseInputReport {
                    movement_x: Some(-2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(183),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(105290214427),
                mouse: Some(MouseInputReport {
                    movement_x: Some(-2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(184),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
        ]
    }

    pub fn hello_world_scenic_input_events() -> Vec<fidl_fuchsia_ui_input3::KeyEvent> {
        use fidl_fuchsia_input::Key::*;
        use fidl_fuchsia_ui_input3::{
            KeyEvent,
            KeyEventType::{Pressed, Released},
        };
        vec![
            KeyEvent {
                timestamp: Some(3264387612285),
                type_: Some(Pressed),
                key: Some(LeftShift),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3265130500125),
                type_: Some(Pressed),
                key: Some(H),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3265266507731),
                type_: Some(Released),
                key: Some(H),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3265370503901),
                type_: Some(Released),
                key: Some(LeftShift),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3266834499940),
                type_: Some(Pressed),
                key: Some(E),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3266962508842),
                type_: Some(Released),
                key: Some(E),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3267154500453),
                type_: Some(Pressed),
                key: Some(L),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3267219444859),
                type_: Some(Released),
                key: Some(L),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3267346499392),
                type_: Some(Pressed),
                key: Some(L),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3267458502427),
                type_: Some(Released),
                key: Some(L),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3267690502669),
                type_: Some(Pressed),
                key: Some(O),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3267858501367),
                type_: Some(Released),
                key: Some(O),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3275178512511),
                type_: Some(Pressed),
                key: Some(Space),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3275274501635),
                type_: Some(Pressed),
                key: Some(LeftShift),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3275298499697),
                type_: Some(Released),
                key: Some(Space),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3275474504423),
                type_: Some(Pressed),
                key: Some(W),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3275586502431),
                type_: Some(Released),
                key: Some(LeftShift),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3275634500151),
                type_: Some(Released),
                key: Some(W),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3275714502408),
                type_: Some(Pressed),
                key: Some(O),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3275834561768),
                type_: Some(Released),
                key: Some(O),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3275858499854),
                type_: Some(Pressed),
                key: Some(R),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3276018509754),
                type_: Some(Released),
                key: Some(R),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3276114540325),
                type_: Some(Pressed),
                key: Some(L),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3276282504845),
                type_: Some(Released),
                key: Some(L),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3276578503737),
                type_: Some(Pressed),
                key: Some(D),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3276706501366),
                type_: Some(Released),
                key: Some(D),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
        ]
    }

    pub fn control_r_scenic_events() -> Vec<fidl_fuchsia_ui_input3::KeyEvent> {
        use fidl_fuchsia_input::Key::*;
        use fidl_fuchsia_ui_input3::{
            KeyEvent,
            KeyEventType::{Pressed, Released},
        };
        vec![
            KeyEvent {
                timestamp: Some(4453530520711),
                type_: Some(Pressed),
                key: Some(LeftCtrl),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(4454138519645),
                type_: Some(Pressed),
                key: Some(R),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(4454730534107),
                type_: Some(Released),
                key: Some(R),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(4454738498944),
                type_: Some(Released),
                key: Some(LeftCtrl),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
        ]
    }

    pub fn touch_drag_scenic_events() -> Vec<fidl_fuchsia_ui_input::InputEvent> {
        use fidl_fuchsia_ui_input::{
            FocusEvent,
            InputEvent::{Focus, Pointer},
            PointerEvent,
            PointerEventPhase::{Add, Down, Move, Remove, Up},
            PointerEventType::Touch,
        };
        vec![
            Pointer(PointerEvent {
                event_time: 3724420542810,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Add,
                x: 193.06534,
                y: 107.604416,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Focus(FocusEvent { event_time: 3724420796330, focused: true }),
            Pointer(PointerEvent {
                event_time: 3724420542810,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Down,
                x: 193.06534,
                y: 107.604416,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724446545561,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Move,
                x: 193.06534,
                y: 107.604416,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724472567434,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Move,
                x: 194.81122,
                y: 108.20114,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724498537301,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Move,
                x: 200.63081,
                y: 112.29306,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724524543861,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Move,
                x: 219.8355,
                y: 125.67702,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724550551818,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Move,
                x: 239.04019,
                y: 137.86748,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724575547592,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Move,
                x: 254.17116,
                y: 149.5465,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724601536497,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Move,
                x: 260.57272,
                y: 153.04164,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724627538012,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Move,
                x: 269.8841,
                y: 157.13356,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724669535009,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Up,
                x: 269.8841,
                y: 157.13356,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724669535009,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Remove,
                x: 269.8841,
                y: 157.13356,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
        ]
    }

    pub fn mouse_drag_scenic_events() -> Vec<fidl_fuchsia_ui_input::InputEvent> {
        use fidl_fuchsia_ui_input::{
            FocusEvent,
            InputEvent::{Focus, Pointer},
            PointerEvent,
            PointerEventPhase::{Down, Move, Up},
            PointerEventType::Mouse,
        };
        vec![
            Focus(FocusEvent { event_time: 112397259832, focused: true }),
            Pointer(PointerEvent {
                event_time: 112396994735,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Down,
                x: 67.49091,
                y: 62.25455,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112508984750,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 68.07273,
                y: 62.25455,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112516989437,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 69.818184,
                y: 62.25455,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112524990631,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 72.72728,
                y: 64.58183,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112532991020,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 74.47273,
                y: 66.9091,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112541018566,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 76.8,
                y: 69.81819,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112548984575,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 79.12728,
                y: 72.72728,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112556985463,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 82.03637,
                y: 76.80002,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112564990769,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 84.94546,
                y: 79.70911,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112572989372,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 87.85455,
                y: 83.78183,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112580993049,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 90.18182,
                y: 86.69092,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112588991675,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 92.509094,
                y: 90.18182,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112596989208,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 95.41819,
                y: 93.672745,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112604989384,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 97.74546,
                y: 96.000015,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112612996959,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 100.07273,
                y: 98.9091,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112620989830,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 101.818184,
                y: 100.65456,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112628994663,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 103.563644,
                y: 102.40001,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112636989146,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 104.72728,
                y: 104.14546,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112644983252,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 105.890915,
                y: 105.3091,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112652985951,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 106.47273,
                y: 105.890915,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112660993261,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 106.47273,
                y: 107.05455,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112669007012,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 107.63637,
                y: 108.8,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112676986758,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 108.8,
                y: 109.96365,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112684990368,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 109.38182,
                y: 111.12729,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112692990815,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 111.12727,
                y: 114.03638,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112700991008,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 112.29092,
                y: 116.36365,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112708991160,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 114.61819,
                y: 119.272736,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112716985316,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 116.36364,
                y: 122.76364,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112724995834,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 118.69091,
                y: 125.09091,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112732988629,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 120.43637,
                y: 128.00002,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112740984820,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 122.18182,
                y: 129.74547,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112748990041,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 123.34546,
                y: 130.9091,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112756988188,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 124.509094,
                y: 132.07274,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112764986974,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 125.67273,
                y: 133.23637,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112772992141,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 126.25455,
                y: 133.81819,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112780992743,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 126.836365,
                y: 134.98183,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112844991890,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 128.0,
                y: 136.14546,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112924985017,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Up,
                x: 128.0,
                y: 136.14546,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
        ]
    }
}
