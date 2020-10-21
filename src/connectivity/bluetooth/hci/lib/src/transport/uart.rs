// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::{future::FutureExt, stream::FusedStream, Sink, Stream},
    parking_lot::Mutex,
    std::{
        collections::VecDeque,
        pin::Pin,
        sync::Arc,
        task::{Context, Poll},
    },
};

use crate::{
    ffi::uart::{
        serial_impl_async_protocol_t, serial_read_async, serial_write_async, ReadAsyncFn, Serial,
        WriteAsyncFn,
    },
    log::*,
    transport::{
        HwTransport, HwTransportBuilder, IncomingPacket, IncomingPacketToken, OutgoingPacket,
    },
};

use self::parser::{
    consume_next_packet, encode_outgoing_packet, AclHeader, ParseError, UartHeader,
};

pub(crate) mod parse_util;
pub(crate) mod parser;

/// Allocate an initial buffer that is large enough under most situations.
/// TODO (56593): Dynamically configure initial capacity based on controller acl buffer size.
pub const ACL_BUFFER_INITIAL_CAPACITY: usize = UartHeader::SIZE + AclHeader::SIZE + 1024;

pub const IO_COMPLETE: zx::Signals = zx::Signals::USER_0;
pub const IO_ERROR: zx::Signals = zx::Signals::USER_1;

/// A bitmask with the signals used from serial read and write callback functions set
/// and all other bits cleared.
fn serial_signals() -> zx::Signals {
    IO_COMPLETE | IO_ERROR
}

/// Clear the relevant signal bits and return an `OnSignals` future that can be awaited on those
/// bits.
fn clear_and_create_signal_fut(e: &zx::Event) -> Result<fasync::OnSignals<'static>, zx::Status> {
    e.signal_handle(serial_signals(), zx::Signals::NONE)?;
    let s = fasync::OnSignals::new(e, serial_signals());
    Ok(s.extend_lifetime())
}

/// A struct that is manages the state that needs to be shared between the bt-transport driver and
/// the serial-impl driver.
pub struct SerialReadState {
    /// Data is pushed onto the end of the accumulation buffer immediately when read from the serial
    /// line. It is copied from the accumulation buffer for further processing. Once the data has
    /// been copied over, it is truncated. At this time, the buffer will be empty or the beginning
    /// of the buffer will be the start of the next HCI packet, including header.
    pub accumulation_buffer: VecDeque<u8>,
    /// Zircon Event object used to signal the worker thread when a complete packet is present in
    /// the accumulation buffer.
    pub event: zx::Event,
    /// Set to true if `event` has been signaled but not yet handled.
    pub signaled_ready: bool,
    /// A count of the total number of hci packets received from the controller over serial.
    /// This will wrap around to 0 if it ever exceeds `u64::MAX`. A current packet_count is used to
    /// give a the flow event for a packet an id when tracing is enabled.
    pub packet_count: u64,
    pub serial_ptr: *mut serial_impl_async_protocol_t,
}

impl SerialReadState {
    /// Return an arc-wrapped instance of `SerialReadState`. It is arc-wrapped because this object
    /// must be _always_ be Clone + Send + Sync.
    pub fn new(serial_ptr: *mut serial_impl_async_protocol_t) -> Arc<Mutex<SerialReadState>> {
        let event = zx::Event::create().expect("new event object");
        Arc::new(Mutex::new(SerialReadState {
            accumulation_buffer: VecDeque::with_capacity(ACL_BUFFER_INITIAL_CAPACITY),
            event,
            signaled_ready: false,
            packet_count: 0,
            serial_ptr,
        }))
    }

    pub fn push_read_bytes(&mut self, buffer: &[u8]) {
        self.accumulation_buffer.extend(buffer.iter());
    }

    // TODO (belgum): This will stall out if the following sequence of events occurs:
    // There is a currently unhandled packet in the accumulation buffer. A new complete packet is
    // read into the buffer. The first packet is handled. No futher data is read off the serial line
    // after the first packet is processed.
    // At this point, a signal will not be generated for the second packet that was accumulated in
    // the buffer until new data is read off the serial line to trigger a check for a new packet.
    pub fn check_for_next_packet(&mut self) -> Result<(), ()> {
        // Skip check if a packet is queued to be handled by `Worker`.
        //
        // This early return accomplishes two goals:
        //   1) Do not signal the worker thread again before the thread is able to handle
        //      the previously signaled event.
        //   2) Do not begin a new Transport::rx_packet flow tracing event until the previous
        //      flow has ended.
        if self.signaled_ready {
            return Ok(());
        }

        match parser::has_complete_packet(&self.accumulation_buffer) {
            Ok(()) => {
                trace_duration!("Transport::packet_ready");
                if let Err(e) = self.event.signal_handle(zx::Signals::NONE, IO_COMPLETE) {
                    bt_log_err!(
                        "Could not signal transport of serial read completion event: {:?}",
                        e
                    );
                    return Err(());
                }
                self.signaled_ready = true;
                self.packet_count = self.packet_count.wrapping_add(1);
                trace_flow_begin!("Transport::rx_packet", self.packet_count);
                Ok(())
            }
            Err(ParseError::PayloadTooShort) => Ok(()),
            Err(ParseError::InvalidPacketIndicator(b)) => {
                // Signal transport that an invalid byte has been encountered
                bt_log_err!(
                    "Unexpected packet type indicator '{}'. \
                    Controller out of sync or sending unsupported packet",
                    b
                );
                if let Err(e) = self.event.signal_handle(zx::Signals::NONE, IO_ERROR) {
                    bt_log_err!(
                        "Could not signal transport of serial read completion event: {:?}",
                        e
                    );
                }
                Err(())
            }
        }
    }
}

/// A `UartBuilder` is a `Send` type that implements the builder pattern for the `Uart` type
/// which is _not_ `Send`. It should be constructed using the `Uart::builder` function.
pub struct UartBuilder {
    s: Serial,
}

impl UartBuilder {
    pub fn new(s: Serial) -> UartBuilder {
        UartBuilder { s }
    }
}

impl HwTransportBuilder for UartBuilder {
    fn build(self: Box<Self>) -> Result<Box<dyn HwTransport>, zx::Status> {
        Ok(Box::new(Uart::new(self.s)))
    }
}

/// `Uart` stores the underlying uart serial object and all state necessary to track
/// read and write operations on that object. It implements the `HwTransport` trait necessary
/// to hook into the worker thread event loop.
///
/// It requires a valid `Serial` object that should be created by the consumer of the library
/// and passed in through the public FFI exposed by the library.
pub struct Uart {
    serial: Serial,
    terminated: bool,
    // write_signal is ordered above write_event because it should drop first.
    // There is no memory unsafety to dropping write_event before write_signal,
    // but to avoid any surprising behavior, the future is dropped before the event
    // it depends on.
    write_signal: fasync::OnSignals<'static>,
    write_event: Arc<zx::Event>,
    write_in_progress: bool,
    write_buffer: Vec<u8>,
    write_async: Box<WriteAsyncFn>,

    read_signal: fasync::OnSignals<'static>,
    read_state: Arc<Mutex<SerialReadState>>,
    read_in_progress: bool,
    read_async: Box<ReadAsyncFn>,
}

impl Uart {
    pub fn builder(serial: Serial) -> UartBuilder {
        UartBuilder::new(serial)
    }

    pub fn new(serial: Serial) -> Uart {
        let write_event = Arc::new(zx::Event::create().expect("zx event creation to succeed"));
        let write_event_clone = write_event.clone();
        let write_signal = fasync::OnSignals::new(&*write_event_clone, serial_signals());

        let read_state = SerialReadState::new(serial.as_ptr());
        let read_signal = {
            let read_state = read_state.lock();
            let read_signal = fasync::OnSignals::new(&read_state.event, serial_signals());
            read_signal.extend_lifetime()
        };

        Uart {
            serial,
            terminated: false,
            write_event,
            write_signal: write_signal.extend_lifetime(),
            write_in_progress: false,
            write_buffer: Vec::with_capacity(ACL_BUFFER_INITIAL_CAPACITY),
            write_async: Box::new(serial_write_async),
            read_signal,
            read_state,
            read_in_progress: false,
            read_async: Box::new(serial_read_async),
        }
    }

    /// Set up signal handlers to handle completion of a read and kick off a new async read
    /// operation.
    ///
    /// Set `poll_handler` to true to poll the signal handler immediately to register interest.
    ///
    /// Returns an error if the signal handler cannot be set up.
    fn start_read(&mut self) -> Result<(), zx::Status> {
        self.read_signal = clear_and_create_signal_fut(&self.read_state.lock().event)?;
        self.read_in_progress = true;
        (self.read_async)(self.serial.as_ptr(), self.read_state.clone());
        Ok(())
    }
}

impl Stream for Uart {
    type Item = IncomingPacketToken;
    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        trace_duration!("Worker::UartPollNext");
        if self.terminated {
            return Poll::Ready(None);
        }

        if !self.read_in_progress {
            if let Err(e) = self.start_read() {
                bt_log_err!("could not signal read event: {:?}", e);

                self.terminated = true;
                return Poll::Ready(None);
            }
        }

        match self.read_signal.poll_unpin(cx) {
            Poll::Ready(result) => {
                match result {
                    Ok(IO_COMPLETE) => {
                        let this: &mut Self = &mut self;
                        let state = this.read_state.lock();
                        this.read_signal = clear_and_create_signal_fut(&state.event).unwrap();
                        return Poll::Ready(Some(IncomingPacketToken::mint()));
                    }
                    Ok(signals) if signals.contains(IO_ERROR) => {
                        // The Stream implementation should never attempt to queue multiple
                        // simultaneous read operations. It is an implementation bug if this
                        // codepath is executed.
                        bt_log_warn!("Unxpected signal set, indicating that a read operation is already queued.");
                    }
                    Ok(signals) => {
                        // An unknown signal was set on the read event object.
                        bt_log_warn!("Unexpected signal set: {:?}", signals);
                    }
                    Err(e) => {
                        bt_log_warn!("Error polling event handle: {:?}", e);
                    }
                }
                // At this point there was an error in the read callback that cannot be recovered
                // from.
                self.terminated = true;
                Poll::Ready(None)
            }
            Poll::Pending => Poll::Pending,
        }
    }
}

impl FusedStream for Uart {
    fn is_terminated(&self) -> bool {
        self.terminated
    }
}

impl<'a> Sink<OutgoingPacket<'a>> for Uart {
    type Error = zx::Status;
    fn poll_ready(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        self.poll_flush(cx)
    }

    fn start_send(mut self: Pin<&mut Self>, item: OutgoingPacket<'_>) -> Result<(), Self::Error> {
        encode_outgoing_packet(item, &mut self.write_buffer);
        self.write_in_progress = true;
        (self.write_async)(&self.serial, &self.write_buffer, self.write_event.clone());
        Ok(())
    }

    fn poll_flush(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        if !self.write_in_progress {
            return Poll::Ready(Ok(()));
        }
        let res = self.write_signal.poll_unpin(cx);
        if res.is_ready() {
            self.write_in_progress = false;
            self.write_buffer.clear();
        }
        match res {
            Poll::Ready(Ok(IO_COMPLETE)) => {
                self.write_signal = clear_and_create_signal_fut(&self.write_event)?;
                Poll::Ready(Ok(()))
            }
            Poll::Ready(Ok(_)) => {
                bt_log_warn!("outgoing poll flush err");
                Poll::Ready(Err(zx::Status::INTERNAL))
            }
            Poll::Ready(Err(e)) => {
                bt_log_warn!("outgoing poll flush err");
                Poll::Ready(Err(e))
            }
            Poll::Pending => Poll::Pending,
        }
    }

    fn poll_close(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        self.poll_flush(cx)
    }
}

impl Unpin for Uart {}

impl HwTransport for Uart {
    fn take_incoming(
        &mut self,
        _proof: IncomingPacketToken,
        mut buffer: Vec<u8>,
    ) -> IncomingPacket {
        buffer.clear();
        let mut state = self.read_state.lock();
        match consume_next_packet(&mut state.accumulation_buffer, buffer) {
            Ok(pkt) => {
                state.signaled_ready = false;
                trace_instant!("Transport::packet_nonce_end", fuchsia_trace::Scope::Thread,
                    "id" => state.packet_count);
                // End of trace flow that began in `check_for_next_packet`
                // Because check_for_next_packet returns early if there is an unhandled packet,
                // there is never more than a single active Transport::rx_packet flow event at any
                // given point in time.
                trace_flow_end!("Transport::rx_packet", state.packet_count);
                pkt
            }
            Err(e) => {
                // Grab the first 4 bytes in the accumulation buffer for debugging purposes.
                // These bytes are expected to contain the beginning of the HCI packet header.
                let beginning = state.accumulation_buffer.iter().take(4).collect::<Vec<_>>();
                // Indicates a bug in the program's logic. A complete packet should always be
                // present if the caller has an `IncomingPacketToken` object.
                panic!(
                    "buffer of len ({}) did not contain full packet: {:?}. Error: {}",
                    state.accumulation_buffer.len(),
                    beginning,
                    e,
                );
            }
        }
    }
    unsafe fn unbind(&mut self) {
        self.serial.cancel_all();
    }
}

#[cfg(test)]
mod tests {
    use super::{parser::IncomingPacketIndicator, *};
    use {
        futures::{SinkExt, StreamExt},
        std::sync::atomic::{AtomicUsize, Ordering},
    };

    async fn create_new_signal() -> (Arc<zx::Event>, fasync::OnSignals<'static>) {
        let event = Arc::new(zx::Event::create().unwrap());
        event.signal_handle(zx::Signals::NONE, serial_signals()).unwrap();
        // Check that bits _are_ set here
        fasync::OnSignals::new(&*event, serial_signals()).await.unwrap();

        // Clear bits
        let signal = clear_and_create_signal_fut(&*event).unwrap();
        (event, signal)
    }

    #[fasync::run_until_stalled(test)]
    async fn create_new_signal_succeeds() {
        // This tests that a panic is not encountered when setting up a new signal
        let (_, _) = create_new_signal().await;
    }

    #[fasync::run_until_stalled(test)]
    #[should_panic]
    async fn clear_and_set_signals_works() {
        // `create_new_signal_succeeds` ensures this does not panic
        let (_event, signal) = create_new_signal().await;
        // signal stalls out and panics because no signal bits are set
        signal.await.unwrap();
    }

    #[fasync::run_until_stalled(test)]
    async fn stream_await_terminated_returns_none() {
        let mut transport = Uart::new(unsafe { Serial::fake() });
        transport.terminated = true;
        assert!(transport.next().await.is_none())
    }

    #[fasync::run_until_stalled(test)]
    async fn stream_await_ready_returns_value() {
        let expected = IncomingPacket::Event(vec![2, 3, 4, 5, 6]);
        let expected_ = expected.clone();
        let mut transport = Uart::new(unsafe { Serial::fake() });
        transport.read_async = Box::new(move |_, read_state| {
            read_state.lock().event.signal_handle(zx::Signals::NONE, IO_COMPLETE).unwrap();
            read_state.lock().accumulation_buffer.push_back(IncomingPacketIndicator::Event as u8);
            read_state.lock().accumulation_buffer.extend(expected_.clone().inner());
        });
        let token = transport.next().await.unwrap();
        assert_eq!(transport.take_incoming(token, vec![]), expected);

        // Still returns value with leftover data in buffer
        let expected = IncomingPacket::Event(vec![2, 3, 4, 5, 6]);
        let expected_ = expected.clone();
        let mut transport = Uart::new(unsafe { Serial::fake() });
        transport.read_async = Box::new(move |_, read_state| {
            read_state.lock().event.signal_handle(zx::Signals::NONE, IO_COMPLETE).unwrap();
            read_state.lock().accumulation_buffer.push_back(IncomingPacketIndicator::Event as u8);
            read_state.lock().accumulation_buffer.extend(expected_.clone().inner());
            read_state.lock().accumulation_buffer.extend(&[7, 8]);
        });
        let token = transport.next().await.unwrap();
        assert_eq!(transport.take_incoming(token, vec![]), expected);
    }

    #[fasync::run_until_stalled(test)]
    #[should_panic]
    async fn stream_await_partial_stalls() {
        let mut transport = Uart::new(unsafe { Serial::fake() });
        transport.read_async = Box::new(|_, _| {});

        // stalls
        transport.next().await;
    }

    #[fasync::run_until_stalled(test)]
    async fn stream_await_error_signal_returns_none() {
        let mut transport = Uart::new(unsafe { Serial::fake() });
        transport.read_async = Box::new(move |_, read_state| {
            read_state.lock().event.signal_handle(zx::Signals::NONE, IO_ERROR).unwrap();
        });
        assert!(transport.next().await.is_none());
    }

    #[fasync::run_until_stalled(test)]
    #[should_panic]
    async fn stream_await_unexpected_signal_stalls() {
        let mut transport = Uart::new(unsafe { Serial::fake() });
        transport.read_async = Box::new(move |_, read_state| {
            let unexpected_signal = zx::Signals::USER_7;
            read_state.lock().event.signal_handle(zx::Signals::NONE, unexpected_signal).unwrap();
        });
        // stalls
        transport.next().await;
    }

    #[fasync::run_until_stalled(test)]
    async fn sink_send_await_succeeds() {
        let mut transport = Uart::new(unsafe { Serial::fake() });
        transport.write_async = Box::new(move |_, buffer, event| {
            assert_eq!(buffer, &[1, 2, 3, 4, 5, 6]);
            event.signal_handle(zx::Signals::NONE, IO_COMPLETE).unwrap();
        });
        assert_eq!(transport.send(OutgoingPacket::Cmd(&[2, 3, 4, 5, 6])).await, Ok(()));
    }

    #[fasync::run_until_stalled(test)]
    async fn sink_send_await_fails_with_read_error() {
        let mut transport = Uart::new(unsafe { Serial::fake() });
        transport.write_async = Box::new(move |_, buffer, event| {
            assert_eq!(buffer, &[1, 2, 3, 4, 5, 6]);
            event.signal_handle(zx::Signals::NONE, IO_ERROR).unwrap();
        });
        assert_eq!(
            transport.send(OutgoingPacket::Cmd(&[2, 3, 4, 5, 6])).await,
            Err(zx::Status::INTERNAL)
        );
    }

    #[fasync::run_until_stalled(test)]
    #[should_panic]
    async fn sink_send_await_stalls_on_unexpected_signal() {
        let mut transport = Uart::new(unsafe { Serial::fake() });
        transport.write_async = Box::new(move |_, buffer, event| {
            assert_eq!(buffer, &[1, 2, 3, 4, 5, 6]);
            let unexpected_signal = zx::Signals::USER_7;
            event.signal_handle(zx::Signals::NONE, unexpected_signal).unwrap();
        });
        transport.send(OutgoingPacket::Cmd(&[2, 3, 4, 5, 6])).await.unwrap();
    }

    #[fasync::run_until_stalled(test)]
    #[should_panic]
    async fn sink_send_await_stalls_on_no_signal() {
        let mut transport = Uart::new(unsafe { Serial::fake() });
        transport.write_async = Box::new(move |_, buffer, _event| {
            assert_eq!(buffer, &[1, 2, 3, 4, 5, 6]);
        });
        transport.send(OutgoingPacket::Cmd(&[2, 3, 4, 5, 6])).await.unwrap();
    }

    #[fasync::run_until_stalled(test)]
    async fn sink_send_all_await_succeeds() {
        let mut transport = Uart::new(unsafe { Serial::fake() });
        let packets: Vec<&'static [u8]> = vec![&[2, 3, 4, 5, 6], &[0, 3, 0, 0, 0]];
        let packets_ = packets.clone();
        let index = AtomicUsize::new(0);

        transport.write_async = Box::new(move |_, buffer, event| {
            // insert packet indicator
            let mut packet = vec![1];
            // increment expected packet index;
            let i = index.fetch_add(1, Ordering::Relaxed);
            packet.extend_from_slice(packets_[i]);
            assert_eq!(packet, buffer);
            event.signal_handle(zx::Signals::NONE, IO_COMPLETE).unwrap();
        });
        // let mut packets = stream::iter(packets).map(OutgoingPacket::Cmd);
        // assert_eq!(transport.send_all(&mut packets).await, Ok(()));
    }

    #[fasync::run_until_stalled(test)]
    async fn sink_flush_await_succeeds() {
        let mut transport = Uart::new(unsafe { Serial::fake() });

        // Send some data
        transport.write_async = Box::new(move |_, buffer, event| {
            assert_eq!(buffer, &[1, 2, 3, 4, 5, 6]);
            event.signal_handle(zx::Signals::NONE, IO_COMPLETE).unwrap();
        });
        Pin::new(&mut transport).start_send(OutgoingPacket::Cmd(&[2, 3, 4, 5, 6])).unwrap();

        // successfully flush sink (data has already been
        assert_eq!(transport.flush().await, Ok(()));
    }

    #[fasync::run_until_stalled(test)]
    async fn sink_close_await_succeeds() {
        let mut transport = Uart::new(unsafe { Serial::fake() });

        // Send some data
        transport.write_async = Box::new(move |_, buffer, event| {
            assert_eq!(buffer, &[1, 2, 3, 4, 5, 6]);
            event.signal_handle(zx::Signals::NONE, IO_COMPLETE).unwrap();
        });
        assert_eq!(transport.send(OutgoingPacket::Cmd(&[2, 3, 4, 5, 6])).await, Ok(()));

        // successfully close sink
        assert_eq!(transport.close().await, Ok(()));
    }
}
