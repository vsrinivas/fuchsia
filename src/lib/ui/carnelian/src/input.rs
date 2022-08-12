// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app::{InternalSender, MessageInternal},
    geometry::{IntPoint, IntSize},
};
use anyhow::{format_err, Error};
use euclid::default::Transform2D;
use fidl::endpoints::create_proxy;
use fidl_fuchsia_input_report as hid_input_report;
use fuchsia_async::{self as fasync, Time, TimeoutExt};
use fuchsia_fs::OpenFlags;
use fuchsia_syslog::fx_log_warn;
use fuchsia_vfs_watcher as vfs_watcher;
use fuchsia_zircon::{self as zx, Duration};
use futures::{TryFutureExt, TryStreamExt};
use keymaps::usages::input3_key_to_hid_usage;
use std::{
    collections::HashSet,
    fs,
    hash::{Hash, Hasher},
    path::{Path, PathBuf},
};

#[derive(Debug)]
pub(crate) enum UserInputMessage {
    ScenicInputEvent(fidl_fuchsia_ui_input::InputEvent),
    ScenicKeyEvent(fidl_fuchsia_ui_input3::KeyEvent),
    FlatlandMouseEvents(Vec<fidl_fuchsia_ui_pointer::MouseEvent>),
    FlatlandTouchEvents(Vec<fidl_fuchsia_ui_pointer::TouchEvent>),
}

/// A button on a mouse
#[derive(Clone, Debug, Default, Eq, PartialEq, Hash)]
pub struct Button(pub u8);

const PRIMARY_BUTTON: u8 = 1;

impl Button {
    /// Is this the primary button, usually the leftmost button on
    /// a mouse.
    pub fn is_primary(&self) -> bool {
        self.0 == PRIMARY_BUTTON
    }
}

/// A set of buttons.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ButtonSet {
    buttons: HashSet<Button>,
}

impl ButtonSet {
    /// Create a new set of buttons from input report flags.
    pub fn new(buttons: &HashSet<u8>) -> ButtonSet {
        ButtonSet { buttons: buttons.iter().map(|button| Button(*button)).collect() }
    }

    /// Create a new set of buttons from scenic flags.
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

    /// Convenience function for checking if the primary button is down.
    pub fn primary_button_is_down(&self) -> bool {
        self.buttons.contains(&Button(PRIMARY_BUTTON))
    }
}

/// Keyboard modifier keys.
#[derive(Debug, Default, PartialEq, Clone, Copy)]
pub struct Modifiers {
    /// A shift key is down.
    pub shift: bool,
    /// An alt or option key is down.
    pub alt: bool,
    /// A control key is down.
    pub control: bool,
    /// A caps lock key is down.
    pub caps_lock: bool,
}

impl Modifiers {
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

    pub(crate) fn is_modifier(key: &fidl_fuchsia_input::Key) -> bool {
        match key {
            fidl_fuchsia_input::Key::LeftShift
            | fidl_fuchsia_input::Key::RightShift
            | fidl_fuchsia_input::Key::LeftAlt
            | fidl_fuchsia_input::Key::RightAlt
            | fidl_fuchsia_input::Key::LeftCtrl
            | fidl_fuchsia_input::Key::RightCtrl
            | fidl_fuchsia_input::Key::CapsLock => true,
            _ => false,
        }
    }
}

/// Mouse-related items
pub mod mouse {
    use super::*;
    use crate::geometry::IntVector;

    /// Phase of a mouse event.
    #[derive(Debug, PartialEq, Clone)]
    pub enum Phase {
        /// A particular button went down.
        Down(Button),
        /// A particular button came up.
        Up(Button),
        /// The mouse moved, with or without a change in button state.
        Moved,
        /// The mouse wheel changed position.
        Wheel(IntVector),
    }

    /// A mouse event.
    #[derive(Debug, PartialEq, Clone)]
    pub struct Event {
        /// Pressed buttons.
        pub buttons: ButtonSet,
        /// Event phase.
        pub phase: Phase,
        /// Location of the mouse cursor during this event.
        pub location: IntPoint,
    }

    pub(crate) fn create_event(
        event_time: u64,
        device_id: &DeviceId,
        button_set: &ButtonSet,
        cursor_position: IntPoint,
        transform: &Transform2D<f32>,
        phase: mouse::Phase,
    ) -> super::Event {
        let cursor_position = transform.transform_point(cursor_position.to_f32()).to_i32();
        let mouse_event =
            mouse::Event { buttons: button_set.clone(), phase, location: cursor_position };
        super::Event {
            event_time,
            device_id: device_id.clone(),
            event_type: EventType::Mouse(mouse_event),
        }
    }
}

/// Keyboard-related items.
pub mod keyboard {
    use super::*;

    /// Phase of a keyboard event.
    #[derive(Clone, Copy, Debug, PartialEq)]
    pub enum Phase {
        /// A key is pressed.
        Pressed,
        /// A key is released.
        Released,
        /// A key is no longer pressed without being released.
        Cancelled,
        /// A key has been held down long enough to start repeating.
        Repeat,
    }

    /// A keyboard event.
    #[derive(Debug, PartialEq, Clone)]
    pub struct Event {
        /// Event phase.
        pub phase: Phase,
        /// Unicode code point of the key causing the event, if any.
        pub code_point: Option<u32>,
        /// USB HID usage of the key causing the event.
        pub hid_usage: u32,
        /// Modifier keys being pressed or held in addition to the key
        /// causing the event.
        pub modifiers: Modifiers,
    }
}

/// Touch-related items.
pub mod touch {
    use super::*;

    #[derive(Debug, Eq)]
    pub(crate) struct RawContact {
        pub contact_id: u32,
        pub position: IntPoint,
        // TODO(fxbug.dev/84729)
        #[allow(unused)]
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

    /// ID of a touch contact.
    #[derive(Clone, Copy, Debug, Eq, Ord, PartialOrd, PartialEq, Hash)]
    pub struct ContactId(pub u32);

    /// Phase of a touch event.
    #[derive(Debug, PartialEq, Clone)]
    pub enum Phase {
        /// A contact began.
        Down(IntPoint, IntSize),
        /// A contact moved.
        Moved(IntPoint, IntSize),
        /// A contact ended.
        Up,
        /// A contact was removed.
        Remove,
        /// A contact was cancelled.
        Cancel,
    }

    /// A single contact found in a touch event.
    #[derive(Debug, Clone, PartialEq)]
    pub struct Contact {
        /// ID of this contact
        pub contact_id: ContactId,
        /// Phase of this contact
        pub phase: Phase,
    }

    /// A touch event.
    #[derive(Debug, PartialEq, Clone)]
    pub struct Event {
        /// All the current contact in this event
        pub contacts: Vec<Contact>,
        /// Buttons in this touch event, possible if the touch comes
        /// from a stylus with buttons.
        pub buttons: ButtonSet,
    }
}

/// Pointer event
///
/// Carnelian provides a least-common-denominator pointer event that can be created from
/// either touch events or mouse events.
pub mod pointer {
    use super::*;

    /// Pointer phase.
    #[derive(Debug, PartialEq, Clone)]
    pub enum Phase {
        /// A pointer has gone down.
        Down(IntPoint),
        /// A pointer has moved.
        Moved(IntPoint),
        /// A pointer has come up.
        Up,
        /// A pointer has been removed without coming up.
        Remove,
        /// A pointer has been cancelled.
        Cancel,
    }

    /// Pointer ID.
    #[derive(Clone, Debug, Eq, Ord, PartialOrd, PartialEq, Hash)]
    pub enum PointerId {
        /// ID from a mouse event.
        Mouse(DeviceId),
        /// ID from a contact in a touch event.
        Contact(touch::ContactId),
    }

    /// Pointer event.
    #[derive(Debug, PartialEq, Clone)]
    pub struct Event {
        /// Pointer event phase.
        pub phase: Phase,
        /// Pointer event pointer ID.
        pub pointer_id: PointerId,
    }

    impl Event {
        /// Create a pointer event from a mouse event.
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
                mouse::Phase::Wheel(_) => None,
            }
            .and_then(|phase| Some(Self { phase, pointer_id: PointerId::Mouse(device_id.clone()) }))
        }

        /// Create a pointer event from a single contact in a touch event.
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

/// Events related to "consumer control" buttons, like volume controls.
///
/// These events are separated because they are different devices at the driver
/// level, but it's not clear this is the right abstraction for Carnelian.
pub mod consumer_control {
    use super::*;

    /// Phase of a consumer control event.
    #[derive(Debug, PartialEq, Clone, Copy)]
    pub enum Phase {
        /// Button went down.
        Down,
        /// Button came up.
        Up,
    }

    /// A consumer control event.
    #[derive(Debug, PartialEq, Clone)]
    pub struct Event {
        /// Phase of event.
        pub phase: Phase,
        /// USB HID for key being pressed or released.
        pub button: hid_input_report::ConsumerControlButton,
    }
}

/// Unique identifier for an input device.
#[derive(Clone, Debug, Default, Eq, PartialEq, Hash, PartialOrd, Ord)]
pub struct DeviceId(pub String);

/// Enum of all supported user-input events.
#[derive(Debug, PartialEq, Clone)]
pub enum EventType {
    /// Mouse event.
    Mouse(mouse::Event),
    /// Keyboard event.
    Keyboard(keyboard::Event),
    /// Touch event.
    Touch(touch::Event),
    /// Consumer control event.
    ConsumerControl(consumer_control::Event),
}

/// Over user-input struct.
#[derive(Debug, PartialEq, Clone)]
pub struct Event {
    /// Time of event.
    pub event_time: u64,
    /// Id of device producting this event.
    pub device_id: DeviceId,
    /// The event.
    pub event_type: EventType,
}

fn device_id_for_event(event: &fidl_fuchsia_ui_input::PointerEvent) -> DeviceId {
    #[allow(unreachable_patterns)]
    let id_string = match event.type_ {
        fidl_fuchsia_ui_input::PointerEventType::Touch => "touch",
        fidl_fuchsia_ui_input::PointerEventType::Mouse => "mouse",
        fidl_fuchsia_ui_input::PointerEventType::Stylus => "stylus",
        fidl_fuchsia_ui_input::PointerEventType::InvertedStylus => "inverted-stylus",
        _ => {
            // If you see this log line, it means that the list of pointer event
            // types has been expanded and needs to be added above.
            fx_log_warn!("unknown pointer event type");
            "unknown"
        }
    };
    DeviceId(format!("{}-{}", id_string, event.device_id))
}

async fn listen_to_path(device_path: &Path, internal_sender: &InternalSender) -> Result<(), Error> {
    let (client, server) = zx::Channel::create()?;
    fdio::service_connect(device_path.to_str().expect("bad path"), server)?;
    let client = fasync::Channel::from_channel(client)?;
    let device = hid_input_report::InputDeviceProxy::new(client);
    let descriptor = device
        .get_descriptor()
        .map_err(|err| format_err!("FIDL error on get_descriptor: {:?}", err))
        .on_timeout(Time::after(Duration::from_millis(200)), || {
            Err(format_err!("FIDL timeout on get_descriptor"))
        })
        .await?;
    let device_id = device_path.file_name().expect("file_name").to_string_lossy().to_string();
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
                                    report,
                                ))
                                .expect("unbounded_send");
                        }
                    }
                    Err(err) => {
                        eprintln!("Error report from read_input_reports: {}: {}", device_id, err);
                        break;
                    }
                },
                Err(err) => {
                    eprintln!("Error report from read_input_reports: {}: {}", device_id, err);
                    break;
                }
            }
        }
    })
    .detach();
    Ok(())
}

pub(crate) async fn listen_for_user_input(internal_sender: InternalSender) -> Result<(), Error> {
    let input_devices_directory = "/dev/class/input-report";
    let watcher_sender = internal_sender.clone();
    let path = std::path::Path::new(input_devices_directory);
    let entries = fs::read_dir(path)?;
    for entry in entries {
        let entry = entry?;
        match listen_to_path(&entry.path(), &internal_sender).await {
            Err(err) => {
                eprintln!("Error: {}: {}", entry.file_name().to_string_lossy().to_string(), err)
            }
            _ => (),
        }
    }
    let dir_proxy = fuchsia_fs::directory::open_in_namespace(
        input_devices_directory,
        OpenFlags::RIGHT_READABLE,
    )?;
    let mut watcher = vfs_watcher::Watcher::new(dir_proxy).await?;
    fasync::Task::local(async move {
        let input_devices_directory_path = PathBuf::from("/dev/class/input-report");
        while let Some(msg) = (watcher.try_next()).await.expect("msg") {
            match msg.event {
                vfs_watcher::WatchEvent::ADD_FILE => {
                    let device_path = input_devices_directory_path.join(msg.filename);
                    match listen_to_path(&device_path, &watcher_sender).await {
                        Err(err) => {
                            eprintln!("Error: {:?}: {}", device_path, err)
                        }
                        _ => (),
                    };
                }
                _ => (),
            }
        }
    })
    .detach();

    Ok(())
}

pub(crate) mod flatland;
pub(crate) mod key3;
pub(crate) mod report;
pub(crate) mod scenic;

#[cfg(test)]
mod tests;
