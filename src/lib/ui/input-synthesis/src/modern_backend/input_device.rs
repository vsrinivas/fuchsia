// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

use {
    crate::{
        modern_backend::input_reports_reader::InputReportsReader, synthesizer,
        usages::hid_usage_to_input3_key,
    },
    anyhow::{format_err, Context as _, Error},
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl::Error as FidlError,
    fidl_fuchsia_input::Key,
    fidl_fuchsia_input_report::{
        ContactInputReport, DeviceDescriptor, InputDeviceRequest, InputDeviceRequestStream,
        InputReport, InputReportsReaderMarker, KeyboardInputReport, TouchInputReport,
        TOUCH_MAX_CONTACTS,
    },
    fidl_fuchsia_ui_input::{KeyboardReport, Touch},
    futures::{future, pin_mut, StreamExt, TryFutureExt},
    std::convert::TryFrom as _,
};

/// Implements the `synthesizer::InputDevice` trait, and the server side of the
/// `fuchsia.input.report.InputDevice` FIDL protocol. Used by
/// `modern_backend::InputDeviceRegistry`.
///
/// # Notes
/// * Some of the methods of `fuchsia.input.report.InputDevice` are not relevant to
///   input injection, so this implemnentation does not support them:
///   * `GetFeatureReport` and `SetFeatureReport` are for sensors.
///   * `SendOutputReport` provides a way to change keyboard LED state.
///   If these FIDL methods are invoked, `InputDevice::serve_reports()` will resolve
///   to Err.
/// * This implementation does not support multiple calls to `GetInputReportsReader`,
///   since:
///   * The ideal semantics for multiple calls are not obvious, and
///   * Each `InputDevice` has a single FIDL client (an input pipeline implementation),
///     and the current input pipeline implementation is happy to use a single
///     `InputReportsReader` for the lifetime of the `InputDevice`.
pub(super) struct InputDevice {
    request_stream: InputDeviceRequestStream,
    /// For responding to `fuchsia.input.report.InputDevice.GetDescriptor()` requests.
    descriptor: DeviceDescriptor,
    /// FIFO queue of reports to be consumed by calls to
    /// `fuchsia.input.report.InputReportsReader.ReadInputReports()`.
    /// Populated by calls to `synthesizer::InputDevice` trait methods.
    reports: Vec<InputReport>,
}

#[async_trait(?Send)]
impl synthesizer::InputDevice for self::InputDevice {
    fn media_buttons(
        &mut self,
        _volume_up: bool,
        _volume_down: bool,
        _mic_mute: bool,
        _reset: bool,
        _pause: bool,
        _camera_disable: bool,
        _time: u64,
    ) -> Result<(), Error> {
        Err(format_err!("TODO: implement media_buttons()"))
    }

    // TODO(fxbug.dev/63973): remove dependency on HID usage codes.
    fn key_press(&mut self, report: KeyboardReport, time: u64) -> Result<(), Error> {
        self.key_press_internal(report, time, Self::convert_keyboard_report_to_keys)
    }

    fn key_press_raw(&mut self, report: KeyboardReport, time: u64) -> Result<(), Error> {
        self.key_press_internal(report, time, Self::convert_keyboard_report_to_keys_no_transform)
    }

    // TODO(fxbug.dev/63973): remove reference to HID usage codes.
    fn key_press_usage(&mut self, usage: Option<u32>, time: u64) -> Result<(), Error> {
        self.key_press(KeyboardReport { pressed_keys: usage.into_iter().collect() }, time)
    }

    fn tap(&mut self, pos: Option<(u32, u32)>, time: u64) -> Result<(), Error> {
        let fingers = pos.and_then(|(x, y)| {
            // Note: we use finger_id `1` for consistency with the legacy_backend.
            Some(vec![Touch { finger_id: 1, x: x as i32, y: y as i32, width: 0, height: 0 }])
        });
        self.multi_finger_tap(fingers, time)
    }

    fn multi_finger_tap(&mut self, fingers: Option<Vec<Touch>>, time: u64) -> Result<(), Error> {
        let num_fingers = match &fingers {
            Some(fingers_vec) => fingers_vec.len(),
            None => 0,
        };
        if num_fingers > usize::try_from(TOUCH_MAX_CONTACTS).context("usize is at least 32 bits")? {
            return Err(format_err!(
                "Got {} fingers, but max is {}",
                num_fingers,
                TOUCH_MAX_CONTACTS
            ));
        }
        self.multi_finger_tap_internal(
            TouchInputReport {
                contacts: Some(fingers.map_or_else(Vec::new, |fingers_vec| {
                    fingers_vec
                        .into_iter()
                        .map(|finger| ContactInputReport {
                            contact_id: Some(finger.finger_id),
                            position_x: Some(i64::from(finger.x)),
                            position_y: Some(i64::from(finger.y)),
                            contact_width: Some(i64::from(finger.width)),
                            contact_height: Some(i64::from(finger.height)),
                            ..ContactInputReport::EMPTY
                        })
                        .collect()
                })),
                pressed_buttons: Some(vec![]),
                ..TouchInputReport::EMPTY
            },
            time,
        )
    }

    /// Returns a `Future` which resolves when all `InputReport`s for this device
    /// have been sent to a `fuchsia.input.InputReportsReader` client, or when
    /// an error occurs.
    ///
    /// # Resolves to
    /// * `Ok(())` if all reports were written successfully
    /// * `Err` otherwise. For example:
    ///   * The `fuchsia.input.InputDevice` client sent an invalid request.
    ///   * A FIDL error occurred while trying to read a FIDL request.
    ///   * A FIDL error occurred while trying to write a FIDL response.
    ///
    /// # Corner cases
    /// Resolves to `Err` if the `fuchsia.input.InputDevice` client did not call
    /// `GetInputReportsReader()`, even if no `InputReport`s were queued.
    ///
    /// # Note
    /// When the `Future` resolves, `InputReports` may still be sitting unread in the
    /// channel to the `fuchsia.input.InputReportsReader` client. (The client will
    /// typically be an input pipeline implementation.)
    async fn serve_reports(mut self: Box<Self>) -> Result<(), Error> {
        // Destructure fields into independent variables, to avoid "partial-move" issues.
        let Self { request_stream, descriptor, reports } = *self;

        // Process `fuchsia.input.report.InputDevice` requests, waiting for the `InputDevice`
        // client to provide a `ServerEnd<InputReportsReader>` by calling `GetInputReportsReader()`.
        let mut input_reports_reader_server_end_stream = request_stream
            .filter_map(|r| future::ready(Self::handle_device_request(r, &descriptor)));
        let input_reports_reader_fut = {
            let reader_server_end = input_reports_reader_server_end_stream
                .next()
                .await
                .ok_or(format_err!("stream ended without a call GetInputReportsReader"))?
                .context("handling InputDeviceRequest")?;
            InputReportsReader {
                request_stream: reader_server_end
                    .into_stream()
                    .context("converting ServerEnd<InputReportsReader>")?,
                reports,
            }
            .into_future()
        };
        pin_mut!(input_reports_reader_fut);

        // Create a `Future` to keep serving the `fuchsia.input.report.InputDevice` protocol.
        // This time, receiving a `ServerEnd<InputReportsReaderMarker>` will be an `Err`.
        let input_device_server_fut = async {
            match input_reports_reader_server_end_stream.next().await {
                Some(Ok(_server_end)) => {
                    // There are no obvious "best" semantics for how to handle multiple
                    // `GetInputReportsReader` calls, and there is no current need to
                    // do so. Instead of taking a guess at what the client might want
                    // in such a case, just return `Err`.
                    Err(format_err!(
                        "InputDevice does not support multiple GetInputReportsReader calls"
                    ))
                }
                Some(Err(e)) => Err(e.context("handling InputDeviceRequest")),
                None => Ok(()),
            }
        };
        pin_mut!(input_device_server_fut);

        // Now, process both `fuchsia.input.report.InputDevice` requests, and
        // `fuchsia.input.report.InputReportsReader` requests. And keep processing
        // `InputReportsReader` requests even if the `InputDevice` connection
        // is severed.
        future::select(
            input_device_server_fut.and_then(|_: ()| future::pending()),
            input_reports_reader_fut,
        )
        .await
        .factor_first()
        .0
    }
}

impl InputDevice {
    /// Creates a new `InputDevice` that will:
    /// a) process requests from `request_stream`, and
    /// b) respond to `GetDescriptor` calls with the descriptor generated by `descriptor_generator()`
    ///
    /// The `InputDevice` initially has no reports queued.
    pub(super) fn new(
        request_stream: InputDeviceRequestStream,
        descriptor: DeviceDescriptor,
    ) -> Self {
        Self { request_stream, descriptor, reports: vec![] }
    }

    /// Converts a [KeyboardReport] into a sequence of key presses, using the supplied
    /// key-to-HID usage transformation function.
    fn key_press_internal(
        &mut self,
        report: KeyboardReport,
        time: u64,
        transform: fn(r: &KeyboardReport) -> Result<Vec<Key>, Error>,
    ) -> Result<(), Error> {
        self.reports.push(InputReport {
            event_time: Some(i64::try_from(time).context("converting time to i64")?),
            keyboard: Some(KeyboardInputReport {
                pressed_keys3: Some(transform(&report)?),
                ..KeyboardInputReport::EMPTY
            }),
            ..InputReport::EMPTY
        });
        Ok(())
    }

    fn multi_finger_tap_internal(
        &mut self,
        touch: TouchInputReport,
        time: u64,
    ) -> Result<(), Error> {
        self.reports.push(InputReport {
            event_time: Some(i64::try_from(time).context("converting time to i64")?),
            touch: Some(touch),
            ..InputReport::EMPTY
        });
        Ok(())
    }

    /// Processes a single request from an `InputDeviceRequestStream`
    ///
    /// # Returns
    /// * Some(Ok(ServerEnd<InputReportsReaderMarker>)) if the request yielded an
    ///   `InputReportsReader`. `InputDevice` should route its `InputReports` to the yielded
    ///   `InputReportsReader`.
    /// * Some(Err) if the request yielded an `Error`
    /// * None if the request was fully processed by `handle_device_request()`
    fn handle_device_request(
        request: Result<InputDeviceRequest, FidlError>,
        descriptor: &DeviceDescriptor,
    ) -> Option<Result<ServerEnd<InputReportsReaderMarker>, Error>> {
        match request {
            Ok(InputDeviceRequest::GetInputReportsReader { reader: reader_server_end, .. }) => {
                Some(Ok(reader_server_end))
            }
            Ok(InputDeviceRequest::GetDescriptor { responder }) => {
                match responder.send(descriptor.clone()) {
                    Ok(()) => None,
                    Err(e) => {
                        Some(Err(anyhow::Error::from(e).context("sending GetDescriptor response")))
                    }
                }
            }
            Ok(InputDeviceRequest::SendOutputReport { .. }) => {
                Some(Err(format_err!("InputDevice does not support SendOutputReport")))
            }
            Ok(InputDeviceRequest::GetFeatureReport { .. }) => {
                Some(Err(format_err!("InputDevice does not support GetFeatureReport")))
            }
            Ok(InputDeviceRequest::SetFeatureReport { .. }) => {
                Some(Err(format_err!("InputDevice does not support SetFeatureReport")))
            }
            Ok(InputDeviceRequest::GetInputReport { .. }) => {
                Some(Err(format_err!("InputDevice does not support GetInputReport")))
            }
            Err(e) => Some(Err(anyhow::Error::from(e).context("while reading InputDeviceRequest"))),
        }
    }

    fn convert_keyboard_report_to_keys(report: &KeyboardReport) -> Result<Vec<Key>, Error> {
        report
            .pressed_keys
            .iter()
            .map(|&usage| {
                hid_usage_to_input3_key(usage as u16)
                    .ok_or_else(|| format_err!("no Key for usage {:?}", usage))
            })
            .collect()
    }

    /// Same as convert_keyboard_report_to_keys, but no additional calls to HID usage mapping.
    ///
    /// The keyboard report in `convert_keyboard_report_to_keys` assumes USB HID usage page 7.
    /// This set of keys is narrower than what Fuchsia supports so that function needs to map
    /// back into Fuchsia USB HID encoding (see [fidl_fuchsia_input::Key]).
    ///
    /// This function, in turn, uses the full range of [fidl_fuchsia_input::Key], so does not
    /// need this conversion.
    fn convert_keyboard_report_to_keys_no_transform(
        report: &KeyboardReport,
    ) -> Result<Vec<Key>, Error> {
        report
            .pressed_keys
            .iter()
            .map(|&usage| {
                Key::from_primitive(usage)
                    .ok_or(anyhow::anyhow!("could not convert to input::Key: {}", &usage))
            })
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{synthesizer::InputDevice as _, *},
        fidl::endpoints,
        fidl_fuchsia_input_report::{
            DeviceDescriptor, InputDeviceMarker, KeyboardDescriptor, KeyboardInputDescriptor,
        },
        fuchsia_async as fasync,
        futures::future,
    };

    const DEFAULT_REPORT_TIMESTAMP: u64 = 0;

    mod responds_to_get_descriptor_request {
        use {
            super::{
                utils::{make_input_device_proxy_and_struct, make_keyboard_descriptor},
                *,
            },
            fidl_fuchsia_input_report::InputReportsReaderMarker,
            futures::{pin_mut, task::Poll},
            matches::assert_matches,
        };

        #[fasync::run_until_stalled(test)]
        async fn single_request_before_call_to_get_input_repors_reader() -> Result<(), Error> {
            let (proxy, request_stream) = endpoints::create_proxy_and_stream::<InputDeviceMarker>()
                .context("creating InputDevice proxy and stream")?;
            let input_device_server_fut =
                Box::new(InputDevice::new(request_stream, make_keyboard_descriptor(vec![Key::A])))
                    .serve_reports();
            let get_descriptor_fut = proxy.get_descriptor();
            std::mem::drop(proxy); // Drop `proxy` to terminate `request_stream`.

            let (_, get_descriptor_result) =
                future::join(input_device_server_fut, get_descriptor_fut).await;
            assert_eq!(
                get_descriptor_result.context("fidl error")?,
                make_keyboard_descriptor(vec![Key::A])
            );
            Ok(())
        }

        #[test]
        fn multiple_requests_before_call_to_get_input_repors_reader() -> Result<(), Error> {
            let mut executor = fasync::TestExecutor::new().context("creating executor")?;
            let (proxy, request_stream) = endpoints::create_proxy_and_stream::<InputDeviceMarker>()
                .context("creating InputDevice proxy and stream")?;
            let mut input_device_server_fut =
                Box::new(InputDevice::new(request_stream, make_keyboard_descriptor(vec![Key::A])))
                    .serve_reports();

            let mut get_descriptor_fut = proxy.get_descriptor();
            assert_matches!(
                executor.run_until_stalled(&mut input_device_server_fut),
                Poll::Pending
            );
            std::mem::drop(executor.run_until_stalled(&mut get_descriptor_fut));

            let mut get_descriptor_fut = proxy.get_descriptor();
            let _ = executor.run_until_stalled(&mut input_device_server_fut);
            assert_matches!(
                executor.run_until_stalled(&mut get_descriptor_fut),
                Poll::Ready(Ok(_))
            );

            Ok(())
        }

        #[test]
        fn after_call_to_get_input_reports_reader_with_report_pending() -> Result<(), Error> {
            let mut executor = fasync::TestExecutor::new().context("creating executor")?;
            let (input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
            input_device
                .key_press(KeyboardReport { pressed_keys: vec![] }, DEFAULT_REPORT_TIMESTAMP)
                .context("internal error queuing input event")?;

            let input_device_server_fut = input_device.serve_reports();
            pin_mut!(input_device_server_fut);

            let (_input_reports_reader_proxy, input_reports_reader_server_end) =
                endpoints::create_proxy::<InputReportsReaderMarker>()
                    .context("internal error creating InputReportsReader proxy and server end")?;
            input_device_proxy
                .get_input_reports_reader(input_reports_reader_server_end)
                .context("sending get_input_reports_reader request")?;
            assert_matches!(
                executor.run_until_stalled(&mut input_device_server_fut),
                Poll::Pending
            );

            let mut get_descriptor_fut = input_device_proxy.get_descriptor();
            assert_matches!(
                executor.run_until_stalled(&mut input_device_server_fut),
                Poll::Pending
            );
            assert_matches!(executor.run_until_stalled(&mut get_descriptor_fut), Poll::Ready(_));
            Ok(())
        }
    }

    mod report_contents {
        use {
            super::{
                utils::{get_input_reports, make_input_device_proxy_and_struct},
                *,
            },
            crate::usages::Usages,
            matches::assert_matches,
            std::convert::TryInto as _,
        };

        #[fasync::run_until_stalled(test)]
        async fn key_press_generates_expected_keyboard_input_report() -> Result<(), Error> {
            let (input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
            input_device.key_press(
                KeyboardReport {
                    pressed_keys: vec![Usages::HidUsageKeyA as u32, Usages::HidUsageKeyB as u32],
                },
                DEFAULT_REPORT_TIMESTAMP,
            )?;

            let input_reports = get_input_reports(input_device, input_device_proxy).await;
            assert_eq!(
                input_reports.as_slice(),
                [InputReport {
                    event_time: Some(
                        DEFAULT_REPORT_TIMESTAMP.try_into().expect("converting to i64")
                    ),
                    keyboard: Some(KeyboardInputReport {
                        pressed_keys3: Some(vec![Key::A, Key::B]),
                        ..KeyboardInputReport::EMPTY
                    }),
                    ..InputReport::EMPTY
                }]
            );
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn key_press_usage_generates_expected_keyboard_input_report_for_some(
        ) -> Result<(), Error> {
            let (input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
            input_device
                .key_press_usage(Some(Usages::HidUsageKeyA as u32), DEFAULT_REPORT_TIMESTAMP)?;

            let input_reports = get_input_reports(input_device, input_device_proxy).await;
            assert_eq!(
                input_reports.as_slice(),
                [InputReport {
                    event_time: Some(
                        DEFAULT_REPORT_TIMESTAMP.try_into().expect("converting to i64")
                    ),
                    keyboard: Some(KeyboardInputReport {
                        pressed_keys3: Some(vec![Key::A]),
                        ..KeyboardInputReport::EMPTY
                    }),
                    ..InputReport::EMPTY
                }]
            );
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn key_press_usage_generates_expected_keyboard_input_report_for_none(
        ) -> Result<(), Error> {
            let (input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
            input_device.key_press_usage(None, DEFAULT_REPORT_TIMESTAMP)?;

            let input_reports = get_input_reports(input_device, input_device_proxy).await;
            assert_eq!(
                input_reports.as_slice(),
                [InputReport {
                    event_time: Some(
                        DEFAULT_REPORT_TIMESTAMP.try_into().expect("converting to i64")
                    ),
                    keyboard: Some(KeyboardInputReport {
                        pressed_keys3: Some(vec![]),
                        ..KeyboardInputReport::EMPTY
                    }),
                    ..InputReport::EMPTY
                }]
            );
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn key_press_returns_error_if_usage_cannot_be_mapped_to_key() {
            let (_input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
            assert_matches!(
                input_device.key_press(
                    KeyboardReport { pressed_keys: vec![0xffff_ffff] },
                    DEFAULT_REPORT_TIMESTAMP
                ),
                Err(_)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn key_press_usage_returns_error_if_usage_cannot_be_mapped_to_key() {
            let (_input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
            assert_matches!(
                input_device.key_press_usage(Some(0xffff_ffff), DEFAULT_REPORT_TIMESTAMP),
                Err(_)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn key_events_generates_expected_keyboard_response() -> Result<(), Error> {
            let (_input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
            input_device.key_press_raw(
                KeyboardReport { pressed_keys: vec![Key::A as u32, Key::B as u32] },
                DEFAULT_REPORT_TIMESTAMP,
            )?;

            let input_reports = get_input_reports(input_device, _input_device_proxy).await;
            assert_eq!(
                input_reports.as_slice(),
                [InputReport {
                    event_time: Some(
                        DEFAULT_REPORT_TIMESTAMP.try_into().expect("converting to i64")
                    ),
                    keyboard: Some(KeyboardInputReport {
                        pressed_keys3: Some(vec![Key::A, Key::B]),
                        ..KeyboardInputReport::EMPTY
                    }),
                    ..InputReport::EMPTY
                }]
            );
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn tap_generates_expected_report_for_some() -> Result<(), Error> {
            let (_input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
            input_device.tap(Some((10, 20)), DEFAULT_REPORT_TIMESTAMP)?;

            let input_reports = get_input_reports(input_device, _input_device_proxy).await;
            assert_eq!(
                input_reports.as_slice(),
                [InputReport {
                    event_time: Some(
                        DEFAULT_REPORT_TIMESTAMP.try_into().expect("converting to i64")
                    ),
                    touch: Some(TouchInputReport {
                        contacts: Some(vec![ContactInputReport {
                            contact_id: Some(1),
                            position_x: Some(10),
                            position_y: Some(20),
                            pressure: None,
                            contact_width: Some(0),
                            contact_height: Some(0),
                            ..ContactInputReport::EMPTY
                        }]),
                        pressed_buttons: Some(vec![]),
                        ..TouchInputReport::EMPTY
                    }),
                    ..InputReport::EMPTY
                }]
            );
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn tap_generates_expected_report_for_none() -> Result<(), Error> {
            let (_input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
            input_device.tap(None, DEFAULT_REPORT_TIMESTAMP)?;

            let input_reports = get_input_reports(input_device, _input_device_proxy).await;
            assert_eq!(
                input_reports.as_slice(),
                [InputReport {
                    event_time: Some(
                        DEFAULT_REPORT_TIMESTAMP.try_into().expect("converting to i64")
                    ),
                    touch: Some(TouchInputReport {
                        contacts: Some(vec![]),
                        pressed_buttons: Some(vec![]),
                        ..TouchInputReport::EMPTY
                    }),
                    ..InputReport::EMPTY
                }]
            );
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn multi_finger_tap_generates_report_for_single_finger() -> Result<(), Error> {
            let (_input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
            input_device.multi_finger_tap(
                Some(vec![Touch { finger_id: 5, x: 10, y: 20, width: 100, height: 200 }]),
                DEFAULT_REPORT_TIMESTAMP,
            )?;

            let input_reports = get_input_reports(input_device, _input_device_proxy).await;
            assert_eq!(
                input_reports.as_slice(),
                [InputReport {
                    event_time: Some(
                        DEFAULT_REPORT_TIMESTAMP.try_into().expect("converting to i64")
                    ),
                    touch: Some(TouchInputReport {
                        contacts: Some(vec![ContactInputReport {
                            contact_id: Some(5),
                            position_x: Some(10),
                            position_y: Some(20),
                            pressure: None,
                            contact_width: Some(100),
                            contact_height: Some(200),
                            ..ContactInputReport::EMPTY
                        }]),
                        pressed_buttons: Some(vec![]),
                        ..TouchInputReport::EMPTY
                    }),
                    ..InputReport::EMPTY
                }]
            );
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn multi_finger_tap_generates_expected_report_for_two_fingers() -> Result<(), Error> {
            let (_input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
            input_device.multi_finger_tap(
                Some(vec![
                    Touch { finger_id: 5, x: 10, y: 20, width: 100, height: 200 },
                    Touch { finger_id: 0, x: 30, y: 40, width: 300, height: 400 },
                ]),
                DEFAULT_REPORT_TIMESTAMP,
            )?;

            let input_reports = get_input_reports(input_device, _input_device_proxy).await;
            assert_eq!(
                input_reports.as_slice(),
                [InputReport {
                    event_time: Some(
                        DEFAULT_REPORT_TIMESTAMP.try_into().expect("converting to i64")
                    ),
                    touch: Some(TouchInputReport {
                        contacts: Some(vec![
                            ContactInputReport {
                                contact_id: Some(5),
                                position_x: Some(10),
                                position_y: Some(20),
                                pressure: None,
                                contact_width: Some(100),
                                contact_height: Some(200),
                                ..ContactInputReport::EMPTY
                            },
                            ContactInputReport {
                                contact_id: Some(0),
                                position_x: Some(30),
                                position_y: Some(40),
                                pressure: None,
                                contact_width: Some(300),
                                contact_height: Some(400),
                                ..ContactInputReport::EMPTY
                            }
                        ]),
                        pressed_buttons: Some(vec![]),
                        ..TouchInputReport::EMPTY
                    }),
                    ..InputReport::EMPTY
                }]
            );
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn multi_finger_tap_generates_expected_report_for_zero_fingers() -> Result<(), Error>
        {
            let (_input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
            input_device.multi_finger_tap(Some(vec![]), DEFAULT_REPORT_TIMESTAMP)?;

            let input_reports = get_input_reports(input_device, _input_device_proxy).await;
            assert_eq!(
                input_reports.as_slice(),
                [InputReport {
                    event_time: Some(
                        DEFAULT_REPORT_TIMESTAMP.try_into().expect("converting to i64")
                    ),
                    touch: Some(TouchInputReport {
                        contacts: Some(vec![]),
                        pressed_buttons: Some(vec![]),
                        ..TouchInputReport::EMPTY
                    }),
                    ..InputReport::EMPTY
                }]
            );
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn multi_finger_tap_generates_expected_report_for_none() -> Result<(), Error> {
            let (_input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
            input_device.multi_finger_tap(None, DEFAULT_REPORT_TIMESTAMP)?;

            let input_reports = get_input_reports(input_device, _input_device_proxy).await;
            assert_eq!(
                input_reports.as_slice(),
                [InputReport {
                    event_time: Some(
                        DEFAULT_REPORT_TIMESTAMP.try_into().expect("converting to i64")
                    ),
                    touch: Some(TouchInputReport {
                        contacts: Some(vec![]),
                        pressed_buttons: Some(vec![]),
                        ..TouchInputReport::EMPTY
                    }),
                    ..InputReport::EMPTY
                }]
            );
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn multi_finger_tap_returns_error_when_num_fingers_is_to_large() {
            let (_input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
            assert_matches!(
                input_device.multi_finger_tap(
                    Some(
                        (0..=TOUCH_MAX_CONTACTS)
                            .map(|i| Touch {
                                finger_id: i,
                                x: i as i32,
                                y: i as i32,
                                width: i,
                                height: i
                            })
                            .collect(),
                    ),
                    DEFAULT_REPORT_TIMESTAMP,
                ),
                Err(_)
            );
        }
    }

    mod future_resolution {
        use {
            super::{
                utils::{make_input_device_proxy_and_struct, make_input_reports_reader_proxy},
                *,
            },
            futures::task::Poll,
        };

        mod yields_ok_after_all_reports_are_sent_to_input_reports_reader {
            use {super::*, matches::assert_matches};

            #[test]
            fn if_device_request_channel_was_closed() {
                let mut executor = fasync::TestExecutor::new().expect("creating executor");
                let (input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
                let input_reports_reader_proxy =
                    make_input_reports_reader_proxy(&input_device_proxy);
                input_device
                    .key_press(KeyboardReport { pressed_keys: vec![] }, DEFAULT_REPORT_TIMESTAMP)
                    .expect("queuing input report");

                let _input_reports_fut = input_reports_reader_proxy.read_input_reports();
                let mut input_device_fut = input_device.serve_reports();
                std::mem::drop(input_device_proxy); // Close device request channel.
                assert_matches!(
                    executor.run_until_stalled(&mut input_device_fut),
                    Poll::Ready(Ok(()))
                );
            }

            #[test]
            fn even_if_device_request_channel_is_open() {
                let mut executor = fasync::TestExecutor::new().expect("creating executor");
                let (input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
                let input_reports_reader_proxy =
                    make_input_reports_reader_proxy(&input_device_proxy);
                input_device
                    .key_press(KeyboardReport { pressed_keys: vec![] }, DEFAULT_REPORT_TIMESTAMP)
                    .expect("queuing input report");

                let _input_reports_fut = input_reports_reader_proxy.read_input_reports();
                let mut input_device_fut = input_device.serve_reports();
                assert_matches!(
                    executor.run_until_stalled(&mut input_device_fut),
                    Poll::Ready(Ok(()))
                );
            }

            #[test]
            fn even_if_reports_was_empty_and_device_request_channel_is_open() {
                let mut executor = fasync::TestExecutor::new().expect("creating executor");
                let (input_device_proxy, input_device) = make_input_device_proxy_and_struct();
                let input_reports_reader_proxy =
                    make_input_reports_reader_proxy(&input_device_proxy);
                let _input_reports_fut = input_reports_reader_proxy.read_input_reports();
                let mut input_device_fut = input_device.serve_reports();
                assert_matches!(
                    executor.run_until_stalled(&mut input_device_fut),
                    Poll::Ready(Ok(()))
                );
            }
        }

        mod yields_err_if_peer_closed_device_channel_without_calling_get_input_reports_reader {
            use super::*;
            use matches::assert_matches;

            #[test]
            fn if_reports_were_available() {
                let mut executor = fasync::TestExecutor::new().expect("creating executor");
                let (input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
                input_device
                    .key_press(KeyboardReport { pressed_keys: vec![] }, DEFAULT_REPORT_TIMESTAMP)
                    .expect("queuing input report");

                let mut input_device_fut = input_device.serve_reports();
                std::mem::drop(input_device_proxy);
                assert_matches!(
                    executor.run_until_stalled(&mut input_device_fut),
                    Poll::Ready(Err(_))
                )
            }

            #[test]
            fn even_if_no_reports_were_available() {
                let mut executor = fasync::TestExecutor::new().expect("creating executor");
                let (input_device_proxy, input_device) = make_input_device_proxy_and_struct();
                let mut input_device_fut = input_device.serve_reports();
                std::mem::drop(input_device_proxy);
                assert_matches!(
                    executor.run_until_stalled(&mut input_device_fut),
                    Poll::Ready(Err(_))
                )
            }
        }

        mod is_pending_if_peer_has_device_channel_open_and_has_not_called_get_input_reports_reader {
            use super::*;
            use matches::assert_matches;

            #[test]
            fn if_reports_were_available() {
                let mut executor = fasync::TestExecutor::new().expect("creating executor");
                let (_input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
                input_device
                    .key_press(KeyboardReport { pressed_keys: vec![] }, DEFAULT_REPORT_TIMESTAMP)
                    .expect("queuing input report");

                let mut input_device_fut = input_device.serve_reports();
                assert_matches!(executor.run_until_stalled(&mut input_device_fut), Poll::Pending)
            }

            #[test]
            fn even_if_no_reports_were_available() {
                let mut executor = fasync::TestExecutor::new().expect("creating executor");
                let (_input_device_proxy, input_device) = make_input_device_proxy_and_struct();
                let mut input_device_fut = input_device.serve_reports();
                assert_matches!(executor.run_until_stalled(&mut input_device_fut), Poll::Pending)
            }

            #[test]
            fn even_if_get_device_descriptor_has_been_called() {
                let mut executor = fasync::TestExecutor::new().expect("creating executor");
                let (input_device_proxy, input_device) = make_input_device_proxy_and_struct();
                let mut input_device_fut = input_device.serve_reports();
                let _get_descriptor_fut = input_device_proxy.get_descriptor();
                assert_matches!(executor.run_until_stalled(&mut input_device_fut), Poll::Pending)
            }
        }

        mod is_pending_if_peer_has_not_read_any_reports_when_a_report_is_available {
            use super::*;
            use matches::assert_matches;

            #[test]
            fn if_device_request_channel_is_open() {
                let mut executor = fasync::TestExecutor::new().expect("creating executor");
                let (input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
                let _input_reports_reader_proxy =
                    make_input_reports_reader_proxy(&input_device_proxy);
                input_device
                    .key_press(KeyboardReport { pressed_keys: vec![] }, DEFAULT_REPORT_TIMESTAMP)
                    .expect("queuing input report");

                let mut input_device_fut = input_device.serve_reports();
                assert_matches!(executor.run_until_stalled(&mut input_device_fut), Poll::Pending)
            }

            #[test]
            fn even_if_device_channel_is_closed() {
                let mut executor = fasync::TestExecutor::new().expect("creating executor");
                let (input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
                let _input_reports_reader_proxy =
                    make_input_reports_reader_proxy(&input_device_proxy);
                input_device
                    .key_press(KeyboardReport { pressed_keys: vec![] }, DEFAULT_REPORT_TIMESTAMP)
                    .expect("queuing input report");

                let mut input_device_fut = input_device.serve_reports();
                std::mem::drop(input_device_proxy); // Terminate `InputDeviceRequestStream`.
                assert_matches!(executor.run_until_stalled(&mut input_device_fut), Poll::Pending)
            }
        }

        mod is_pending_if_peer_did_not_read_all_reports {
            use {
                super::*, fidl_fuchsia_input_report::MAX_DEVICE_REPORT_COUNT,
                matches::assert_matches,
            };

            #[test]
            fn if_device_request_channel_is_open() {
                let mut executor = fasync::TestExecutor::new().expect("creating executor");
                let (input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
                let input_reports_reader_proxy =
                    make_input_reports_reader_proxy(&input_device_proxy);
                (0..=MAX_DEVICE_REPORT_COUNT).for_each(|_| {
                    input_device
                        .key_press(
                            KeyboardReport { pressed_keys: vec![] },
                            DEFAULT_REPORT_TIMESTAMP,
                        )
                        .expect("queuing input report");
                });

                // One query isn't enough to consume all of the reports queued above.
                let _input_reports_fut = input_reports_reader_proxy.read_input_reports();
                let mut input_device_fut = input_device.serve_reports();
                assert_matches!(executor.run_until_stalled(&mut input_device_fut), Poll::Pending)
            }

            #[test]
            fn even_if_device_request_channel_is_closed() {
                let mut executor = fasync::TestExecutor::new().expect("creating executor");
                let (input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
                let input_reports_reader_proxy =
                    make_input_reports_reader_proxy(&input_device_proxy);
                (0..=MAX_DEVICE_REPORT_COUNT).for_each(|_| {
                    input_device
                        .key_press(
                            KeyboardReport { pressed_keys: vec![] },
                            DEFAULT_REPORT_TIMESTAMP,
                        )
                        .expect("queuing input report");
                });

                // One query isn't enough to consume all of the reports queued above.
                let _input_reports_fut = input_reports_reader_proxy.read_input_reports();
                let mut input_device_fut = input_device.serve_reports();
                std::mem::drop(input_device_proxy); // Terminate `InputDeviceRequestStream`.
                assert_matches!(executor.run_until_stalled(&mut input_device_fut), Poll::Pending)
            }
        }
    }

    // Because `input_synthesis` is a library, unsupported features should yield `Err`s,
    // rather than panic!()-ing.
    mod unsupported_fidl_requests {
        use {
            super::{utils::make_input_device_proxy_and_struct, *},
            fidl_fuchsia_input_report::{FeatureReport, OutputReport},
            matches::assert_matches,
        };

        #[fasync::run_until_stalled(test)]
        async fn send_output_report_request_yields_error() -> Result<(), Error> {
            let (proxy, input_device) = make_input_device_proxy_and_struct();
            let input_device_server_fut = input_device.serve_reports();
            let send_output_report_fut = proxy.send_output_report(OutputReport::EMPTY);
            std::mem::drop(proxy); // Drop `proxy` to terminate `request_stream`.
            assert_matches!(
                future::join(input_device_server_fut, send_output_report_fut).await,
                (_, Err(_))
            );
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn get_feature_report_request_yields_error() -> Result<(), Error> {
            let (proxy, input_device) = make_input_device_proxy_and_struct();
            let input_device_server_fut = input_device.serve_reports();
            let get_feature_report_fut = proxy.get_feature_report();
            std::mem::drop(proxy); // Drop `proxy` to terminate `request_stream`.
            assert_matches!(
                future::join(input_device_server_fut, get_feature_report_fut).await,
                (_, Err(_))
            );
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn set_feature_report_request_yields_error() -> Result<(), Error> {
            let (proxy, input_device) = make_input_device_proxy_and_struct();
            let input_device_server_fut = input_device.serve_reports();
            let set_feature_report_fut = proxy.set_feature_report(FeatureReport::EMPTY);
            std::mem::drop(proxy); // Drop `proxy` to terminate `request_stream`.
            assert_matches!(
                future::join(input_device_server_fut, set_feature_report_fut).await,
                (_, Err(_))
            );
            Ok(())
        }
    }

    // Because `input_synthesis` is a library, unimplemented features should yield `Error`s,
    // rather than panic!()-ing.
    mod unimplemented_trait_methods {
        use super::{utils::make_input_device_proxy_and_struct, *};
        use matches::assert_matches;

        #[test]
        fn media_buttons_yields_error() -> Result<(), Error> {
            let _executor = fuchsia_async::TestExecutor::new(); // Create TLS executor used by `endpoints`.
            let (_proxy, mut input_device) = make_input_device_proxy_and_struct();
            let media_buttons_result =
                input_device.media_buttons(false, false, false, false, false, false, 0);
            assert_matches!(media_buttons_result, Err(_));
            Ok(())
        }
    }

    // Because `input_synthesis` is a library, unsupported use cases should yield `Error`s,
    // rather than panic!()-ing.
    mod unsupported_use_cases {
        use {
            super::{utils::make_input_device_proxy_and_struct, *},
            fidl_fuchsia_input_report::InputReportsReaderMarker,
            matches::assert_matches,
        };

        #[fasync::run_until_stalled(test)]
        async fn multiple_get_input_reports_reader_requests_yield_error() -> Result<(), Error> {
            let (input_device_proxy, input_device) = make_input_device_proxy_and_struct();

            let (_input_reports_reader_proxy, input_reports_reader_server_end) =
                endpoints::create_proxy::<InputReportsReaderMarker>()
                    .context("creating InputReportsReader proxy and server end")?;
            input_device_proxy
                .get_input_reports_reader(input_reports_reader_server_end)
                .expect("sending first get_input_reports_reader request");

            let (_input_reports_reader_proxy, input_reports_reader_server_end) =
                endpoints::create_proxy::<InputReportsReaderMarker>()
                    .context("internal error creating InputReportsReader proxy and server end")?;
            input_device_proxy
                .get_input_reports_reader(input_reports_reader_server_end)
                .expect("sending second get_input_reports_reader request");

            let input_device_fut = input_device.serve_reports();
            assert_matches!(input_device_fut.await, Err(_));
            Ok(())
        }
    }

    mod utils {
        use {
            super::*,
            fidl_fuchsia_input_report::{
                InputDeviceMarker, InputDeviceProxy, InputReportsReaderMarker,
                InputReportsReaderProxy,
            },
            fuchsia_zircon as zx,
        };

        /// Creates a `DeviceDescriptor` for a keyboard which has the keys enumerated
        /// in `keys`.
        pub(super) fn make_keyboard_descriptor(keys: Vec<Key>) -> DeviceDescriptor {
            DeviceDescriptor {
                keyboard: Some(KeyboardDescriptor {
                    input: Some(KeyboardInputDescriptor {
                        keys3: Some(keys),
                        ..KeyboardInputDescriptor::EMPTY
                    }),
                    ..KeyboardDescriptor::EMPTY
                }),
                ..DeviceDescriptor::EMPTY
            }
        }

        /// Creates an `InputDeviceProxy`, for sending `fuchsia.input.report.InputDevice`
        /// requests, and an `InputDevice` struct that will receive the FIDL requests
        /// from the `InputDeviceProxy`.
        ///
        /// # Returns
        /// A tuple of the proxy and struct. The struct is `Box`-ed so that the caller
        /// can easily invoke `serve_reports()`.
        pub(super) fn make_input_device_proxy_and_struct() -> (InputDeviceProxy, Box<InputDevice>) {
            let (input_device_proxy, input_device_request_stream) =
                endpoints::create_proxy_and_stream::<InputDeviceMarker>()
                    .expect("creating InputDevice proxy and stream");
            let input_device =
                Box::new(InputDevice::new(input_device_request_stream, DeviceDescriptor::EMPTY));
            (input_device_proxy, input_device)
        }

        /// Creates an `InputReportsReaderProxy`, for sending
        /// `fuchsia.input.report.InputReportsReader` reqests, and registers that
        /// `InputReportsReader` with the `InputDevice` bound to `InputDeviceProxy`.
        ///
        /// # Returns
        /// The newly created `InputReportsReaderProxy`.
        pub(super) fn make_input_reports_reader_proxy(
            input_device_proxy: &InputDeviceProxy,
        ) -> InputReportsReaderProxy {
            let (input_reports_reader_proxy, input_reports_reader_server_end) =
                endpoints::create_proxy::<InputReportsReaderMarker>()
                    .expect("internal error creating InputReportsReader proxy and server end");
            input_device_proxy
                .get_input_reports_reader(input_reports_reader_server_end)
                .expect("sending get_input_reports_reader request");
            input_reports_reader_proxy
        }

        /// Serves `fuchsia.input.report.InputDevice` and `fuchsia.input.report.InputReportsReader`
        /// protocols using `input_device`, and reads `InputReport`s with one call to
        /// `input_device_proxy.read_input_reports()`. Then drops the connections to
        /// `fuchsia.input.report.InputDevice` and `fuchsia.input.report.InputReportsReader`.
        ///
        /// # Returns
        /// The reports provided by the `InputDevice`.
        pub(super) async fn get_input_reports(
            input_device: Box<InputDevice>,
            mut input_device_proxy: InputDeviceProxy,
        ) -> Vec<InputReport> {
            let input_reports_reader_proxy =
                make_input_reports_reader_proxy(&mut input_device_proxy);
            let input_device_server_fut = input_device.serve_reports();
            let input_reports_fut = input_reports_reader_proxy.read_input_reports();
            std::mem::drop(input_reports_reader_proxy); // Close channel to `input_reports_reader_server_end`
            std::mem::drop(input_device_proxy); // Terminate `input_device_request_stream`.
            future::join(input_device_server_fut, input_reports_fut)
                .await
                .1
                .expect("fidl error")
                .map_err(zx::Status::from_raw)
                .expect("service error")
        }
    }
}
