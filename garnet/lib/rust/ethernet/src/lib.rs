// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Fuchsia Ethernet client

#![deny(missing_docs)]

use bitflags::bitflags;
use fidl_fuchsia_hardware_ethernet as sys;
use fidl_fuchsia_hardware_ethernet_ext::{EthernetInfo, EthernetStatus};
use fuchsia_async as fasync;
use fuchsia_zircon::{self as zx, AsHandleRef};
use futures::{
    ready,
    task::{Context, Waker},
    FutureExt, Stream,
};

use std::fs::File;
use std::marker::Unpin;
use std::os::unix::io::AsRawFd;
use std::pin::Pin;
use std::sync::{Arc, Mutex};
use std::task::Poll;

mod buffer;
mod ethernet_sys;

bitflags! {
    /// Status flags describing the result of queueing a packet to an Ethernet device.
    #[repr(transparent)]
    pub struct EthernetQueueFlags: u16 {
        /// The packet was received correctly.
        const RX_OK   = ethernet_sys::ETH_FIFO_RX_OK as u16;
        /// The packet was transmitted correctly.
        const TX_OK   = ethernet_sys::ETH_FIFO_TX_OK as u16;
        /// The packet was out of the bounds of the memory shared with the Ethernet device driver.
        const INVALID = ethernet_sys::ETH_FIFO_INVALID as u16;
        /// The received packet was sent by this host.
        ///
        /// This bit is only set after `tx_listen_start` is called.
        const TX_ECHO = ethernet_sys::ETH_FIFO_RX_TX as u16;
    }
}

/// Default buffer size communicating with Ethernet devices.
///
/// It is the smallest power of 2 greater than the Ethernet MTU of 1500.
pub const DEFAULT_BUFFER_SIZE: usize = 2048;

/// An Ethernet client that communicates with a device driver to send and receive packets.
#[derive(Debug)]
pub struct Client {
    inner: Arc<ClientInner>,
}

impl Client {
    /// Create a new Ethernet client for the device proxy `dev`.
    ///
    /// The `buf` is used to share memory between this process and the device driver, and must
    /// remain valid as long as this client is valid. The `name` is used by the driver for debug
    /// logs.
    ///
    /// TODO(tkilbourn): handle the buffer size better. How does the user of this crate know what
    /// to pass, before the device is opened?
    pub async fn new(
        dev: sys::DeviceProxy,
        buf: zx::Vmo,
        buf_size: usize,
        name: &str,
    ) -> Result<Self, anyhow::Error> {
        zx::Status::ok(dev.set_client_name(name).await?)?;
        let (status, fifos) = dev.get_fifos().await?;
        zx::Status::ok(status)?;
        // Safe because we checked the return status above.
        let fifos = *fifos.unwrap();
        {
            let buf = zx::Vmo::from(buf.as_handle_ref().duplicate(zx::Rights::SAME_RIGHTS)?);
            dev.set_io_buffer(buf).await?;
        }
        let pool = Mutex::new(buffer::BufferPool::new(buf, buf_size)?);
        Ok(Client { inner: Arc::new(ClientInner::new(dev, pool, fifos)?) })
    }

    /// Create a new Ethernet client for the device represented by the `dev` file.
    ///
    /// The `buf` is used to share memory between this process and the device driver, and must
    /// remain valid as long as this client is valid. The `name` is used by the driver for debug
    /// logs.
    ///
    /// TODO(tkilbourn): handle the buffer size better. How does the user of this crate know what
    /// to pass, before the device is opened?
    pub async fn from_file(
        dev: File,
        buf: zx::Vmo,
        buf_size: usize,
        name: &str,
    ) -> Result<Self, anyhow::Error> {
        let dev = dev.as_raw_fd();
        let mut client = 0;
        // Safe because we're passing a valid fd.
        zx::Status::ok(unsafe { fdio::fdio_sys::fdio_get_service_handle(dev, &mut client) })?;
        let dev = fidl::endpoints::ClientEnd::<sys::DeviceMarker>::new(
            // Safe because we checked the return status above.
            fuchsia_zircon::Channel::from(unsafe { fuchsia_zircon::Handle::from_raw(client) }),
        )
        .into_proxy()?;
        Client::new(dev, buf, buf_size, name).await
    }

    /// Get a stream of events from the Ethernet device.
    ///
    /// This is a default implementation using the various "poll" methods on this `Client`; more
    /// sophisticated uses should use those methods directly to implement a `Future` or `Stream`.
    pub fn get_stream(&self) -> EventStream {
        EventStream { inner: Arc::clone(&self.inner) }
    }

    /// Retrieve information about the Ethernet device.
    pub async fn info(&self) -> Result<EthernetInfo, fidl::Error> {
        let info = self.inner.dev.get_info().await?;
        Ok(info.into())
    }

    /// Start transferring packets.
    ///
    /// Before this is called, no packets will be transferred.
    pub async fn start(&self) -> Result<(), anyhow::Error> {
        let raw = self.inner.dev.start().await?;
        Ok(zx::Status::ok(raw)?)
    }

    /// Stop transferring packets.
    ///
    /// After this is called, no packets will be transferred.
    pub async fn stop(&self) -> Result<(), fidl::Error> {
        self.inner.dev.stop().await
    }

    /// Start receiving all packets transmitted by this host.
    ///
    /// Such packets will have the `EthernetQueueFlags::TX_ECHO` bit set.
    pub async fn tx_listen_start(&self) -> Result<(), anyhow::Error> {
        let raw = self.inner.dev.listen_start().await?;
        Ok(zx::Status::ok(raw)?)
    }

    /// Stop receiving all packets transmitted by this host.
    pub async fn tx_listen_stop(&self) -> Result<(), fidl::Error> {
        self.inner.dev.listen_stop().await
    }

    /// Get the status of the Ethernet device.
    pub async fn get_status(&self) -> Result<EthernetStatus, fidl::Error> {
        Ok(EthernetStatus::from_bits_truncate(self.inner.dev.get_status().await?))
    }

    /// Send a buffer with the Ethernet device.
    ///
    /// TODO(tkilbourn): indicate to the caller whether the send failed due to lack of buffers.
    pub fn send(&mut self, buf: &[u8]) {
        let mut guard = self.inner.pool.lock().unwrap();
        match guard.alloc_tx_buffer() {
            None => (),
            Some(mut t) => {
                t.write(buf);
                self.inner.push_tx(t.entry());
            }
        }
    }

    /// Poll the Ethernet client to see if the status has changed.
    pub fn poll_status(&self, cx: &mut Context<'_>) -> Poll<Result<zx::Signals, zx::Status>> {
        self.inner.poll_status(cx)
    }

    /// Poll the Ethernet client to queue any pending packets for transmit.
    pub fn poll_queue_tx(&self, cx: &mut Context<'_>) -> Poll<Result<usize, zx::Status>> {
        self.inner.poll_queue_tx(cx)
    }

    /// Poll the Ethernet client to complete any attempted packet transmissions.
    pub fn poll_complete_tx(
        &self,
        cx: &mut Context<'_>,
    ) -> Poll<Result<EthernetQueueFlags, zx::Status>> {
        self.inner.poll_complete_tx(cx)
    }

    /// Poll the Ethernet client to queue a buffer for receiving packets from the Ethernet device.
    pub fn poll_queue_rx(&self, cx: &mut Context<'_>) -> Poll<Result<(), zx::Status>> {
        self.inner.poll_queue_rx(cx)
    }

    /// Poll the Ethernet client to receive a packet from the Ethernet device.
    pub fn poll_complete_rx(
        &self,
        cx: &mut Context<'_>,
    ) -> Poll<Result<(buffer::RxBuffer, EthernetQueueFlags), zx::Status>> {
        self.inner.poll_complete_rx(cx)
    }
}

/// A stream of events from the Ethernet device.
pub struct EventStream {
    inner: Arc<ClientInner>,
}

impl Unpin for EventStream {}

/// An event from the Ethernet device.
#[derive(Debug)]
pub enum Event {
    /// The Ethernet device indicated that its status has changed.
    ///
    /// The `get_status` method may be used to retrieve the current status.
    StatusChanged,
    /// A RxBuffer was received from the Ethernet device.
    Receive(buffer::RxBuffer, EthernetQueueFlags),
}

impl Stream for EventStream {
    type Item = Result<Event, zx::Status>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        Poll::Ready(match ready!(self.poll_inner(cx)) {
            Ok(event) => Some(Ok(event)),
            Err(zx::Status::PEER_CLOSED) => None,
            Err(e) => Some(Err(e)),
        })
    }
}

impl EventStream {
    fn poll_inner(&mut self, cx: &mut Context<'_>) -> Poll<Result<Event, zx::Status>> {
        loop {
            if let Poll::Ready(signals) = self.inner.poll_status(cx)? {
                if signals.contains(zx::Signals::USER_0) {
                    return Poll::Ready(Ok(Event::StatusChanged));
                }
            }

            let mut progress = false;
            if self.inner.poll_queue_tx(cx)?.is_ready() {
                progress = true;
            }

            if self.inner.poll_queue_rx(cx)?.is_ready() {
                progress = true;
            }

            // Drain the completed tx queue to make all the buffers available.
            while self.inner.poll_complete_tx(cx)?.is_ready() {
                progress = true;
            }

            if let Poll::Ready((buf, flags)) = self.inner.poll_complete_rx(cx)? {
                return Poll::Ready(Ok(Event::Receive(buf, flags)));
            }

            if !progress {
                return Poll::Pending;
            }
        }
    }
}

#[derive(Debug)]
struct ClientInner {
    dev: sys::DeviceProxy,
    pool: Mutex<buffer::BufferPool>,
    rx_fifo: fasync::Fifo<buffer::FifoEntry>,
    tx_fifo: fasync::Fifo<buffer::FifoEntry>,
    rx_depth: u32,
    tx_depth: u32,
    tx_pending: Mutex<(Vec<buffer::FifoEntry>, Option<Waker>)>,
    signals: Mutex<fasync::OnSignals<'static>>,
}

impl ClientInner {
    fn new(
        dev: sys::DeviceProxy,
        pool: Mutex<buffer::BufferPool>,
        fifos: sys::Fifos,
    ) -> Result<Self, zx::Status> {
        let sys::Fifos { rx, tx, rx_depth, tx_depth } = fifos;
        let rx_fifo = fasync::Fifo::from_fifo(rx)?;
        let tx_fifo = fasync::Fifo::from_fifo(tx)?;
        let signals =
            Mutex::new(fasync::OnSignals::new(&rx_fifo, zx::Signals::USER_0).extend_lifetime());
        Ok(ClientInner {
            dev,
            pool,
            rx_fifo,
            tx_fifo,
            rx_depth,
            tx_depth,
            tx_pending: Mutex::new((vec![], None)),
            signals,
        })
    }

    fn push_tx(&self, entry: buffer::FifoEntry) {
        let mut tx_guard = self.tx_pending.lock().unwrap();
        tx_guard.0.push(entry);
        if let Some(waker) = &tx_guard.1 {
            waker.wake_by_ref();
        }
    }

    fn register_signals(&self) {
        *self.signals.lock().unwrap() =
            fasync::OnSignals::new(&self.rx_fifo, zx::Signals::USER_0).extend_lifetime();
    }

    /// Check for Ethernet device status changes.
    ///
    /// These changes are signaled on the rx fifo.
    fn poll_status(&self, cx: &mut Context<'_>) -> Poll<Result<zx::Signals, zx::Status>> {
        let signals = ready!(self.signals.lock().unwrap().poll_unpin(cx))?;
        self.register_signals();
        Poll::Ready(Ok(signals))
    }

    /// Write any pending transmits to the tx fifo.
    fn poll_queue_tx(&self, cx: &mut Context<'_>) -> Poll<Result<usize, zx::Status>> {
        let mut tx_guard = self.tx_pending.lock().unwrap();
        if tx_guard.0.len() > 0 {
            let result = self.tx_fifo.try_write(cx, &tx_guard.0[..])?;
            // It's possible that only some of the entries were queued, so split those off the
            // pending queue and save the rest for later.
            if let Poll::Ready(n) = result {
                tx_guard.0 = tx_guard.0.split_off(n);
                return Poll::Ready(Ok(n));
            }
        } else {
            // We want to wake up when something gets queued for transmit.
            tx_guard.1 = Some(cx.waker().clone());
        }
        Poll::Pending
    }

    /// Receive a tx completion entry from the Ethernet device.
    ///
    /// Returns the flags indicating success or failure.
    fn poll_complete_tx(
        &self,
        cx: &mut Context<'_>,
    ) -> Poll<Result<EthernetQueueFlags, zx::Status>> {
        match ready!(self.tx_fifo.try_read(cx))? {
            Some(buffer::FifoEntry { offset, flags, .. }) => {
                self.pool.lock().unwrap().release_tx_buffer(offset as usize);
                Poll::Ready(Ok(EthernetQueueFlags::from_bits_truncate(flags)))
            }
            None => Poll::Ready(Err(zx::Status::PEER_CLOSED)),
        }
    }

    /// Queue an available receive buffer to the rx fifo.
    fn poll_queue_rx(&self, cx: &mut Context<'_>) -> Poll<Result<(), zx::Status>> {
        ready!(self.rx_fifo.poll_write(cx))?;
        let mut pool_guard = self.pool.lock().unwrap();
        match pool_guard.alloc_rx_buffer() {
            None => Poll::Pending,
            Some(entry) => {
                let fifo_offset = entry.offset;
                let result = self.rx_fifo.try_write(cx, &[entry.into()])?;
                if let Poll::Pending = result {
                    // Map the buffer and drop it immediately, to return it to the set of available
                    // buffers.
                    let _ = pool_guard.map_rx_buffer(fifo_offset as usize, 0);
                }
                Poll::Ready(Ok(()))
            }
        }
    }

    /// Receive a buffer from the Ethernet device representing a packet from the network.
    fn poll_complete_rx(
        &self,
        cx: &mut Context<'_>,
    ) -> Poll<Result<(buffer::RxBuffer, EthernetQueueFlags), zx::Status>> {
        Poll::Ready(match ready!(self.rx_fifo.try_read(cx))? {
            Some(entry) => {
                let mut pool_guard = self.pool.lock().unwrap();
                let buf = pool_guard.map_rx_buffer(entry.offset as usize, entry.length as usize);
                Ok((buf, EthernetQueueFlags::from_bits_truncate(entry.flags)))
            }
            None => Err(zx::Status::PEER_CLOSED),
        })
    }
}
