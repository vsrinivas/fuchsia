// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{modern_backend::input_device::InputDevice, synthesizer},
    anyhow::{format_err, Context as _, Error},
    fidl::endpoints,
    fidl_fuchsia_input::Key,
    fidl_fuchsia_input_injection::InputDeviceRegistryProxy,
    fidl_fuchsia_input_report::{
        Axis, ContactInputDescriptor, DeviceDescriptor, InputDeviceMarker, KeyboardDescriptor,
        KeyboardInputDescriptor, Range, TouchDescriptor, TouchInputDescriptor, TouchType, Unit,
        UnitType,
    },
    std::convert::TryFrom,
};

/// Implements the `synthesizer::InputDeviceRegistry` trait, and the client side
/// of the `fuchsia.input.injection.InputDeviceRegistry` protocol.
pub struct InputDeviceRegistry {
    proxy: InputDeviceRegistryProxy,
}

impl synthesizer::InputDeviceRegistry for self::InputDeviceRegistry {
    fn add_touchscreen_device(
        &mut self,
        width: u32,
        height: u32,
    ) -> Result<Box<dyn synthesizer::InputDevice>, Error> {
        const MAX_CONTACTS: u32 = 255;
        self.add_device(DeviceDescriptor {
            touch: Some(TouchDescriptor {
                input: Some(TouchInputDescriptor {
                    contacts: Some(
                        std::iter::repeat(ContactInputDescriptor {
                            position_x: Some(Axis {
                                range: Range { min: 0, max: i64::from(width) },
                                unit: Unit { type_: UnitType::Other, exponent: 0 },
                            }),
                            position_y: Some(Axis {
                                range: Range { min: 0, max: i64::from(height) },
                                unit: Unit { type_: UnitType::Other, exponent: 0 },
                            }),
                            contact_width: Some(Axis {
                                range: Range { min: 0, max: i64::from(width) },
                                unit: Unit { type_: UnitType::Other, exponent: 0 },
                            }),
                            contact_height: Some(Axis {
                                range: Range { min: 0, max: i64::from(height) },
                                unit: Unit { type_: UnitType::Other, exponent: 0 },
                            }),
                            ..ContactInputDescriptor::EMPTY
                        })
                        .take(usize::try_from(MAX_CONTACTS).context("usize is impossibly small")?)
                        .collect(),
                    ),
                    max_contacts: Some(MAX_CONTACTS),
                    touch_type: Some(TouchType::Touchscreen),
                    buttons: Some(vec![]),
                    ..TouchInputDescriptor::EMPTY
                }),
                ..TouchDescriptor::EMPTY
            }),
            ..DeviceDescriptor::EMPTY
        })
    }

    fn add_keyboard_device(&mut self) -> Result<Box<dyn synthesizer::InputDevice>, Error> {
        // Generate a `Vec` of all known keys.
        // * Because there is no direct way to iterate over enum values, we iterate
        //   over the primitives corresponding to `Key::A` and `Key::MediaVolumeDecrement`.
        // * Some primitive values in the range have no corresponding enum value. For
        //   example, the value 0x00070065 sits between `NonUsBackslash` (0x00070064), and
        //   `KeypadEquals` (0x00070067). Such primitives are removed by `filter_map()`.
        let all_keys: Vec<Key> = (Key::A.into_primitive()
            ..=Key::MediaVolumeDecrement.into_primitive())
            .filter_map(Key::from_primitive)
            .collect();
        self.add_device(DeviceDescriptor {
            keyboard: Some(KeyboardDescriptor {
                input: Some(KeyboardInputDescriptor {
                    keys3: Some(all_keys.clone()),
                    ..KeyboardInputDescriptor::EMPTY
                }),
                ..KeyboardDescriptor::EMPTY
            }),
            ..DeviceDescriptor::EMPTY
        })
    }

    fn add_media_buttons_device(&mut self) -> Result<Box<dyn synthesizer::InputDevice>, Error> {
        Err(format_err!("TODO: implement media buttons support"))
    }
}

impl InputDeviceRegistry {
    pub fn new(proxy: InputDeviceRegistryProxy) -> Self {
        Self { proxy }
    }

    /// Adds a device to the `InputDeviceRegistry` FIDL server connected to this
    /// `InputDeviceRegistry` struct.
    ///
    /// # Returns
    /// A `synthesizer::InputDevice`, which can be used to send events to the
    /// `fuchsia.input.report.InputDevice` that has been registered with the
    /// `fuchsia.input.injection.InputDeviceRegistry` service.
    fn add_device(
        &self,
        descriptor: DeviceDescriptor,
    ) -> Result<Box<dyn synthesizer::InputDevice>, Error> {
        let (client_end, request_stream) = endpoints::create_request_stream::<InputDeviceMarker>()?;
        self.proxy.register(client_end)?;
        Ok(Box::new(InputDevice::new(request_stream, descriptor)))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{synthesizer::InputDeviceRegistry as _, *},
        fidl_fuchsia_input_injection::{InputDeviceRegistryMarker, InputDeviceRegistryRequest},
        fuchsia_async as fasync,
        futures::{pin_mut, task::Poll, StreamExt},
        test_case::test_case,
    };

    #[test_case(&super::InputDeviceRegistry::add_keyboard_device; "keyboard_device")]
    #[test_case(&|registry| InputDeviceRegistry::add_touchscreen_device(registry, 640, 480);
                "touchscreen_device")]
    fn add_device_invokes_fidl_register_method_exactly_once(
        add_device_method: &dyn Fn(
            &mut super::InputDeviceRegistry,
        ) -> Result<Box<dyn synthesizer::InputDevice>, Error>,
    ) -> Result<(), Error> {
        let mut executor = fasync::TestExecutor::new().context("creating executor")?;
        let (proxy, request_stream) =
            endpoints::create_proxy_and_stream::<InputDeviceRegistryMarker>()
                .context("failed to create proxy and stream for InputDeviceRegistry")?;
        add_device_method(&mut InputDeviceRegistry { proxy }).context("adding device")?;

        let requests = match executor.run_until_stalled(&mut request_stream.collect::<Vec<_>>()) {
            Poll::Ready(reqs) => reqs,
            Poll::Pending => return Err(format_err!("request_stream did not terminate")),
        };
        matches::assert_matches!(
            requests.as_slice(),
            [Ok(InputDeviceRegistryRequest::Register { .. })]
        );

        Ok(())
    }

    #[test_case(&super::InputDeviceRegistry::add_keyboard_device =>
                matches Ok(DeviceDescriptor { keyboard: Some(_), .. });
                "keyboard_device")]
    #[test_case(&|registry| InputDeviceRegistry::add_touchscreen_device(registry, 640, 480) =>
                matches Ok(DeviceDescriptor {
                    touch: Some(TouchDescriptor {
                        input: Some(TouchInputDescriptor { .. }),
                        ..
                    }),
                    .. });
                "touchscreen_device")]
    fn add_device_registers_correct_device_type(
        add_device_method: &dyn Fn(
            &mut super::InputDeviceRegistry,
        ) -> Result<Box<dyn synthesizer::InputDevice>, Error>,
    ) -> Result<DeviceDescriptor, Error> {
        let mut executor = fasync::TestExecutor::new().context("creating executor")?;
        // Create an `InputDeviceRegistry`, and add a keyboard to it.
        let (registry_proxy, mut registry_request_stream) =
            endpoints::create_proxy_and_stream::<InputDeviceRegistryMarker>()
                .context("failed to create proxy and stream for InputDeviceRegistry")?;
        let mut input_device_registry = InputDeviceRegistry { proxy: registry_proxy };
        let input_device =
            add_device_method(&mut input_device_registry).context("adding keyboard")?;

        let test_fut = async {
            // `input_device_registry` should send a `Register` messgage to `registry_request_stream`.
            // Use `registry_request_stream` to grab the `ClientEnd` of the keyboard added above,
            // and convert the `ClientEnd` into an `InputDeviceProxy`.
            let input_device_proxy = match registry_request_stream
                .next()
                .await
                .context("stream read should yield Some")?
                .context("fidl read")?
            {
                InputDeviceRegistryRequest::Register { device, .. } => device,
            }
            .into_proxy()
            .context("converting client_end to proxy")?;

            // Send a `GetDescriptor` request to `input_device`, and verify that the device
            // is as keyboard.
            let input_device_get_descriptor_fut = input_device_proxy.get_descriptor();
            let input_device_server_fut = input_device.serve_reports();
            std::mem::drop(input_device_proxy); // Terminate stream served by `input_device_server_fut`.

            let (_server_result, get_descriptor_result) =
                futures::future::join(input_device_server_fut, input_device_get_descriptor_fut)
                    .await;
            get_descriptor_result.map_err(anyhow::Error::from)
        };
        pin_mut!(test_fut);

        match executor.run_until_stalled(&mut test_fut) {
            Poll::Ready(r) => r,
            Poll::Pending => Err(format_err!("test did not complete")),
        }
    }

    // Because `input_synthesis` is a library, unimplemented features should yield `Error`s,
    // rather than panic!()-ing.
    mod unimplemented_methods {
        use super::*;

        #[test]
        fn add_media_buttons_device_yields_error() -> Result<(), Error> {
            let _executor = fuchsia_async::TestExecutor::new(); // Create TLS executor used by `endpoints`.
            let (proxy, _request_stream) =
                endpoints::create_proxy_and_stream::<InputDeviceRegistryMarker>()
                    .context("internal error creating InputDevice proxy and stream")?;
            assert!(InputDeviceRegistry { proxy }.add_media_buttons_device().is_err());
            Ok(())
        }
    }
}
