// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device::InputDevice,
    anyhow::{Context as _, Error},
    fidl::endpoints,
    fidl_fuchsia_input_injection::InputDeviceRegistryProxy,
    fidl_fuchsia_input_report::{
        Axis, ContactInputDescriptor, DeviceDescriptor, InputDeviceMarker, Range, TouchDescriptor,
        TouchInputDescriptor, TouchType, Unit, UnitType,
    },
    std::convert::TryFrom,
};

/// Implements the client side of the `fuchsia.input.injection.InputDeviceRegistry` protocol.
pub(crate) struct InputDeviceRegistry {
    proxy: InputDeviceRegistryProxy,
}

impl InputDeviceRegistry {
    pub fn new(proxy: InputDeviceRegistryProxy) -> Self {
        Self { proxy }
    }

    /// Registers a touchscreen device, with in injection coordinate space that spans [-1000, 1000]
    /// on both axes.
    /// # Returns
    /// A `input_device::InputDevice`, which can be used to send events to the
    /// `fuchsia.input.report.InputDevice` that has been registered with the
    /// `fuchsia.input.injection.InputDeviceRegistry` service.
    pub fn add_touchscreen_device(&mut self) -> Result<InputDevice, Error> {
        const MAX_CONTACTS: u32 = 255;
        self.add_device(DeviceDescriptor {
            touch: Some(TouchDescriptor {
                input: Some(TouchInputDescriptor {
                    contacts: Some(
                        std::iter::repeat(ContactInputDescriptor {
                            position_x: Some(Axis {
                                range: Range { min: -1000, max: 1000 },
                                unit: Unit { type_: UnitType::Other, exponent: 0 },
                            }),
                            position_y: Some(Axis {
                                range: Range { min: -1000, max: 1000 },
                                unit: Unit { type_: UnitType::Other, exponent: 0 },
                            }),
                            contact_width: Some(Axis {
                                range: Range { min: -1000, max: 1000 },
                                unit: Unit { type_: UnitType::Other, exponent: 0 },
                            }),
                            contact_height: Some(Axis {
                                range: Range { min: -1000, max: 1000 },
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

    /// Adds a device to the `InputDeviceRegistry` FIDL server connected to this
    /// `InputDeviceRegistry` struct.
    ///
    /// # Returns
    /// A `input_device::InputDevice`, which can be used to send events to the
    /// `fuchsia.input.report.InputDevice` that has been registered with the
    /// `fuchsia.input.injection.InputDeviceRegistry` service.
    fn add_device(&self, descriptor: DeviceDescriptor) -> Result<InputDevice, Error> {
        let (client_end, request_stream) = endpoints::create_request_stream::<InputDeviceMarker>()?;
        self.proxy.register(client_end)?;
        Ok(InputDevice::new(request_stream, descriptor))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::input_device::InputDevice,
        anyhow::format_err,
        fidl_fuchsia_input_injection::{InputDeviceRegistryMarker, InputDeviceRegistryRequest},
        fuchsia_async as fasync,
        futures::{pin_mut, task::Poll, StreamExt},
        test_case::test_case,
    };

    #[test_case(&|registry| InputDeviceRegistry::add_touchscreen_device(registry);
                "touchscreen_device")]
    fn add_device_invokes_fidl_register_method_exactly_once(
        add_device_method: &dyn Fn(&mut super::InputDeviceRegistry) -> Result<InputDevice, Error>,
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
        assert_matches::assert_matches!(
            requests.as_slice(),
            [Ok(InputDeviceRegistryRequest::Register { .. })]
        );

        Ok(())
    }

    #[test_case(&|registry| InputDeviceRegistry::add_touchscreen_device(registry) =>
                matches Ok(DeviceDescriptor {
                    touch: Some(TouchDescriptor {
                        input: Some(TouchInputDescriptor { .. }),
                        ..
                    }),
                    .. });
                "touchscreen_device")]
    fn add_device_registers_correct_device_type(
        add_device_method: &dyn Fn(&mut super::InputDeviceRegistry) -> Result<InputDevice, Error>,
    ) -> Result<DeviceDescriptor, Error> {
        let mut executor = fasync::TestExecutor::new().context("creating executor")?;
        // Create an `InputDeviceRegistry`, and add a device to it.
        let (registry_proxy, mut registry_request_stream) =
            endpoints::create_proxy_and_stream::<InputDeviceRegistryMarker>()
                .context("failed to create proxy and stream for InputDeviceRegistry")?;
        let mut input_device_registry = InputDeviceRegistry { proxy: registry_proxy };
        let input_device =
            add_device_method(&mut input_device_registry).context("adding input device")?;

        let test_fut = async {
            // `input_device_registry` should send a `Register` messgage to `registry_request_stream`.
            // Use `registry_request_stream` to grab the `ClientEnd` of the device added above,
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
            // is the expected type.
            let input_device_get_descriptor_fut = input_device_proxy.get_descriptor();
            let input_device_server_fut = input_device.flush();
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
}
