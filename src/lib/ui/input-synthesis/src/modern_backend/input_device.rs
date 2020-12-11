// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)] // TODO(fxbug.dev/63985) remove `cfg`
#![warn(missing_docs)]

use {
    crate::{modern_backend::input_reports_reader::InputReportsReader, synthesizer},
    anyhow::{format_err, Context as _, Error},
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl::Error as FidlError,
    fidl_fuchsia_input::Key,
    fidl_fuchsia_input_report::{
        DeviceDescriptor, InputDeviceRequest, InputDeviceRequestStream, InputReport,
        InputReportsReaderMarker,
    },
    fidl_fuchsia_ui_input::{KeyboardReport, Touch},
    futures::{future, StreamExt},
};

/// Implements the `synthesizer::InputDevice` trait, and the server side of the
/// `fuchsia.input.report.InputDevice` FIDL protocol. Used by
/// `modern_backend::InputDeviceRegistry`.
///
/// # Note
/// Some of the methods of `fuchsia.input.report.InputDevice` are not relevant to
/// input injection, so this implemnentation does not support them:
/// * `GetFeatureReport` and `SetFeatureReport` are for sensors.
/// * `SendOutputReport` provides a way to change keyboard LED state.
///
/// If these FIDL methods are invoked, `InputDevice::serve_reports()` will resolve
/// to Err.
pub(super) struct InputDevice<F: Fn() -> DeviceDescriptor> {
    request_stream: InputDeviceRequestStream,
    /// Generates `fuchsia.input.report.DeviceDescriptor`s, to respond to
    /// `fuchsia.input.report.InputDevice.GetDescriptor()` requests.
    descriptor_generator: F,
    /// FIFO queue of reports to be consumed by calls to
    /// `fuchsia.input.report.InputReportsReader.ReadInputReports()`.
    /// Populated by calls to `synthesizer::InputDevice` trait methods.
    reports: Vec<InputReport>,
}

#[async_trait(?Send)]
impl<F: Fn() -> DeviceDescriptor> synthesizer::InputDevice for self::InputDevice<F> {
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
        todo!();
    }

    // TODO(fxbug.dev/63973): remove dependency on HID usage codes.
    fn key_press(&mut self, _report: KeyboardReport, _time: u64) -> Result<(), Error> {
        // TODO: populate the `InputReport` with a `KeyboardInputReport`.
        self.reports.push(InputReport::EMPTY);
        Ok(())
    }

    // TODO(fxbug.dev/63973): remove reference to HID usage codes.
    fn key_press_usage(&mut self, _usage: Option<u32>, _time: u64) -> Result<(), Error> {
        todo!();
    }

    fn tap(&mut self, _pos: Option<(u32, u32)>, _time: u64) -> Result<(), Error> {
        todo!();
    }

    fn multi_finger_tap(&mut self, _fingers: Option<Vec<Touch>>, _time: u64) -> Result<(), Error> {
        todo!();
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
    /// channel to the `fuchsia.input.InputReportsReader` client.
    async fn serve_reports(mut self: Box<Self>) -> Result<(), Error> {
        // Destructure fields into independent variables, to avoid "partial-move" issues.
        let Self { request_stream, descriptor_generator, reports } = *self;

        // Process `fuchsia.input.report.InputDevice` requests, waiting for the `InputDevice`
        // client to provide a `ServerEnd<InputReportsReader>` by calling `GetInputReportsReader()`.
        let mut input_reports_reader_server_end_stream = request_stream
            .filter_map(|r| future::ready(Self::handle_device_request(r, &descriptor_generator)));
        let _input_reports_reader_fut = {
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

        // Now, serve both the `fuchsia.input.report.InputDevice` protocol, and the
        // `fuchsia.input.report.InputReportsReader` protocol.
        todo!();
    }
}

impl<F: Fn() -> DeviceDescriptor> InputDevice<F> {
    /// Creates a new `InputDevice` that will:
    /// a) process requests from `request_stream`, and
    /// b) respond to `GetDescriptor` calls with the descriptor generated by `descriptor_generator()`
    ///
    /// The `InputDevice` initially has no reports queued.
    pub(super) fn new(request_stream: InputDeviceRequestStream, descriptor_generator: F) -> Self {
        Self { request_stream, descriptor_generator, reports: vec![] }
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
        descriptor_generator: &F,
    ) -> Option<Result<ServerEnd<InputReportsReaderMarker>, Error>> {
        match request {
            Ok(InputDeviceRequest::GetInputReportsReader { reader: reader_server_end, .. }) => {
                Some(Ok(reader_server_end))
            }
            Ok(InputDeviceRequest::GetDescriptor { responder }) => {
                match responder.send(descriptor_generator()) {
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
            Err(e) => Some(Err(anyhow::Error::from(e).context("while reading InputDeviceRequest"))),
        }
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
        matches::assert_matches,
    };

    const DEFAULT_REPORT_TIMESTAMP: u64 = 0;

    mod responds_to_get_descriptor_request {
        use {
            super::{utils::make_keyboard_descriptor, *},
            futures::task::Poll,
        };

        #[fasync::run_until_stalled(test)]
        async fn single_request_before_call_to_get_input_repors_reader() -> Result<(), Error> {
            let (proxy, request_stream) = endpoints::create_proxy_and_stream::<InputDeviceMarker>()
                .context("creating InputDevice proxy and stream")?;
            let input_device_server_fut = Box::new(InputDevice::new(request_stream, || {
                make_keyboard_descriptor(vec![Key::A])
            }))
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
            let mut executor = fasync::Executor::new().context("creating executor")?;
            let (proxy, request_stream) = endpoints::create_proxy_and_stream::<InputDeviceMarker>()
                .context("creating InputDevice proxy and stream")?;
            let mut input_device_server_fut = Box::new(InputDevice::new(request_stream, || {
                utils::make_keyboard_descriptor(vec![Key::A])
            }))
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
    }

    mod future_resolution {
        use {
            super::{utils::make_input_device_proxy_and_struct, *},
            futures::task::Poll,
        };

        mod yields_err_if_peer_closed_device_channel_without_calling_get_input_reports_reader {
            use super::*;

            #[test]
            fn if_reports_were_available() {
                let mut executor = fasync::Executor::new().expect("creating executor");
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
                let mut executor = fasync::Executor::new().expect("creating executor");
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

            #[test]
            fn if_reports_were_available() {
                let mut executor = fasync::Executor::new().expect("creating executor");
                let (_input_device_proxy, mut input_device) = make_input_device_proxy_and_struct();
                input_device
                    .key_press(KeyboardReport { pressed_keys: vec![] }, DEFAULT_REPORT_TIMESTAMP)
                    .expect("queuing input report");

                let mut input_device_fut = input_device.serve_reports();
                assert_matches!(executor.run_until_stalled(&mut input_device_fut), Poll::Pending)
            }

            #[test]
            fn even_if_no_reports_were_available() {
                let mut executor = fasync::Executor::new().expect("creating executor");
                let (_input_device_proxy, input_device) = make_input_device_proxy_and_struct();
                let mut input_device_fut = input_device.serve_reports();
                assert_matches!(executor.run_until_stalled(&mut input_device_fut), Poll::Pending)
            }

            #[test]
            fn even_if_get_device_descriptor_has_been_called() {
                let mut executor = fasync::Executor::new().expect("creating executor");
                let (input_device_proxy, input_device) = make_input_device_proxy_and_struct();
                let mut input_device_fut = input_device.serve_reports();
                let _get_descriptor_fut = input_device_proxy.get_descriptor();
                assert_matches!(executor.run_until_stalled(&mut input_device_fut), Poll::Pending)
            }
        }
    }

    // Because `input_synthesis` is a library, unsupported features should yield `Error`s,
    // rather than panic!()-ing.
    mod unsupported_fidl_requests {
        use {
            super::{utils::make_input_device_proxy_and_struct, *},
            fidl_fuchsia_input_report::{FeatureReport, OutputReport},
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

    mod utils {
        use {
            super::*,
            fidl_fuchsia_input_report::{InputDeviceMarker, InputDeviceProxy},
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
        pub(super) fn make_input_device_proxy_and_struct(
        ) -> (InputDeviceProxy, Box<InputDevice<impl Fn() -> DeviceDescriptor>>) {
            let (input_device_proxy, input_device_request_stream) =
                endpoints::create_proxy_and_stream::<InputDeviceMarker>()
                    .expect("creating InputDevice proxy and stream");
            let input_device =
                Box::new(InputDevice::new(input_device_request_stream, || DeviceDescriptor::EMPTY));
            (input_device_proxy, input_device)
        }
    }
}
