// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device::{self, InputDeviceBinding, InputEvent},
    anyhow::{format_err, Error},
    async_trait::async_trait,
    fidl_fuchsia_input_report as fidl,
    fidl_fuchsia_input_report::{InputDeviceProxy, InputReport},
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::channel::mpsc::{Receiver, Sender},
    futures::StreamExt,
    std::collections::HashSet,
    std::hash::Hash,
    std::hash::Hasher,
    std::iter::FromIterator,
};

#[derive(Copy, Clone, Debug, PartialEq)]
pub struct TouchEvent {
    /// Unique identifier per touch device binding for this contact. Can be used to compare touches across reports.
    pub contact_id: u32,

    /// A contact's position on the x axis of the touch device in 10^-6 meter units.
    pub position_x: i64,

    /// A contact's position on the y axis of the touch device in 10^-6 meter units.
    pub position_y: i64,

    /// Pressure of the contact measured in units of 10^-3 Pascal.
    pub pressure: Option<i64>,

    /// Width of the bounding box around the touch contact. Combined with
    /// `contact_height`, this describes the area of the touch contact.
    /// `contact_width` and `contact_height` should both have units of distance,
    /// and they should be in the same units as `position_x` and `position_y`.
    pub contact_width: Option<i64>,

    /// Height of the bounding box around the touch contact. Combined with
    /// `contact_width`, this describes the area of the touch contact.
    /// `contact_width` and `contact_height` should both have units of distance,
    /// and they should be in the same units as `position_x` and `position_y`.
    pub contact_height: Option<i64>,

    /// The phase of the contact associated with this input event.
    pub phase: fidl_fuchsia_ui_input::PointerEventPhase,
}

#[derive(Copy, Clone, Debug, PartialEq)]
pub struct TouchDeviceDescriptor {
    /// The id of the connected touch input device.
    pub device_id: u32,
}

/// A [`TouchBinding`] represents a connection to a touch input device.
///
/// The [`TouchBinding`] parses and exposes touch descriptor properties (e.g., the range of
/// possible x values for touch contacts) for the device it is associated with.
/// It also parses [`InputReport`]s from the device, and sends them to clients
/// via [`TouchBinding::input_event_stream()`].
///
/// # Example
/// ```
/// let mut touch_device: TouchBinding = input_device::InputDeviceBinding::new().await?;
///
/// while let Some(report) = touch_device.input_event_stream().next().await {}
/// ```
pub struct TouchBinding {
    /// The channel to stream InputEvents to.
    event_sender: Sender<InputEvent>,

    /// The receiving end of the input event channel. Clients use this indirectly via
    /// [`input_event_stream()`].
    event_receiver: Receiver<InputEvent>,

    /// Holds information about this device.
    device_descriptor: TouchDeviceDescriptor,
}

/// A [`ContactDeviceDescriptor`] describes the possible values touch contact properties can take on.
///
/// This descriptor can be used, for example, to determine where on a screen a touch made contact.
///
/// # Example
///
/// ```
/// // Determine the scaling factor between the display and the touch device's x range.
/// let scaling_factor =
///     display_width / (contact_descriptor._x_range.end - contact_descriptor._x_range.start);
/// // Use the scaling factor to scale the contact report's x position.
/// let hit_location = scaling_factor * contact_report.position_x;
/// ```
#[derive(Clone)]
pub struct ContactDeviceDescriptor {
    /// The range of possible x values for this touch contact.
    _x_range: fidl::Range,

    /// The range of possible y values for this touch contact.
    _y_range: fidl::Range,

    /// The range of possible pressure values for this touch contact.
    _pressure_range: Option<fidl::Range>,

    /// The range of possible widths for this touch contact.
    _width_range: Option<fidl::Range>,

    /// The range of possible heights for this touch contact.
    _height_range: Option<fidl::Range>,
}

#[derive(Clone, Copy)]
struct CustomContact {
    pub contact_id: u32,
    pub position_x: i64,
    pub position_y: i64,
    pub pressure: Option<i64>,
    pub contact_width: Option<i64>,
    pub contact_height: Option<i64>,
}

impl Hash for CustomContact {
    fn hash<S: Hasher>(&self, state: &mut S) {
        self.contact_id.hash(state);
    }
}

impl Eq for CustomContact {}

impl PartialEq for CustomContact {
    fn eq(&self, other: &CustomContact) -> bool {
        self.contact_id == other.contact_id
    }
}

impl From<&fidl_fuchsia_input_report::ContactInputReport> for CustomContact {
    fn from(fidl_contact: &fidl_fuchsia_input_report::ContactInputReport) -> CustomContact {
        CustomContact {
            contact_id: fidl_contact.contact_id.unwrap(),
            position_x: fidl_contact.position_x.unwrap(),
            position_y: fidl_contact.position_y.unwrap(),
            pressure: fidl_contact.pressure,
            contact_width: fidl_contact.contact_width,
            contact_height: fidl_contact.contact_height,
        }
    }
}

#[async_trait]
impl input_device::InputDeviceBinding for TouchBinding {
    fn input_event_sender(&self) -> Sender<InputEvent> {
        self.event_sender.clone()
    }

    fn input_event_stream(&mut self) -> &mut Receiver<InputEvent> {
        return &mut self.event_receiver;
    }

    fn get_device_descriptor(&self) -> input_device::InputDeviceDescriptor {
        input_device::InputDeviceDescriptor::Touch(self.device_descriptor.clone())
    }

    async fn any_input_device() -> Result<InputDeviceProxy, Error> {
        let mut devices = Self::all_devices().await?;
        devices.pop().ok_or(format_err!("Couldn't find a default touch device."))
    }

    async fn all_devices() -> Result<Vec<InputDeviceProxy>, Error> {
        input_device::all_devices(input_device::InputDeviceType::Touch).await
    }

    fn process_reports(
        report: InputReport,
        previous_report: Option<InputReport>,
        device_descriptor: &input_device::InputDeviceDescriptor,
        input_event_sender: &mut Sender<InputEvent>,
    ) -> Option<InputReport> {
        // fail early if not a touch report
        let touch_report: &fidl_fuchsia_input_report::TouchInputReport = match &report.touch {
            Some(touch) => touch,
            None => {
                fx_log_err!("Not processing non-touch report.");
                return previous_report;
            }
        };

        let empty_vec = vec![];
        let mut previous_contacts = Vec::<CustomContact>::new();
        for contact in previous_report
            .as_ref()
            .and_then(|unwrapped_report| unwrapped_report.touch.as_ref())
            .and_then(|unwrapped_touch| unwrapped_touch.contacts.as_ref())
            .unwrap_or(&empty_vec)
        {
            let contact_candidate: &fidl_fuchsia_input_report::ContactInputReport = contact.into();
            if valid_contact(
                contact_candidate.contact_id,
                contact_candidate.position_x,
                contact_candidate.position_y,
            ) {
                previous_contacts.push(contact_candidate.into());
            }
        }

        let mut current_contacts = Vec::<CustomContact>::new();
        for contact in touch_report.contacts.as_ref().unwrap_or(&empty_vec) {
            let contact_candidate: &fidl_fuchsia_input_report::ContactInputReport = contact.into();
            if valid_contact(
                contact_candidate.contact_id,
                contact_candidate.position_x,
                contact_candidate.position_y,
            ) {
                current_contacts.push(contact_candidate.into());
            }
        }

        let current_contacts_set: HashSet<CustomContact> =
            HashSet::from_iter(current_contacts.iter().cloned());
        let previous_contacts_set: HashSet<CustomContact> =
            HashSet::from_iter(previous_contacts.iter().cloned());

        let moved_contacts: HashSet<_> =
            current_contacts_set.intersection(&previous_contacts_set).collect();
        let added_contacts: HashSet<_> =
            current_contacts_set.difference(&previous_contacts_set).collect();
        let removed_contacts: HashSet<_> =
            previous_contacts_set.difference(&current_contacts_set).collect();

        for contact in moved_contacts {
            send_touch_event(
                contact.contact_id,
                contact.position_x,
                contact.position_y,
                contact.pressure,
                contact.contact_width,
                contact.contact_height,
                fidl_fuchsia_ui_input::PointerEventPhase::Move,
                device_descriptor.clone(),
                input_event_sender,
            );
        }

        for contact in added_contacts {
            send_touch_event(
                contact.contact_id,
                contact.position_x,
                contact.position_y,
                contact.pressure,
                contact.contact_width,
                contact.contact_height,
                fidl_fuchsia_ui_input::PointerEventPhase::Add,
                device_descriptor.clone(),
                input_event_sender,
            );
            send_touch_event(
                contact.contact_id,
                contact.position_x,
                contact.position_y,
                contact.pressure,
                contact.contact_width,
                contact.contact_height,
                fidl_fuchsia_ui_input::PointerEventPhase::Down,
                device_descriptor.clone(),
                input_event_sender,
            );
        }

        for contact in removed_contacts {
            send_touch_event(
                contact.contact_id,
                contact.position_x,
                contact.position_y,
                contact.pressure,
                contact.contact_width,
                contact.contact_height,
                fidl_fuchsia_ui_input::PointerEventPhase::Up,
                device_descriptor.clone(),
                input_event_sender,
            );
            send_touch_event(
                contact.contact_id,
                contact.position_x,
                contact.position_y,
                contact.pressure,
                contact.contact_width,
                contact.contact_height,
                fidl_fuchsia_ui_input::PointerEventPhase::Remove,
                device_descriptor.clone(),
                input_event_sender,
            );
        }
        Some(report)
    }

    async fn bind_device(device: &InputDeviceProxy) -> Result<Self, Error> {
        let device_descriptor: fidl_fuchsia_input_report::DeviceDescriptor =
            device.get_descriptor().await?;
        match device_descriptor.touch {
            Some(fidl_fuchsia_input_report::TouchDescriptor {
                input:
                    Some(fidl_fuchsia_input_report::TouchInputDescriptor {
                        contacts: Some(_contacts),
                        max_contacts: _,
                        touch_type: _,
                    }),
            }) => {
                let (event_sender, event_receiver) =
                    futures::channel::mpsc::channel(input_device::INPUT_EVENT_BUFFER_SIZE);
                let device_id = match device_descriptor.device_info {
                    Some(info) => info.product_id,
                    None => {
                        return Err(format_err!("Touch Descriptor doesn't contain a product id."))
                    }
                };

                Ok(TouchBinding {
                    event_sender,
                    event_receiver,
                    device_descriptor: TouchDeviceDescriptor { device_id },
                })
            }
            descriptor => Err(format_err!("Touch Descriptor failed to parse: \n {:?}", descriptor)),
        }
    }
}

/// Sends a TouchEvent over `sender`.
///
/// # Parameters
/// - `contact_id`: An identifier for this contact.
/// - `position_x`: The position of the contact on the x axis.
/// - `position_y`: The position of the contact on the y axis.
/// - `pressure`: The pressure of the contact.
/// - `contact_width`: Width of the area of contact.
/// - `contact_height`: Height of the area of contact.
/// - `phase`: The phase of the contact associated with the input event.
/// - `sender`: The stream to send the TouchEvent to.
fn send_touch_event(
    contact_id: u32,
    position_x: i64,
    position_y: i64,
    pressure: Option<i64>,
    contact_width: Option<i64>,
    contact_height: Option<i64>,
    phase: fidl_fuchsia_ui_input::PointerEventPhase,
    device_descriptor: input_device::InputDeviceDescriptor,
    sender: &mut Sender<input_device::InputEvent>,
) {
    match sender.try_send(input_device::InputEvent {
        device_event: input_device::InputDeviceEvent::Touch(TouchEvent {
            contact_id,
            position_x,
            position_y,
            pressure,
            contact_width,
            contact_height,
            phase,
        }),
        device_descriptor: device_descriptor.clone(),
    }) {
        Err(e) => fx_log_err!("Failed to send TouchEvent with error: {:?}", e),
        _ => {}
    }
}

impl TouchBinding {
    /// Parses a FIDL contact descriptor into a [`ContactDeviceDescriptor`]
    ///
    /// # Parameters
    /// - `contact_device_descriptor`: The contact descriptor to parse.
    ///
    /// # Errors
    /// If the contact description fails to parse because required fields aren't present.
    #[allow(dead_code)]
    fn parse_contact_descriptor(
        contact_device_descriptor: &fidl::ContactInputDescriptor,
    ) -> Result<ContactDeviceDescriptor, Error> {
        match contact_device_descriptor {
            fidl::ContactInputDescriptor {
                position_x: Some(x_axis),
                position_y: Some(y_axis),
                pressure: pressure_axis,
                contact_width: width_axis,
                contact_height: height_axis,
            } => Ok(ContactDeviceDescriptor {
                _x_range: x_axis.range,
                _y_range: y_axis.range,
                _pressure_range: Some(pressure_axis.unwrap().range), // Unwrap may be unsafe here
                _width_range: Some(width_axis.unwrap().range),       // Unwrap may be unsafe here
                _height_range: Some(height_axis.unwrap().range),     // Unwrap may be unsafe here
            }),
            descriptor => {
                Err(format_err!("Touch Contact Descriptor failed to parse: \n {:?}", descriptor))
            }
        }
    }
}

/// Returns a vector of [`TouchBindings`] for all currently connected touch devices.
///
/// # Errors
/// If there was an error binding to any touch device.
async fn all_touch_bindings() -> Result<Vec<TouchBinding>, Error> {
    let device_proxies = input_device::all_devices(input_device::InputDeviceType::Touch).await?;
    let mut device_bindings: Vec<TouchBinding> = vec![];

    for device_proxy in device_proxies {
        let device_binding: TouchBinding =
            input_device::InputDeviceBinding::new(device_proxy).await?;
        device_bindings.push(device_binding);
    }

    Ok(device_bindings)
}

/// Returns a stream of InputEvents from all touch devices.
///
/// # Errors
/// If there was an error binding to any touch device.
pub async fn all_touch_events() -> Result<Receiver<InputEvent>, Error> {
    let bindings = all_touch_bindings().await?;
    let (event_sender, event_receiver) =
        futures::channel::mpsc::channel(input_device::INPUT_EVENT_BUFFER_SIZE);

    for mut touch in bindings {
        let mut sender = event_sender.clone();
        fasync::spawn(async move {
            while let Some(input_event) = touch.input_event_stream().next().await {
                let _ = sender.try_send(input_event);
            }
        });
    }

    Ok(event_receiver)
}

/// Returns a bool if the contact has a valid id and position.
///
/// #Parameters
/// - `contact_id`: An identifier for this contact.
/// - `position_x`: The position of the contact on the x axis.
/// - `position_y`: The position of the contact on the y axis.
fn valid_contact(
    contact_id: Option<u32>,
    position_x: Option<i64>,
    position_y: Option<i64>,
) -> bool {
    contact_id.is_some() && position_x.is_some() && position_y.is_some()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing_utilities;

    // Tests that a Move InputEvent is sent for a moved touch.
    #[fasync::run_singlethreaded(test)]
    async fn move_input_message() {
        const PREV_VALUE: i64 = 0;
        const CUR_VALUE: i64 = 1;
        const TOUCH_ID: u32 = 2;

        let descriptor =
            input_device::InputDeviceDescriptor::Touch(TouchDeviceDescriptor { device_id: 1 });

        let contact = fidl_fuchsia_input_report::ContactInputReport {
            contact_id: Some(TOUCH_ID),
            position_x: Some(PREV_VALUE),
            position_y: Some(PREV_VALUE),
            pressure: None,
            contact_width: None,
            contact_height: None,
        };

        let contact_moved = fidl_fuchsia_input_report::ContactInputReport {
            contact_id: Some(TOUCH_ID),
            position_x: Some(CUR_VALUE),
            position_y: Some(CUR_VALUE),
            pressure: None,
            contact_width: None,
            contact_height: None,
        };

        let first_report = testing_utilities::create_touch_input_report(vec![contact]);
        let second_report = testing_utilities::create_touch_input_report(vec![contact_moved]);
        let reports = vec![first_report, second_report];
        let expected_events = vec![
            testing_utilities::create_touch_event(
                TOUCH_ID,
                PREV_VALUE,
                PREV_VALUE,
                fidl_fuchsia_ui_input::PointerEventPhase::Add,
                &descriptor,
            ),
            testing_utilities::create_touch_event(
                TOUCH_ID,
                PREV_VALUE,
                PREV_VALUE,
                fidl_fuchsia_ui_input::PointerEventPhase::Down,
                &descriptor,
            ),
            testing_utilities::create_touch_event(
                TOUCH_ID,
                CUR_VALUE,
                CUR_VALUE,
                fidl_fuchsia_ui_input::PointerEventPhase::Move,
                &descriptor,
            ),
        ];

        assert_input_report_sequence_generates_events!(
            input_reports: reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: TouchBinding,
        );
    }

    // Tests that Up and Remove InputEvents are sent when a touch is released.
    #[fasync::run_singlethreaded(test)]
    async fn up_input_message() {
        const CUR_VALUE: i64 = 0;
        const TOUCH_ID: u32 = 1;

        let contact = fidl_fuchsia_input_report::ContactInputReport {
            contact_id: Some(TOUCH_ID),
            position_x: Some(CUR_VALUE),
            position_y: Some(CUR_VALUE),
            pressure: None,
            contact_width: None,
            contact_height: None,
        };
        let first_report = testing_utilities::create_touch_input_report(vec![contact]);
        let second_report = testing_utilities::create_touch_input_report(vec![]);

        let descriptor =
            input_device::InputDeviceDescriptor::Touch(TouchDeviceDescriptor { device_id: 1 });
        let reports = vec![first_report, second_report];
        let expected_events = vec![
            testing_utilities::create_touch_event(
                TOUCH_ID,
                CUR_VALUE,
                CUR_VALUE,
                fidl_fuchsia_ui_input::PointerEventPhase::Add,
                &descriptor,
            ),
            testing_utilities::create_touch_event(
                TOUCH_ID,
                CUR_VALUE,
                CUR_VALUE,
                fidl_fuchsia_ui_input::PointerEventPhase::Down,
                &descriptor,
            ),
            testing_utilities::create_touch_event(
                TOUCH_ID,
                CUR_VALUE,
                CUR_VALUE,
                fidl_fuchsia_ui_input::PointerEventPhase::Up,
                &descriptor,
            ),
            testing_utilities::create_touch_event(
                TOUCH_ID,
                CUR_VALUE,
                CUR_VALUE,
                fidl_fuchsia_ui_input::PointerEventPhase::Remove,
                &descriptor,
            ),
        ];

        assert_input_report_sequence_generates_events!(
            input_reports: reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: TouchBinding,
        );
    }

    // Tests that two InputEvents are sent when a touch is pressed.
    #[fasync::run_singlethreaded(test)]
    async fn down_input_event() {
        const CUR_VALUE: i64 = 0;
        const TOUCH_ID: u32 = 0;

        let contact = fidl_fuchsia_input_report::ContactInputReport {
            contact_id: Some(TOUCH_ID),
            position_x: Some(CUR_VALUE),
            position_y: Some(CUR_VALUE),
            pressure: None,
            contact_width: None,
            contact_height: None,
        };
        let descriptor =
            input_device::InputDeviceDescriptor::Touch(TouchDeviceDescriptor { device_id: 1 });
        let reports = vec![testing_utilities::create_touch_input_report(vec![contact])];

        let expected_events = vec![
            testing_utilities::create_touch_event(
                TOUCH_ID,
                CUR_VALUE,
                CUR_VALUE,
                fidl_fuchsia_ui_input::PointerEventPhase::Add,
                &descriptor,
            ),
            testing_utilities::create_touch_event(
                TOUCH_ID,
                CUR_VALUE,
                CUR_VALUE,
                fidl_fuchsia_ui_input::PointerEventPhase::Down,
                &descriptor,
            ),
        ];

        assert_input_report_sequence_generates_events!(
            input_reports: reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: TouchBinding,
        );
    }

    // Tests that the contact ID, position_x, and position_y are all properly set.
    #[fasync::run_singlethreaded(test)]
    async fn valid_contact_id_input_message() {
        const X_VALUE: i64 = 0;
        const Y_VALUE: i64 = 1;
        const TOUCH_ID: u32 = 2;

        let contact = fidl_fuchsia_input_report::ContactInputReport {
            contact_id: Some(TOUCH_ID),
            position_x: Some(X_VALUE),
            position_y: Some(Y_VALUE),
            pressure: None,
            contact_width: None,
            contact_height: None,
        };

        let descriptor =
            input_device::InputDeviceDescriptor::Touch(TouchDeviceDescriptor { device_id: 1 });
        let reports = vec![testing_utilities::create_touch_input_report(vec![contact])];

        let expected_events = vec![
            testing_utilities::create_touch_event(
                TOUCH_ID,
                X_VALUE,
                Y_VALUE,
                fidl_fuchsia_ui_input::PointerEventPhase::Add,
                &descriptor,
            ),
            testing_utilities::create_touch_event(
                TOUCH_ID,
                X_VALUE,
                Y_VALUE,
                fidl_fuchsia_ui_input::PointerEventPhase::Down,
                &descriptor,
            ),
        ];

        assert_input_report_sequence_generates_events!(
            input_reports: reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: TouchBinding,
        );
    }
}
