// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;
use std::marker::PhantomData;

use futures::{task, Async, Future, Poll};
use zx::{self, AsHandleRef};

use RWHandle;

/// Marker trait for types that can be read/written with a `Fifo`.
/// Unsafe because not all types may be represented by arbitrary bit patterns.
pub unsafe trait FifoEntry {}

/// An I/O object representing a `Fifo`.
pub struct Fifo<R: FifoEntry, W: FifoEntry = R> {
    handle: RWHandle<zx::Fifo>,
    read_marker: PhantomData<R>,
    write_marker: PhantomData<W>,
}

impl<R: FifoEntry, W: FifoEntry> AsRef<zx::Fifo> for Fifo<R, W> {
    fn as_ref(&self) -> &zx::Fifo {
        self.handle.get_ref()
    }
}

impl<R: FifoEntry, W: FifoEntry> AsHandleRef for Fifo<R, W> {
    fn as_handle_ref(&self) -> zx::HandleRef {
        self.handle.get_ref().as_handle_ref()
    }
}

impl<R: FifoEntry, W: FifoEntry> From<Fifo<R, W>> for zx::Fifo {
    fn from(fifo: Fifo<R, W>) -> zx::Fifo {
        fifo.handle.into_inner()
    }
}

impl<R: FifoEntry, W: FifoEntry> Fifo<R, W> {
    /// Creates a new `Fifo` from a previously-created `zx::Fifo`.
    pub fn from_fifo(fifo: zx::Fifo) -> Result<Self, zx::Status> {
        Ok(Fifo {
            handle: RWHandle::new(fifo)?,
            read_marker: PhantomData,
            write_marker: PhantomData,
        })
    }

    /// Test whether this fifo is ready to be read or not.
    ///
    /// If the fifo is *not* readable then the current task is scheduled to
    /// get a notification when the fifo does become readable. That is, this
    /// is only suitable for calling in a `Future::poll` method and will
    /// automatically handle ensuring a retry once the fifo is readable again.
    fn poll_read(&self, cx: &mut task::Context) -> Poll<(), zx::Status> {
        self.handle.poll_read(cx)
    }

    /// Reads an entry from the fifo and registers this `Fifo` as
    /// needing a read on receiving a `zx::Status::SHOULD_WAIT`.
    pub fn read(&self, cx: &mut task::Context) -> Poll<Option<R>, zx::Status> {
        try_ready!(self.poll_read(cx));
        let mut element = unsafe { ::std::mem::uninitialized() };
        let elembuf = unsafe {
            ::std::slice::from_raw_parts_mut(
                &mut element as *mut R as *mut u8,
                ::std::mem::size_of::<R>(),
            )
        };

        match self.as_ref().read(::std::mem::size_of::<R>(), elembuf) {
            Err(e) => {
                // Ensure `drop` isn't called on uninitialized memory.
                ::std::mem::forget(element);
                if e == zx::Status::SHOULD_WAIT {
                    self.handle.need_read(cx)?;
                    return Ok(Async::Pending);
                }
                if e == zx::Status::PEER_CLOSED {
                    return Ok(Async::Ready(None));
                }
                return Err(e);
            }
            Ok(count) => {
                debug_assert_eq!(1, count);
                return Ok(Async::Ready(Some(element)));
            }

        }
    }

    /// Creates a future that receives an entry to be written to the element
    /// provided.
    ///
    /// The returned future will return after an entry has been received on
    /// this fifo. The future will resolve to the fifo and the entry.
    ///
    /// An error during reading will cause the fifo and entry to get
    /// destroyed and the status will be returned.
    pub fn read_entry(self) -> ReadEntry<R, W> {
        ReadEntry {
            fifo: Some(self),
        }
    }

    /// Writes an entry into the fifo.
    pub fn write(&self, element: &W) -> Result<(), zx::Status> {
        let elembuf = unsafe {
            ::std::slice::from_raw_parts(
                element as *const W as *const u8,
                ::std::mem::size_of::<W>(),
            )
        };
        self.as_ref()
            .write(::std::mem::size_of::<W>(), elembuf)
            .map(|_| ())
    }
}

impl<R: FifoEntry, W: FifoEntry> fmt::Debug for Fifo<R, W> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.handle.get_ref().fmt(f)
    }
}

pub struct ReadEntry<R: FifoEntry, W: FifoEntry = R> {
    fifo: Option<Fifo<R, W>>,
}

impl<R: FifoEntry, W: FifoEntry> Future for ReadEntry<R, W> {
    type Item = (Fifo<R, W>, Option<R>);
    type Error = zx::Status;

    fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        let element: Option<R>;
        {
            let fifo = self.fifo
                .as_mut()
                .expect("polled a ReadEntry after completion");
            element = try_ready!(fifo.read(cx));
        }
        let fifo = self.fifo.take().unwrap();
        Ok(Async::Ready((fifo, element)))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::prelude::*;
    use zx::prelude::*;
    use {Executor, TimeoutExt, Timer};

    #[derive(Clone, Debug, PartialEq, Eq)]
    #[repr(C)]
    struct entry {
        a: u32,
        b: u32,
    }
    unsafe impl FifoEntry for entry {}

    #[repr(C)]
    struct wrong_entry {
        a: u16,
    }
    unsafe impl FifoEntry for wrong_entry {}

    #[test]
    fn can_read_write() {
        let mut exec = Executor::new().expect("failed to create executor");
        let e = entry { a: 10, b: 20 };

        let (tx, rx) = zx::Fifo::create(2, ::std::mem::size_of::<entry>() as u32)
            .expect("failed to create zx fifo");
        let (tx, rx) = (
            Fifo::<entry>::from_fifo(tx).expect("failed to create async tx fifo"),
            Fifo::<entry>::from_fifo(rx).expect("failed to create async rx fifo"),
        );

        let receive_future = rx.read_entry().map(|(_rx, entry)| {
            assert_eq!(e, entry.expect("peer closed"));
        });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receive_future
            .on_timeout(300.millis().after_now(), || panic!("timeout"))
            .expect("failed to add timeout to receive future");

        // Sends an entry after the timeout has passed
        let sender = Timer::new(100.millis().after_now())
            .and_then(|()| tx.write(&e))
            .map(|_tx| ());

        let done = receiver.join(sender);
        exec.run_singlethreaded(done)
            .expect("failed to run receive future on executor");
    }

    #[test]
    fn read_wrong_size() {
        let mut exec = Executor::new().expect("failed to create executor");
        let e = entry { a: 10, b: 20 };

        let (tx, rx) = zx::Fifo::create(2, ::std::mem::size_of::<entry>() as u32)
            .expect("failed to create zx fifo");
        let (tx, rx) = (
            Fifo::<entry>::from_fifo(tx).expect("failed to create async tx fifo"),
            Fifo::<wrong_entry>::from_fifo(rx).expect("failed to create async rx fifo"),
        );

        let receive_future = rx.read_entry()
            .map(|(_rx, _entry)| panic!("read should have failed"));

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receive_future
            .on_timeout(300.millis().after_now(), || panic!("timeout"))
            .expect("failed to add timeout to receive future");

        // Sends an entry after the timeout has passed
        let sender = Timer::new(100.millis().after_now())
            .and_then(|()| tx.write(&e))
            .map(|_tx| ());

        let done = receiver.join(sender);
        let res = exec.run_singlethreaded(done);
        match res {
            Err(zx::Status::OUT_OF_RANGE) => (),
            _ => panic!("did not get out-of-range error"),
        }
    }

    #[test]
    fn write_wrong_size() {
        let mut _exec = Executor::new().expect("failed to create executor");
        let e = wrong_entry { a: 10 };

        let (tx, rx) = zx::Fifo::create(2, ::std::mem::size_of::<entry>() as u32)
            .expect("failed to create zx fifo");
        let (tx, _rx) = (
            Fifo::<wrong_entry>::from_fifo(tx).expect("failed to create async tx fifo"),
            Fifo::<entry>::from_fifo(rx).expect("failed to create async rx fifo"),
        );

        match tx.write(&e) {
            Err(zx::Status::OUT_OF_RANGE) => (),
            _ => panic!("did not get out-of-range error"),
        }
    }

    #[test]
    fn write_into_full() {
        let mut exec = Executor::new().expect("failed to create executor");
        let e = entry { a: 10, b: 20 };

        let (tx, rx) = zx::Fifo::create(2, ::std::mem::size_of::<entry>() as u32)
            .expect("failed to create zx fifo");
        let (tx, rx) = (
            Fifo::<entry>::from_fifo(tx).expect("failed to create async tx fifo"),
            Fifo::<entry>::from_fifo(rx).expect("failed to create async rx fifo"),
        );

        tx.write(&e).expect("failed to write first entry");
        tx.write(&e).expect("failed to write second entry");

        match tx.write(&e) {
            Err(zx::Status::SHOULD_WAIT) => (),
            _ => panic!("did not get SHOULD_WAIT error"),
        }

        let receive_future = rx.read_entry().map(|(rx, entry)| {
            assert_eq!(e, entry.expect("peer closed"));
            rx
        });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receive_future
            .on_timeout(300.millis().after_now(), || panic!("timeout"))
            .expect("failed to add timeout to receive future");

        let _rx = exec.run_singlethreaded(receiver)
            .expect("failed to run receive future on executor");

        tx.write(&e).expect("failed to write last entry");
    }
}
