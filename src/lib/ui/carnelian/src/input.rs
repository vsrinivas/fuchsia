// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app::{InternalSender, MessageInternal},
    geometry::{IntPoint, IntSize},
    view::ViewKey,
};
use anyhow::{format_err, Error};
use fidl::endpoints::create_proxy;
use fidl_fuchsia_input_report as hid_input_report;
use fuchsia_async::{self as fasync, Time, TimeoutExt};
use fuchsia_vfs_watcher as vfs_watcher;
use fuchsia_zircon::{self as zx, Duration};
use futures::{TryFutureExt, TryStreamExt};
use io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE};
use keymaps::usages::input3_key_to_hid_usage;
use std::{
    collections::HashSet,
    fs,
    hash::{Hash, Hasher},
    path::{Path, PathBuf},
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

    #[derive(Clone, Copy, Debug, Eq, Ord, PartialOrd, PartialEq, Hash)]
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

    #[derive(Clone, Debug, Eq, Ord, PartialOrd, PartialEq, Hash)]
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

async fn listen_to_path(
    device_path: &Path,
    view_key: ViewKey,
    internal_sender: &InternalSender,
) -> Result<(), Error> {
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
                                    view_key,
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

pub(crate) async fn listen_for_user_input(
    view_key: ViewKey,
    internal_sender: InternalSender,
) -> Result<(), Error> {
    let input_devices_directory = "/dev/class/input-report";
    let watcher_sender = internal_sender.clone();
    let path = std::path::Path::new(input_devices_directory);
    let entries = fs::read_dir(path)?;
    for entry in entries {
        let entry = entry?;
        match listen_to_path(&entry.path(), view_key, &internal_sender).await {
            Err(err) => {
                eprintln!("Error: {}: {}", entry.file_name().to_string_lossy().to_string(), err)
            }
            _ => (),
        }
    }
    let dir_proxy = open_directory_in_namespace(input_devices_directory, OPEN_RIGHT_READABLE)?;
    let mut watcher = vfs_watcher::Watcher::new(dir_proxy).await?;
    fasync::Task::local(async move {
        let input_devices_directory_path = PathBuf::from("/dev/class/input-report");
        while let Some(msg) = (watcher.try_next()).await.expect("msg") {
            match msg.event {
                vfs_watcher::WatchEvent::ADD_FILE => {
                    let device_path = input_devices_directory_path.join(msg.filename);
                    match listen_to_path(&device_path, view_key, &watcher_sender).await {
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

pub(crate) mod report;
pub(crate) mod scenic;

#[cfg(test)]
mod tests;
