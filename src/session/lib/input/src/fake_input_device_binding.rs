// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    crate::keyboard,
    anyhow::{format_err, Error},
    async_trait::async_trait,
    fidl_fuchsia_input_report::{InputDeviceProxy, InputReport},
    futures::channel::mpsc::{Receiver, Sender},
};

/// A fake [`InputDeviceBinding`] for testing.
pub struct FakeInputDeviceBinding {
    /// The channel to stream InputEvents to.
    event_sender: Sender<input_device::InputEvent>,

    /// The receiving end of the input event channel. Clients use this indirectly via
    /// [`input_event_stream()`].
    event_receiver: Receiver<input_device::InputEvent>,
}

#[allow(dead_code)]
impl FakeInputDeviceBinding {
    pub fn new() -> Self {
        let (event_sender, event_receiver) =
            futures::channel::mpsc::channel(input_device::INPUT_EVENT_BUFFER_SIZE);
        FakeInputDeviceBinding { event_sender, event_receiver }
    }
}

#[async_trait]
impl input_device::InputDeviceBinding for FakeInputDeviceBinding {
    async fn any_input_device() -> Result<InputDeviceProxy, Error>
    where
        Self: Sized,
    {
        Err(format_err!("Not implemented."))
    }

    async fn all_devices() -> Result<Vec<InputDeviceProxy>, Error>
    where
        Self: Sized,
    {
        Err(format_err!("Not implemented."))
    }

    async fn bind_device(_device: &InputDeviceProxy) -> Result<Self, Error> {
        Err(format_err!("Not implemented."))
    }

    fn get_device_descriptor(&self) -> input_device::InputDeviceDescriptor {
        input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
            keys: vec![],
        })
    }

    fn input_event_stream(&mut self) -> &mut Receiver<input_device::InputEvent> {
        return &mut self.event_receiver;
    }

    fn input_event_sender(&self) -> Sender<input_device::InputEvent> {
        self.event_sender.clone()
    }

    fn process_reports(
        _report: InputReport,
        _previous_report: Option<InputReport>,
        _device_descriptor: &input_device::InputDeviceDescriptor,
        _input_event_sender: &mut Sender<input_device::InputEvent>,
    ) -> Option<InputReport> {
        None
    }

    async fn any_device() -> Result<Self, Error> {
        Err(format_err!("Not implemented."))
    }

    async fn new(_device_proxy: InputDeviceProxy) -> Result<Self, Error> {
        Err(format_err!("Not implemented."))
    }

    fn initialize_report_stream(&self, _device_proxy: fidl_fuchsia_input_report::InputDeviceProxy) {
    }
}
