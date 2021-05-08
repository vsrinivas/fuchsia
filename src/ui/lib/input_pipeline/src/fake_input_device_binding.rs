// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device, crate::keyboard, async_trait::async_trait, futures::channel::mpsc::Sender,
};

/// A fake [`InputDeviceBinding`] for testing.
pub struct FakeInputDeviceBinding {
    /// The channel to stream InputEvents to.
    event_sender: Sender<input_device::InputEvent>,
}

#[allow(dead_code)]
impl FakeInputDeviceBinding {
    pub fn new(input_event_sender: Sender<input_device::InputEvent>) -> Self {
        FakeInputDeviceBinding { event_sender: input_event_sender }
    }
}

#[async_trait]
impl input_device::InputDeviceBinding for FakeInputDeviceBinding {
    fn get_device_descriptor(&self) -> input_device::InputDeviceDescriptor {
        input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
            keys: vec![],
        })
    }

    fn input_event_sender(&self) -> Sender<input_device::InputEvent> {
        self.event_sender.clone()
    }
}
