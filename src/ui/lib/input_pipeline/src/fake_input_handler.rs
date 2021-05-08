// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device, crate::input_handler, async_trait::async_trait,
    futures::channel::mpsc::Sender,
};

/// A fake [`InputHandler`] used for testing. A [`FakeInputHandler`] does not consume InputEvents.
pub struct FakeInputHandler {
    /// Events received by [`handle_input_event()`] are sent to this channel.
    event_sender: Sender<input_device::InputEvent>,
}

#[allow(dead_code)]
impl FakeInputHandler {
    pub fn new(event_sender: Sender<input_device::InputEvent>) -> Self {
        FakeInputHandler { event_sender }
    }
}

#[async_trait]
impl input_handler::InputHandler for FakeInputHandler {
    async fn handle_input_event(
        &mut self,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match self.event_sender.try_send(input_event.clone()) {
            Err(_) => assert!(false),
            _ => {}
        };

        vec![input_event]
    }
}
