// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device::{self, InputDeviceBinding},
    anyhow::{format_err, Error},
    async_trait::async_trait,
    fidl_fuchsia_input_report as fidl_input_report,
    fidl_fuchsia_input_report::{InputDeviceProxy, InputReport},
    fuchsia_syslog::fx_log_err,
    futures::channel::mpsc::Sender,
};

/// A [`ConsumerControlsEvent`] represents an event where one or more consumer control buttons
/// were pressed.
///
/// # Example
/// The following ConsumerControlsEvents represents an event where the volume up button was pressed.
///
/// ```
/// let volume_event = input_device::InputDeviceEvent::ConsumerControls(ConsumerControlsEvent::new(
///     vec![fidl_input_report::ConsumerControlButton::VOLUME_UP],
/// ));
/// ```
#[derive(Clone, Debug, PartialEq)]
pub struct ConsumerControlsEvent {
    pub pressed_buttons: Vec<fidl_input_report::ConsumerControlButton>,
}

impl ConsumerControlsEvent {
    /// Creates a new [`ConsumerControlsEvent`] with the relevant buttons.
    ///
    /// # Parameters
    /// - `pressed_buttons`: The buttons relevant to this event.
    pub fn new(pressed_buttons: Vec<fidl_input_report::ConsumerControlButton>) -> Self {
        Self { pressed_buttons }
    }
}

/// A [`ConsumerControlsBinding`] represents a connection to a consumer controls input device with
/// consumer controls. The buttons supported by this binding is returned by `supported_buttons()`.
///
/// The [`ConsumerControlsBinding`] parses and exposes consumer control descriptor properties
/// for the device it is associated with. It also parses [`InputReport`]s
/// from the device, and sends them to the device binding owner over `event_sender`.
pub struct ConsumerControlsBinding {
    /// The channel to stream InputEvents to.
    event_sender: Sender<input_device::InputEvent>,

    /// Holds information about this device.
    device_descriptor: ConsumerControlsDeviceDescriptor,
}

#[derive(Clone, Debug, PartialEq)]
pub struct ConsumerControlsDeviceDescriptor {
    /// The list of buttons that this device contains.
    pub buttons: Vec<fidl_input_report::ConsumerControlButton>,
}

#[async_trait]
impl input_device::InputDeviceBinding for ConsumerControlsBinding {
    fn input_event_sender(&self) -> Sender<input_device::InputEvent> {
        self.event_sender.clone()
    }

    fn get_device_descriptor(&self) -> input_device::InputDeviceDescriptor {
        input_device::InputDeviceDescriptor::ConsumerControls(self.device_descriptor.clone())
    }
}

impl ConsumerControlsBinding {
    /// Creates a new [`InputDeviceBinding`] from the `device_proxy`.
    ///
    /// The binding will start listening for input reports immediately and send new InputEvents
    /// to the device binding owner over `input_event_sender`.
    ///
    /// # Parameters
    /// - `device_proxy`: The proxy to bind the new [`InputDeviceBinding`] to.
    /// - `input_event_sender`: The channel to send new InputEvents to.
    ///
    /// # Errors
    /// If there was an error binding to the proxy.
    pub async fn new(
        device_proxy: InputDeviceProxy,
        input_event_sender: Sender<input_device::InputEvent>,
    ) -> Result<Self, Error> {
        let device_binding = Self::bind_device(&device_proxy, input_event_sender).await?;
        input_device::initialize_report_stream(
            device_proxy,
            device_binding.get_device_descriptor(),
            device_binding.input_event_sender(),
            Self::process_reports,
        );

        Ok(device_binding)
    }

    /// Binds the provided input device to a new instance of `Self`.
    ///
    /// # Parameters
    /// - `device`: The device to use to initialize the binding.
    /// - `input_event_sender`: The channel to send new InputEvents to.
    ///
    /// # Errors
    /// If the device descriptor could not be retrieved, or the descriptor could
    /// not be parsed correctly.
    async fn bind_device(
        device: &InputDeviceProxy,
        input_event_sender: Sender<input_device::InputEvent>,
    ) -> Result<Self, Error> {
        let device_descriptor: fidl_input_report::DeviceDescriptor =
            device.get_descriptor().await?;

        let consumer_controls_descriptor = device_descriptor.consumer_control.ok_or_else(|| {
            format_err!("DeviceDescriptor does not have a ConsumerControlDescriptor")
        })?;

        let consumer_controls_input_descriptor =
            consumer_controls_descriptor.input.ok_or_else(|| {
                format_err!(
                    "ConsumerControlDescriptor does not have a ConsumerControlInputDescriptor"
                )
            })?;

        let device_descriptor: ConsumerControlsDeviceDescriptor =
            ConsumerControlsDeviceDescriptor {
                buttons: consumer_controls_input_descriptor.buttons.unwrap_or_default(),
            };

        Ok(ConsumerControlsBinding { event_sender: input_event_sender, device_descriptor })
    }

    /// Parses an [`InputReport`] into one or more [`InputEvent`]s. Sends the [`InputEvent`]s
    /// to the device binding owner via [`input_event_sender`].
    ///
    /// # Parameters
    /// `report`: The incoming [`InputReport`].
    /// `previous_report`: The previous [`InputReport`] seen for the same device. This can be
    ///                    used to determine, for example, which keys are no longer present in
    ///                    a keyboard report to generate key released events. If `None`, no
    ///                    previous report was found.
    /// `device_descriptor`: The descriptor for the input device generating the input reports.
    /// `input_event_sender`: The sender for the device binding's input event stream.
    ///
    /// # Returns
    /// An [`InputReport`] which will be passed to the next call to [`process_reports`], as
    /// [`previous_report`]. If `None`, the next call's [`previous_report`] will be `None`.
    fn process_reports(
        report: InputReport,
        previous_report: Option<InputReport>,
        device_descriptor: &input_device::InputDeviceDescriptor,
        input_event_sender: &mut Sender<input_device::InputEvent>,
    ) -> Option<InputReport> {
        // Input devices can have multiple types so ensure `report` is a ConsumerControlInputReport.
        let pressed_buttons: Vec<fidl_input_report::ConsumerControlButton> =
            match report.consumer_control {
                Some(ref consumer_control_report) => consumer_control_report
                    .pressed_buttons
                    .as_ref()
                    .map(|buttons| buttons.iter().cloned().collect())
                    .unwrap_or_default(),
                None => return previous_report,
            };

        let event_time: input_device::EventTime =
            input_device::event_time_or_now(report.event_time);

        send_consumer_controls_event(
            pressed_buttons,
            device_descriptor,
            event_time,
            input_event_sender,
        );

        Some(report)
    }
}

/// Sends an InputEvent over `sender`.
///
/// # Parameters
/// - `pressed_buttons`: The buttons relevant to the event.
/// - `device_descriptor`: The descriptor for the input device generating the input reports.
/// - `event_time`: The time in nanoseconds when the event was first recorded.
/// - `sender`: The stream to send the InputEvent to.
fn send_consumer_controls_event(
    pressed_buttons: Vec<fidl_input_report::ConsumerControlButton>,
    device_descriptor: &input_device::InputDeviceDescriptor,
    event_time: input_device::EventTime,
    sender: &mut Sender<input_device::InputEvent>,
) {
    if let Err(e) = sender.try_send(input_device::InputEvent {
        device_event: input_device::InputDeviceEvent::ConsumerControls(ConsumerControlsEvent::new(
            pressed_buttons,
        )),
        device_descriptor: device_descriptor.clone(),
        event_time,
    }) {
        fx_log_err!("Failed to send ConsumerControlsEvent with error: {:?}", e);
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::testing_utilities, fuchsia_async as fasync, futures::StreamExt};

    // Tests that an InputReport containing one consumer control button generates an InputEvent
    // containing the same consumer control button.
    #[fasync::run_singlethreaded(test)]
    async fn volume_up_only() {
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let pressed_buttons = vec![fidl_input_report::ConsumerControlButton::VolumeUp];
        let first_report = testing_utilities::create_consumer_control_input_report(
            pressed_buttons.clone(),
            event_time_i64,
        );
        let descriptor = testing_utilities::consumer_controls_device_descriptor();

        let input_reports = vec![first_report];
        let expected_events = vec![testing_utilities::create_consumer_controls_event(
            pressed_buttons,
            event_time_u64,
            &descriptor,
        )];

        assert_input_report_sequence_generates_events!(
            input_reports: input_reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: ConsumerControlsBinding,
        );
    }

    // Tests that an InputReport containing two consumer control buttons generates an InputEvent
    // containing both consumer control buttons.
    #[fasync::run_singlethreaded(test)]
    async fn volume_up_and_down() {
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let pressed_buttons = vec![
            fidl_input_report::ConsumerControlButton::VolumeUp,
            fidl_input_report::ConsumerControlButton::VolumeDown,
        ];
        let first_report = testing_utilities::create_consumer_control_input_report(
            pressed_buttons.clone(),
            event_time_i64,
        );
        let descriptor = testing_utilities::consumer_controls_device_descriptor();

        let input_reports = vec![first_report];
        let expected_events = vec![testing_utilities::create_consumer_controls_event(
            pressed_buttons,
            event_time_u64,
            &descriptor,
        )];

        assert_input_report_sequence_generates_events!(
            input_reports: input_reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: ConsumerControlsBinding,
        );
    }

    // Tests that three InputReports containing one consumer control button generates three
    // InputEvents containing the same consumer control button.
    #[fasync::run_singlethreaded(test)]
    async fn sequence_of_buttons() {
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let first_report = testing_utilities::create_consumer_control_input_report(
            vec![fidl_input_report::ConsumerControlButton::VolumeUp],
            event_time_i64,
        );
        let second_report = testing_utilities::create_consumer_control_input_report(
            vec![fidl_input_report::ConsumerControlButton::VolumeDown],
            event_time_i64,
        );
        let third_report = testing_utilities::create_consumer_control_input_report(
            vec![fidl_input_report::ConsumerControlButton::CameraDisable],
            event_time_i64,
        );
        let descriptor = testing_utilities::consumer_controls_device_descriptor();

        let input_reports = vec![first_report, second_report, third_report];
        let expected_events = vec![
            testing_utilities::create_consumer_controls_event(
                vec![fidl_input_report::ConsumerControlButton::VolumeUp],
                event_time_u64,
                &descriptor,
            ),
            testing_utilities::create_consumer_controls_event(
                vec![fidl_input_report::ConsumerControlButton::VolumeDown],
                event_time_u64,
                &descriptor,
            ),
            testing_utilities::create_consumer_controls_event(
                vec![fidl_input_report::ConsumerControlButton::CameraDisable],
                event_time_u64,
                &descriptor,
            ),
        ];

        assert_input_report_sequence_generates_events!(
            input_reports: input_reports,
            expected_events: expected_events,
            device_descriptor: descriptor,
            device_type: ConsumerControlsBinding,
        );
    }
}
