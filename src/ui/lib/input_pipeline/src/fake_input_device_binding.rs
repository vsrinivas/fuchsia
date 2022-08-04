// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device, crate::keyboard_binding, anyhow::Error, async_trait::async_trait,
    fidl_fuchsia_ui_input_config::FeaturesRequest as InputConfigFeaturesRequest,
    futures::channel::mpsc::Sender,
};

/// A fake [`InputDeviceBinding`] for testing.
pub struct FakeInputDeviceBinding {
    /// The channel to stream InputEvents to.
    event_sender: Sender<input_device::InputEvent>,

    /// The channel to stream received input config requests.
    input_config_requests_sender: Sender<InputConfigFeaturesRequest>,
}

#[allow(dead_code)]
impl FakeInputDeviceBinding {
    pub fn new(
        input_event_sender: Sender<input_device::InputEvent>,
        input_config_requests_sender: Sender<InputConfigFeaturesRequest>,
    ) -> Self {
        FakeInputDeviceBinding { event_sender: input_event_sender, input_config_requests_sender }
    }
}

#[async_trait]
impl input_device::InputDeviceBinding for FakeInputDeviceBinding {
    fn get_device_descriptor(&self) -> input_device::InputDeviceDescriptor {
        input_device::InputDeviceDescriptor::Keyboard(keyboard_binding::KeyboardDeviceDescriptor {
            keys: vec![],
            device_info: fidl_fuchsia_input_report::DeviceInfo {
                vendor_id: 42,
                product_id: 43,
                version: 44,
            },
            // Random fake identifier.
            device_id: 442,
        })
    }

    fn input_event_sender(&self) -> Sender<input_device::InputEvent> {
        self.event_sender.clone()
    }

    async fn handle_input_config_request(
        &self,
        request: &InputConfigFeaturesRequest,
    ) -> Result<(), Error> {
        let copied = match request {
            InputConfigFeaturesRequest::SetTouchpadMode { enable, control_handle } => {
                InputConfigFeaturesRequest::SetTouchpadMode {
                    enable: *enable,
                    control_handle: control_handle.to_owned(),
                }
            }
        };

        self.input_config_requests_sender
            .clone()
            .try_send(copied)
            .expect("send input_config_request");
        Ok(())
    }
}
