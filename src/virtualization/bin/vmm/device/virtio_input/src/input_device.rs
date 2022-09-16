// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::keyboard::translate_keyboard_event,
    crate::wire,
    anyhow::{anyhow, Error},
    fidl_fuchsia_virtualization_hardware::{
        KeyboardListenerRequest, KeyboardListenerRequestStream,
    },
    futures::{
        channel::mpsc,
        future::OptionFuture,
        select,
        stream::{Fuse, FusedStream, Stream},
        FutureExt, StreamExt,
    },
    std::{collections::VecDeque, io::Write, pin::Pin},
    virtio_device::{
        chain::WritableChain,
        mem::DriverMem,
        queue::{DescChain, DriverNotify},
    },
    zerocopy::AsBytes,
};

pub struct InputDevice<
    'a,
    'b,
    N: DriverNotify,
    M: DriverMem,
    Q: Stream<Item = DescChain<'a, 'b, N>> + Unpin,
> {
    listener_receiver: mpsc::Receiver<KeyboardListenerRequestStream>,
    event_stream: Q,
    status_stream: Option<Fuse<Q>>,
    keyboard_stream: Pin<Box<dyn FusedStream<Item = Result<KeyboardListenerRequest, fidl::Error>>>>,
    chain_buffer: VecDeque<DescChain<'a, 'b, N>>,
    mem: &'a M,
}

impl<'a, 'b, N: DriverNotify, M: DriverMem, Q: Stream<Item = DescChain<'a, 'b, N>> + Unpin>
    InputDevice<'a, 'b, N, M, Q>
{
    pub fn new(
        mem: &'a M,
        listener_receiver: mpsc::Receiver<KeyboardListenerRequestStream>,
        event_stream: Q,
        status_stream: Option<Q>,
    ) -> Self {
        Self {
            listener_receiver,
            event_stream,
            status_stream: status_stream.map(StreamExt::fuse),
            // Initialize with an empty KeyboardStream. This will be updated when we receive a new
            // KeyboardListener FIDL connection.
            keyboard_stream: Box::pin(futures::stream::empty().fuse()),
            chain_buffer: VecDeque::new(),
            mem,
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
                _chain = OptionFuture::from(self.status_stream.as_mut().map(StreamExt::next)) => {
                    // New status message
                },
            }
        }
    }

    fn handle_keyboard_listener_request(&mut self, request: KeyboardListenerRequest) {
        match request {
            KeyboardListenerRequest::OnKeyEvent { event, responder } => {
                let key_status = if let Some(mut events) = translate_keyboard_event(event) {
                    if let Err(e) = self.write_events_to_queue(&mut events) {
                        tracing::warn!("Failed to write events to the event queue: {}", e);
                        fidl_fuchsia_ui_input3::KeyEventStatus::NotHandled
                    } else {
                        fidl_fuchsia_ui_input3::KeyEventStatus::Handled
                    }
                } else {
                    fidl_fuchsia_ui_input3::KeyEventStatus::NotHandled
                };
                if let Err(e) = responder.send(key_status) {
                    tracing::warn!("Failed to ack KeyEvent: {}", e);
                }
            }
            _ => {}
        }
    }

    fn write_events_to_queue(
        &mut self,
        events: &mut [wire::VirtioInputEvent],
    ) -> Result<(), Error> {
        // Acquire a descriptor for each event. Do this first because we want to send all the
        // events as part of a group or none at all.
        //
        // 5.8.6.2 Device Requirements: Device Operation
        //
        // A device MAY drop input events if the eventq does not have enough available buffers. It
        // SHOULD NOT drop individual input events if they are part of a sequence forming one input
        // device update.
        while self.chain_buffer.len() < events.len() {
            // Don't block on the stream. We prefer instead to drop input events instead of sending
            // them with a long delay.
            match self.event_stream.next().now_or_never() {
                Some(Some(chain)) => self.chain_buffer.push_back(chain),
                _ => {
                    return Err(anyhow!("Not enough descriptors available"));
                }
            }
        }

        // Write all events to the queue.
        for event in events {
            // Unwrap here because we already checked that we have a chain for each event.
            let chain = self.chain_buffer.pop_front().unwrap();
            let mut chain = WritableChain::new(chain, self.mem)?;
            chain.write_all(event.as_bytes())?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::keyboard,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_input::Key,
        fidl_fuchsia_ui_input3 as input3,
        fidl_fuchsia_virtualization_hardware::KeyboardListenerMarker,
        futures::SinkExt,
        virtio_device::{
            fake_queue::{ChainBuilder, IdentityDriverMem, TestQueue},
            util::DescChainStream,
        },
        zerocopy::FromBytes,
    };

    fn read_returned<T: FromBytes>(range: (u64, u32)) -> T {
        let (data, len) = range;
        let slice =
            unsafe { std::slice::from_raw_parts::<u8>(data as usize as *const u8, len as usize) };
        T::read_from(slice).expect("Failed to read result from returned chain")
    }

    #[fuchsia::test]
    async fn test_send_key_press() {
        let mem = IdentityDriverMem::new();
        let mut event_queue = TestQueue::new(32, &mem);
        let status_queue = TestQueue::new(32, &mem);
        let (mut sender, receiver) = mpsc::channel(1);
        let mut device = InputDevice::new(
            &mem,
            receiver,
            DescChainStream::new(&event_queue.queue),
            Some(DescChainStream::new(&status_queue.queue)),
        );

        // Create a keyboard listener proxy and send the request stream to the device.
        let (keyboard_proxy, request_stream) =
            create_proxy_and_stream::<KeyboardListenerMarker>().unwrap();
        sender.send(request_stream).now_or_never().unwrap().unwrap();

        // Add two descriptors to the queue.
        event_queue
            .fake_queue
            .publish(
                ChainBuilder::new()
                    .writable(std::mem::size_of::<wire::VirtioInputEvent>() as u32, &mem)
                    .build(),
            )
            .unwrap();
        event_queue
            .fake_queue
            .publish(
                ChainBuilder::new()
                    .writable(std::mem::size_of::<wire::VirtioInputEvent>() as u32, &mem)
                    .build(),
            )
            .unwrap();

        // Now send a key event to the device over the KeyboardListener.
        let event = input3::KeyEvent {
            type_: Some(input3::KeyEventType::Pressed),
            key: Some(Key::W),
            ..input3::KeyEvent::EMPTY
        };
        // We need to select on both device.run and our proxy call because the device needs to be
        // polled to service the request.
        let result = select! {
            result = keyboard_proxy.on_key_event(event.clone()).fuse() => result.unwrap(),
            _result = device.run().fuse() => {
                panic!("device.run() exited while processing key event");
            }
        };

        // Expect the request was handled.
        assert_eq!(result, input3::KeyEventStatus::Handled);

        // Expect 2 events (a key press and a sync).
        let expected_events = keyboard::translate_keyboard_event(event).unwrap();

        assert_eq!(result, input3::KeyEventStatus::Handled);
        let returned = event_queue.fake_queue.next_used().unwrap();
        let mut iter = returned.data_iter();
        let returned_event = read_returned::<wire::VirtioInputEvent>(iter.next().unwrap());
        assert_eq!(expected_events[0], returned_event);

        let returned = event_queue.fake_queue.next_used().unwrap();
        let mut iter = returned.data_iter();
        let returned_event = read_returned::<wire::VirtioInputEvent>(iter.next().unwrap());
        assert_eq!(expected_events[1], returned_event);

        assert!(event_queue.fake_queue.next_used().is_none());
    }

    #[fuchsia::test]
    async fn test_drop_key_press_if_no_descriptors_are_available() {
        let mem = IdentityDriverMem::new();
        let mut event_queue = TestQueue::new(32, &mem);
        let status_queue = TestQueue::new(32, &mem);
        let (mut sender, receiver) = mpsc::channel(1);
        let mut device = InputDevice::new(
            &mem,
            receiver,
            DescChainStream::new(&event_queue.queue),
            Some(DescChainStream::new(&status_queue.queue)),
        );

        // Create a keyboard listener proxy and send the request stream to the device.
        let (keyboard_proxy, request_stream) =
            create_proxy_and_stream::<KeyboardListenerMarker>().unwrap();
        sender.send(request_stream).now_or_never().unwrap().unwrap();

        // Add only one descriptor to the queue. This will not be enough to generate a key press
        // event because we need 2 descriptors for that (one for the key event and one for the sync
        // event).
        event_queue
            .fake_queue
            .publish(
                ChainBuilder::new()
                    .writable(std::mem::size_of::<wire::VirtioInputEvent>() as u32, &mem)
                    .build(),
            )
            .unwrap();

        // Now send a key event to the device over the KeyboardListener. We expect this won't be
        // handled because jkj
        let event = input3::KeyEvent {
            type_: Some(input3::KeyEventType::Pressed),
            key: Some(Key::P),
            ..input3::KeyEvent::EMPTY
        };
        // We need to select on both device.run and our proxy call because the device needs to be
        // polled to service the request.
        let result = select! {
            result = keyboard_proxy.on_key_event(event).fuse() => result.unwrap(),
            _result = device.run().fuse() => {
                panic!("device.run() exited while processing key event");
            }
        };

        // Expect the request was not handled.
        assert_eq!(result, input3::KeyEventStatus::NotHandled);
        assert!(event_queue.fake_queue.next_used().is_none());

        // Add a second descriptor. Combined with the first descriptor this should allow the device
        // to now handle an event.
        event_queue
            .fake_queue
            .publish(
                ChainBuilder::new()
                    .writable(std::mem::size_of::<wire::VirtioInputEvent>() as u32, &mem)
                    .build(),
            )
            .unwrap();
        let event = input3::KeyEvent {
            type_: Some(input3::KeyEventType::Pressed),
            key: Some(Key::Q),
            ..input3::KeyEvent::EMPTY
        };
        let result = select! {
            result = keyboard_proxy.on_key_event(event.clone()).fuse() => result.unwrap(),
            _result = device.run().fuse() => {
                panic!("device.run() exited while processing key event");
            }
        };

        // Now we should have the event.
        let expected_events = keyboard::translate_keyboard_event(event).unwrap();

        assert_eq!(result, input3::KeyEventStatus::Handled);
        let returned = event_queue.fake_queue.next_used().unwrap();
        let mut iter = returned.data_iter();
        let returned_event = read_returned::<wire::VirtioInputEvent>(iter.next().unwrap());
        assert_eq!(expected_events[0], returned_event);

        let returned = event_queue.fake_queue.next_used().unwrap();
        let mut iter = returned.data_iter();
        let returned_event = read_returned::<wire::VirtioInputEvent>(iter.next().unwrap());
        assert_eq!(expected_events[1], returned_event);

        assert!(event_queue.fake_queue.next_used().is_none());
    }
}
