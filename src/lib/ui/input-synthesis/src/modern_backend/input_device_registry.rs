// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{modern_backend::input_device::InputDevice, synthesizer},
    anyhow::{format_err, Error},
    fidl::endpoints,
    fidl_fuchsia_input::Key,
    fidl_fuchsia_input_injection::InputDeviceRegistryProxy,
    fidl_fuchsia_input_report::{
        DeviceDescriptor, InputDeviceMarker, KeyboardDescriptor, KeyboardInputDescriptor,
    },
};

/// Implements the `synthesizer::InputDeviceRegistry` trait, and the client side
/// of the `fuchsia.input.injection.InputDeviceRegistry` protocol.
pub struct InputDeviceRegistry {
    proxy: InputDeviceRegistryProxy,
}

impl synthesizer::InputDeviceRegistry for self::InputDeviceRegistry {
    fn add_touchscreen_device(
        &mut self,
        _width: u32,
        _height: u32,
    ) -> Result<Box<dyn synthesizer::InputDevice>, Error> {
        Err(format_err!("TODO: implement touchscreen support"))
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
        anyhow::Context as _,
        fidl_fuchsia_input_injection::{InputDeviceRegistryMarker, InputDeviceRegistryRequest},
        fuchsia_async as fasync,
        futures::StreamExt,
        matches::assert_matches,
    };

    #[fasync::run_until_stalled(test)]
    async fn add_keyboard_device_invokes_fidl_register_method_exactly_once() -> Result<(), Error> {
        let (proxy, request_stream) =
            endpoints::create_proxy_and_stream::<InputDeviceRegistryMarker>()
                .context("failed to create proxy and stream for InputDeviceRegistry")?;
        InputDeviceRegistry { proxy }.add_keyboard_device().context("adding keyboard")?;
        assert_matches!(
            request_stream.collect::<Vec<_>>().await.as_slice(),
            [ Ok(InputDeviceRegistryRequest::Register { .. } )]);
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn add_keyboard_device_registers_a_keyboard() -> Result<(), Error> {
        // Create an `InputDeviceRegistry`, and add a keyboard to it.
        let (registry_proxy, mut registry_request_stream) =
            endpoints::create_proxy_and_stream::<InputDeviceRegistryMarker>()
                .context("failed to create proxy and stream for InputDeviceRegistry")?;
        let mut input_device_registry = InputDeviceRegistry { proxy: registry_proxy };
        let input_device =
            input_device_registry.add_keyboard_device().context("adding keyboard")?;

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
        assert_matches!(
            futures::future::join(input_device_server_fut, input_device_get_descriptor_fut).await,
            (_, Ok(DeviceDescriptor { keyboard: Some(_), .. } ))
        );
        Ok(())
    }

    // Because `input_synthesis` is a library, unimplemented features should yield `Error`s,
    // rather than panic!()-ing.
    mod unimplemented_methods {
        use super::*;

        #[test]
        fn add_touchscreen_device_yields_error() -> Result<(), Error> {
            let _executor = fuchsia_async::Executor::new(); // Create TLS executor used by `endpoints`.
            let (proxy, _request_stream) =
                endpoints::create_proxy_and_stream::<InputDeviceRegistryMarker>()
                    .context("internal error creating InputDevice proxy and stream")?;
            assert!(InputDeviceRegistry { proxy }.add_touchscreen_device(0, 0).is_err());
            Ok(())
        }

        #[test]
        fn add_media_buttons_device_yields_error() -> Result<(), Error> {
            let _executor = fuchsia_async::Executor::new(); // Create TLS executor used by `endpoints`.
            let (proxy, _request_stream) =
                endpoints::create_proxy_and_stream::<InputDeviceRegistryMarker>()
                    .context("internal error creating InputDevice proxy and stream")?;
            assert!(InputDeviceRegistry { proxy }.add_media_buttons_device().is_err());
            Ok(())
        }
    }
}
