// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, Ordering};

use futures::Poll;
use futures::task::{self, AtomicWaker};

use executor::{PacketReceiver, ReceiverRegistration, EHandle};
use zx::{self, AsHandleRef};

const READABLE: usize = 0b001;
const WRITABLE: usize = 0b010;
const CLOSED:   usize = 0b100;

struct RWPacketReceiver {
    signals: AtomicUsize,
    read_task: AtomicWaker,
    write_task: AtomicWaker,
}

impl PacketReceiver for RWPacketReceiver {
    fn receive_packet(&self, packet: zx::Packet) {
        let observed = if let zx::PacketContents::SignalOne(p) = packet.contents() {
            p.observed()
        } else { return };

        let new = 0 |
            (if observed.contains(zx::Signals::OBJECT_READABLE) { READABLE } else { 0 }) |
            (if observed.contains(zx::Signals::OBJECT_WRITABLE) { WRITABLE } else { 0 }) |
            (if observed.contains(zx::Signals::OBJECT_PEER_CLOSED) { CLOSED } else { 0 });

        let old = self.signals.fetch_or(new, Ordering::SeqCst);

        let became_readable = ((new & READABLE) != 0) && ((old & READABLE) == 0);
        let became_writable = ((new & WRITABLE) != 0) && ((old & WRITABLE) == 0);
        let became_closed = ((new & CLOSED) != 0) && ((old & CLOSED) == 0);

        if became_readable || became_closed {
            self.read_task.wake();
        }
        if became_writable || became_closed {
            self.write_task.wake();
        }
    }
}

/// A `Handle` that receives notifications when it is readable/writable.
pub struct RWHandle<T> {
    handle: T,
    receiver: ReceiverRegistration<RWPacketReceiver>,
}

impl<T> RWHandle<T> where T: AsHandleRef {
    /// Creates a new `RWHandle` object which will receive notifications when
    /// the underlying handle becomes readable, writable, or closes.
    pub fn new(handle: T) -> Result<Self, zx::Status> {
        let ehandle = EHandle::local();

        let receiver = ehandle.register_receiver(Arc::new(RWPacketReceiver {
            // Optimistically assume that the handle is readable and writable.
            // Reads and writes will be attempted before queueing a packet.
            // This makes handles slightly faster to read/write the first time
            // they're accessed after being created, provided they start off as
            // readable or writable. In return, there will be an extra wasted
            // syscall per read/write if the handle is not readable or writable.
            signals: AtomicUsize::new(READABLE | WRITABLE),
            read_task: AtomicWaker::new(),
            write_task: AtomicWaker::new(),
        }));

        let rwhandle = RWHandle {
            handle,
            receiver,
        };

        // Make sure we get notifications when the handle closes.
        rwhandle.schedule_packet(zx::Signals::OBJECT_PEER_CLOSED)?;

        Ok(rwhandle)
    }

    /// Returns a reference to the underlying handle.
    pub fn get_ref(&self) -> &T {
        &self.handle
    }

    /// Returns a mutable reference to the underlying handle.
    pub fn get_mut(&mut self) -> &mut T {
        &mut self.handle
    }

    /// Consumes this type, returning the inner handle.
    pub fn into_inner(self) -> T {
        self.handle
    }

    /// Tests to see if the channel received a OBJECT_PEER_CLOSED signal
    pub fn is_closed(&self) -> bool {
        (self.receiver().signals.load(Ordering::Relaxed) & CLOSED) != 0
    }

    /// Tests to see if this resource is ready to be read from.
    /// If it is not, it arranges for the current task to receive a notification
    /// when a "readable" signal arrives.
    pub fn poll_read(&self, cx: &mut task::Context) -> Poll<Result<(), zx::Status>> {
        if (self.receiver().signals.load(Ordering::SeqCst) & (READABLE | CLOSED)) != 0 {
            Poll::Ready(Ok(()))
        } else {
            self.need_read(cx)?;
            Poll::Pending
        }
    }

    /// Tests to see if this resource is ready to be read from.
    /// If it is not, it arranges for the current task to receive a notification
    /// when a "writable" signal arrives.
    pub fn poll_write(&self, cx: &mut task::Context) -> Poll<Result<(), zx::Status>> {
        if (self.receiver().signals.load(Ordering::SeqCst) & (WRITABLE | CLOSED)) != 0 {
            Poll::Ready(Ok(()))
        } else {
            self.need_write(cx)?;
            Poll::Pending
        }
    }

    fn receiver(&self) -> &RWPacketReceiver {
        self.receiver.receiver()
    }

    /// Arranges for the current task to receive a notification when a
    /// "readable" signal arrives.
    pub fn need_read(&self, cx: &mut task::Context) -> Result<(), zx::Status> {
        self.receiver().read_task.register(cx.waker());
        let old = self.receiver().signals.fetch_and(!READABLE, Ordering::SeqCst);
        // We only need to schedule a new packet if one isn't already scheduled.
        // If READABLE was already false, a packet was already scheduled.
        if (old & READABLE) != 0 {
            self.schedule_packet(zx::Signals::OBJECT_READABLE)?;
        }
        Ok(())
    }

    /// Arranges for the current task to receive a notification when a
    /// "writable" signal arrives.
    pub fn need_write(&self, cx: &mut task::Context) -> Result<(), zx::Status> {
        self.receiver().write_task.register(cx.waker());
        let old = self.receiver().signals.fetch_and(!WRITABLE, Ordering::SeqCst);
        // We only need to schedule a new packet if one isn't already scheduled.
        // If WRITABLE was already false, a packet was already scheduled.
        if (old & WRITABLE) != 0 {
            self.schedule_packet(zx::Signals::OBJECT_WRITABLE)?;
        }
        Ok(())
    }

    fn schedule_packet(&self, signals: zx::Signals) -> Result<(), zx::Status> {
        self.handle.wait_async_handle(
            self.receiver.port(),
            self.receiver.key(),
            signals,
            zx::WaitAsyncOpts::Once,
        )
    }
}
