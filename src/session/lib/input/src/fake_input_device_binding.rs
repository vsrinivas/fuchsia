// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    crate::keyboard,
    async_trait::async_trait,
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
}
