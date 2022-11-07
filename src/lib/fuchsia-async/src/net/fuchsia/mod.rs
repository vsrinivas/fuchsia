// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(missing_docs)]

mod tcp;
pub use self::tcp::*;

mod udp;
pub use self::udp::*;

use fuchsia_zircon::{self as zx, AsHandleRef};
use futures::io::{self, AsyncRead, AsyncWrite};
use futures::ready;
use futures::task::{AtomicWaker, Context};
use libc;

use std::fmt;
use std::io::{Read, Write};
use std::marker::Unpin;
use std::mem;
use std::os::unix::io::{AsRawFd, RawFd};
use std::pin::Pin;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;
use std::task::Poll;

use crate::runtime::{EHandle, PacketReceiver, ReceiverRegistration};

const READABLE: usize = libc::EPOLLIN as usize;
const WRITABLE: usize = libc::EPOLLOUT as usize;
const ERROR: usize = libc::EPOLLERR as usize;
const HUP: usize = libc::EPOLLHUP as usize;

// Unsafe to use. `receive_packet` must not be called after
// `fdio` is invalidated.
pub(crate) struct EventedFdPacketReceiver {
    fdio: *const syscall::fdio_t,
    signals: AtomicUsize,
    read_task: AtomicWaker,
    write_task: AtomicWaker,
}

// Needed because of the fdio pointer.
// It is safe to send because the `EventedFdPacketReceiver` must be
// deregistered (and therefore `receive_packet` never called again)
// before `fdio_unsafe_release` is called.
unsafe impl Send for EventedFdPacketReceiver {}
unsafe impl Sync for EventedFdPacketReceiver {}

impl PacketReceiver for EventedFdPacketReceiver {
    fn receive_packet(&self, packet: zx::Packet) {
        let observed_signals = if let zx::PacketContents::SignalOne(p) = packet.contents() {
            p.observed()
        } else {
            return;
        };

        let mut events: u32 = 0;
        unsafe {
            syscall::fdio_unsafe_wait_end(self.fdio, observed_signals.bits(), &mut events);
        }
        let events = events as usize;

        let old = self.signals.fetch_or(events, Ordering::SeqCst);
        let became_readable = ((events & READABLE) != 0) && ((old & READABLE) == 0);
        let became_writable = ((events & WRITABLE) != 0) && ((old & WRITABLE) == 0);
        let err_occurred = (events & (ERROR | HUP)) != 0;

        if became_readable || err_occurred {
            self.read_task.wake();
        }
        if became_writable || err_occurred {
            self.write_task.wake();
        }
    }
}

/// A type which can be used for receiving IO events for a file descriptor.
pub struct EventedFd<T> {
    inner: T,
    // Must be valid, acquired from `fdio_unsafe_fd_to_io`
    fdio: *const syscall::fdio_t,
    // Must be dropped before `fdio_unsafe_release` is called
    signal_receiver: mem::ManuallyDrop<ReceiverRegistration<EventedFdPacketReceiver>>,
}

unsafe impl<T> Send for EventedFd<T> where T: Send {}
unsafe impl<T> Sync for EventedFd<T> where T: Sync {}

impl<T> Unpin for EventedFd<T> {}

impl<T> Drop for EventedFd<T> {
    fn drop(&mut self) {
        unsafe {
            // Drop the receiver so `packet_receive` may not be called again.
            mem::ManuallyDrop::drop(&mut self.signal_receiver);

            // Release the fdio
            syscall::fdio_unsafe_release(self.fdio);
        }

        // Then `inner` gets dropped
    }
}

impl<T: fmt::Debug> fmt::Debug for EventedFd<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // FIXME(https://github.com/rust-lang/rust/issues/67364): This could be
        // better written with `.finish_non_exhaustive()` once that feature is
        // stablized.
        f.debug_struct("EventedFd").field("inner", &self.inner).finish()
    }
}

impl<T> EventedFd<T>
where
    T: AsRawFd,
{
    /// Creates a new EventedFd.
    ///
    /// # Safety
    ///
    /// The raw file descriptor returned from `inner.as_raw_fd()` must not be
    /// closed until the returned `EventedFd` is dropped.
    pub unsafe fn new(inner: T) -> io::Result<Self> {
        let fdio = syscall::fdio_unsafe_fd_to_io(inner.as_raw_fd());
        let signal_receiver =
            EHandle::local().register_receiver(Arc::new(EventedFdPacketReceiver {
                fdio,
                // Optimistically assume that the fd is readable and writable.
                // Reads and writes will be attempted before queueing a packet.
                // This makes fds slightly faster to read/write the first time
                // they're accessed after being created, provided they start off as
                // readable or writable. In return, there will be an extra wasted
                // syscall per read/write if the fd is not readable or writable.
                signals: AtomicUsize::new(READABLE | WRITABLE),
                read_task: AtomicWaker::new(),
                write_task: AtomicWaker::new(),
            }));

        let evented_fd =
            EventedFd { inner, fdio, signal_receiver: mem::ManuallyDrop::new(signal_receiver) };

        // Make sure a packet is delivered if an error or closure occurs.
        evented_fd.schedule_packet(ERROR | HUP);

        // Need to schedule packets to maintain the invariant that
        // if !READABLE or !WRITABLE a packet has been scheduled.
        evented_fd.schedule_packet(READABLE);
        evented_fd.schedule_packet(WRITABLE);

        Ok(evented_fd)
    }
    /// Tests to see if this resource is ready to be read from.
    /// If it is not, it arranges for the current task to receive a notification
    /// when a "readable" signal arrives.
    pub fn poll_readable(&self, cx: &mut Context<'_>) -> Poll<Result<(), zx::Status>> {
        let receiver = self.signal_receiver.receiver();
        if (receiver.signals.load(Ordering::SeqCst) & (READABLE | ERROR | HUP)) != 0 {
            Poll::Ready(Ok(()))
        } else {
            self.need_read(cx);
            Poll::Pending
        }
    }

    /// Tests to see if this resource is ready to be written to.
    /// If it is not, it arranges for the current task to receive a notification
    /// when a "writable" signal arrives.
    pub fn poll_writable(&self, cx: &mut Context<'_>) -> Poll<Result<(), zx::Status>> {
        let receiver = self.signal_receiver.receiver();
        if (receiver.signals.load(Ordering::SeqCst) & (WRITABLE | ERROR | HUP)) != 0 {
            Poll::Ready(Ok(()))
        } else {
            self.need_write(cx);
            Poll::Pending
        }
    }

    // Returns a reference to the underlying IO object.
    pub fn as_ref(&self) -> &T {
        &self.inner
    }

    // Returns a mutable reference to the underlying IO object.
    pub fn as_mut(&mut self) -> &mut T {
        &mut self.inner
    }

    /// Arranges for the current task to receive a notification when a "readable"
    /// signal arrives.
    pub fn need_read(&self, cx: &mut Context<'_>) {
        let receiver = self.signal_receiver.receiver();
        receiver.read_task.register(cx.waker());
        let old = receiver.signals.fetch_and(!READABLE, Ordering::SeqCst);
        // We only need to schedule a new packet if one isn't already scheduled.
        // If READABLE was already false, a packet was already scheduled.
        if (old & READABLE) != 0 {
            self.schedule_packet(READABLE);
        }
    }

    /// Arranges for the current task to receive a notification when a "writable"
    /// signal arrives.
    pub fn need_write(&self, cx: &mut Context<'_>) {
        let receiver = self.signal_receiver.receiver();
        receiver.write_task.register(cx.waker());
        let old = receiver.signals.fetch_and(!WRITABLE, Ordering::SeqCst);
        // We only need to schedule a new packet if one isn't already scheduled.
        // If WRITABLE was already false, a packet was already scheduled.
        if (old & WRITABLE) != 0 {
            self.schedule_packet(WRITABLE);
        }
    }

    fn schedule_packet(&self, signals: usize) {
        unsafe {
            let (mut raw_handle, mut raw_signals) = (0, 0);
            syscall::fdio_unsafe_wait_begin(
                self.fdio,
                signals as u32,
                &mut raw_handle,
                &mut raw_signals,
            );

            let handle = zx::Handle::from_raw(raw_handle);
            let signals = zx::Signals::from_bits_truncate(raw_signals);

            let res = handle.wait_async_handle(
                self.signal_receiver.port(),
                self.signal_receiver.key(),
                signals,
                zx::WaitAsyncOpts::empty(),
            );

            // The handle is borrowed, so we cannot drop it.
            mem::forget(handle);
            res.expect("Error scheduling EventedFd notification");
        }
    }

    /// Clears all incoming signals.
    pub fn clear(&self) {
        self.signal_receiver.receiver().signals.store(0, Ordering::SeqCst);
    }
}

impl<T: AsRawFd> AsRawFd for EventedFd<T> {
    fn as_raw_fd(&self) -> RawFd {
        self.as_ref().as_raw_fd()
    }
}

impl<T: AsRawFd + Read> AsyncRead for EventedFd<T> {
    fn poll_read(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &mut [u8],
    ) -> Poll<Result<usize, io::Error>> {
        ready!(EventedFd::poll_readable(&*self, cx))?;
        let res = (&mut *self).as_mut().read(buf);
        if let Err(e) = &res {
            if e.kind() == io::ErrorKind::WouldBlock {
                self.need_read(cx);
                return Poll::Pending;
            }
        }
        Poll::Ready(res.map_err(Into::into))
    }

    // TODO: override poll_vectored_read and call readv on the underlying handle
}

impl<T: AsRawFd + Write> AsyncWrite for EventedFd<T> {
    fn poll_write(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<Result<usize, io::Error>> {
        ready!(EventedFd::poll_writable(&*self, cx))?;
        let res = (&mut *self).as_mut().write(buf);
        if let Err(e) = &res {
            if e.kind() == io::ErrorKind::WouldBlock {
                self.need_write(cx);
                return Poll::Pending;
            }
        }
        Poll::Ready(res.map_err(Into::into))
    }

    fn poll_flush(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<Result<(), io::Error>> {
        Poll::Ready(Ok(()))
    }

    fn poll_close(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<Result<(), io::Error>> {
        Poll::Ready(Ok(()))
    }

    // TODO: override poll_vectored_write and call writev on the underlying handle
}

impl<'a, T> AsyncRead for &'a EventedFd<T>
where
    T: AsRawFd,
    for<'b> &'b T: Read,
{
    fn poll_read(
        self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &mut [u8],
    ) -> Poll<Result<usize, io::Error>> {
        ready!(EventedFd::poll_readable(&*self, cx))?;
        let res = (&*self).as_ref().read(buf);
        if let Err(e) = &res {
            if e.kind() == io::ErrorKind::WouldBlock {
                self.need_read(cx);
                return Poll::Pending;
            }
        }
        Poll::Ready(res.map_err(Into::into))
    }
}

impl<'a, T> AsyncWrite for &'a EventedFd<T>
where
    T: AsRawFd,
    for<'b> &'b T: Write,
{
    fn poll_write(
        self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<Result<usize, io::Error>> {
        ready!(EventedFd::poll_writable(&*self, cx))?;
        let res = (&*self).as_ref().write(buf);
        if let Err(e) = &res {
            if e.kind() == io::ErrorKind::WouldBlock {
                self.need_write(cx);
                return Poll::Pending;
            }
        }
        Poll::Ready(res.map_err(Into::into))
    }

    fn poll_flush(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<Result<(), io::Error>> {
        Poll::Ready(Ok(()))
    }

    fn poll_close(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<Result<(), io::Error>> {
        Poll::Ready(Ok(()))
    }
}

mod syscall {
    #![allow(non_camel_case_types, improper_ctypes)]
    pub use fuchsia_zircon::sys::{zx_handle_t, zx_signals_t};
    use std::os::unix::io::RawFd;

    // This is the "improper" c type
    pub type fdio_t = ();

    #[link(name = "fdio")]
    extern "C" {
        pub fn fdio_unsafe_fd_to_io(fd: RawFd) -> *const fdio_t;
        pub fn fdio_unsafe_release(io: *const fdio_t);

        pub fn fdio_unsafe_wait_begin(
            io: *const fdio_t,
            events: u32,
            handle_out: &mut zx_handle_t,
            signals_out: &mut zx_signals_t,
        );

        pub fn fdio_unsafe_wait_end(io: *const fdio_t, signals: zx_signals_t, events_out: &mut u32);
    }
}
