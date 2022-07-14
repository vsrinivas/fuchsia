// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_virtualization_hardware::{
        KeyboardListenerRequest, KeyboardListenerRequestStream,
    },
    futures::{
        channel::mpsc,
        select,
        stream::{Fuse, FusedStream},
        StreamExt,
    },
    machina_virtio_device::WrappedDescChainStream,
    std::pin::Pin,
    virtio_device::queue::DriverNotify,
};

pub struct InputDevice<'a, 'b, N: DriverNotify> {
    listener_receiver: mpsc::Receiver<KeyboardListenerRequestStream>,
    _event_stream: WrappedDescChainStream<'a, 'b, N>,
    status_stream: Fuse<WrappedDescChainStream<'a, 'b, N>>,
    keyboard_stream: Pin<Box<dyn FusedStream<Item = Result<KeyboardListenerRequest, fidl::Error>>>>,
}

impl<'a, 'b, N: DriverNotify> InputDevice<'a, 'b, N> {
    pub fn new(
        listener_receiver: mpsc::Receiver<KeyboardListenerRequestStream>,
        event_stream: WrappedDescChainStream<'a, 'b, N>,
        status_stream: WrappedDescChainStream<'a, 'b, N>,
    ) -> Self {
        Self {
            listener_receiver,
            _event_stream: event_stream,
            status_stream: status_stream.fuse(),
            // Initialize with an empty KeyboardStream. This will be updated when we receive a new
            // KeyboardListener FIDL connection.
            keyboard_stream: Box::pin(futures::stream::empty().fuse()),
        }
    }

    pub async fn run(&mut self) -> Result<(), Error> {
        loop {
            select! {
                // The listener_recevier will pass request streams that are created as a result of
                // connecting to our public KeyboardListener service. When we receive a stream we
                // update the keyboard request stream.
                //
                // We only support a single KeyboardListener connection and any previous connection
                // will be dropped.
                keyboard_listener = self.listener_receiver.next() => {
                    if let Some(stream) = keyboard_listener {
                        self.keyboard_stream = Box::pin(stream);
                    }
                },
                // This handles incoming key events from the KeyboardListener service. This will
                // attempt to decode the key event and produce 1 or more VirtioInputEvents to send
                // back to the driver.
                request = self.keyboard_stream.next() => {
                    if let Some(Ok(request)) = request {
                        self.handle_keyboard_listener_request(request);
                    }
                },
                _chain = self.status_stream.next() => {
                    // New status message
                },
            }
        }
    }

    fn handle_keyboard_listener_request(&self, request: KeyboardListenerRequest) {
        match request {
            KeyboardListenerRequest::OnKeyEvent { event: _event, responder } => {
                // TODO(fxbug.dev/104229): Translate `event` into wire::VirtioInputEvents.
                if let Err(e) = responder.send(fidl_fuchsia_ui_input3::KeyEventStatus::NotHandled) {
                    tracing::warn!("Failed to ack KeyEvent: {}", e);
                }
            }
            _ => {}
        }
    }
}
