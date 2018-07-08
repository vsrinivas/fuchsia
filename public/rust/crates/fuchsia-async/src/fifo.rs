// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;
use std::marker::{PhantomData, Sized};
use std::vec::Vec;

use futures::{task, Async, Future, Poll};
use zx::{self, AsHandleRef};

use RWHandle;

/// Marker trait for types that can be read/written with a `Fifo`.
/// Unsafe because not all types may be represented by arbitrary bit patterns.
pub unsafe trait FifoEntry {}

/// Identifies that the object may be used to write entries into a FIFO.
pub trait FifoWritable<W: FifoEntry> where Self: Sized {
    /// Creates a future that transmits entries to be written.
    ///
    /// The returned future will return after an entry has been received on
    /// this fifo. The future will resolve to the fifo once all elements
    /// have been transmitted.
    ///
    /// An error during writing will cause the fifo to get
    /// destroyed and the status will be returned.
    fn write_entries(self, entries: Vec<W>) -> WriteEntry<Self, W> {
        WriteEntry::new(self, entries)
    }

    /// Writes entries to the fifo and registers this `Fifo` as
    /// needing a write on receiving a `zx::Status::SHOULD_WAIT`.
    ///
    /// Returns the number of elements processed.
    fn write(&self, entries: &[W], cx: &mut task::Context) -> Poll<usize, zx::Status>;
}

/// Identifies that the object may be used to read entries from a FIFO.
pub trait FifoReadable<R: FifoEntry> where Self: Sized {
    /// Creates a future that receives an entry to be written to the element
    /// provided.
    ///
    /// The returned future will return after an entry has been received on
    /// this fifo. The future will resolve to the fifo and the entry.
    ///
    /// An error during reading will cause the fifo and entry to get
    /// destroyed and the status will be returned.
    fn read_entry(self) -> ReadEntry<Self, R> {
        ReadEntry::new(self)
    }

    /// Reads an entry from the fifo and registers this `Fifo` as
    /// needing a read on receiving a `zx::Status::SHOULD_WAIT`.
    fn read(&self, cx: &mut task::Context) -> Poll<Option<R>, zx::Status>;
}

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

    /// Test whether this fifo is ready to be written or not.
    ///
    /// If the fifo is *not* writable then the current task is scheduled to
    /// get a notification when the fifo does become writable. That is, this
    /// is only suitable for calling in a `Future::poll` method and will
    /// automatically handle ensuring a retry once the fifo is writable again.
    pub fn poll_write(&self, cx: &mut task::Context) -> Poll<(), zx::Status> {
        self.handle.poll_write(cx)
    }

    /// Writes entries to the fifo and registers this `Fifo` as
    /// needing a write on receiving a `zx::Status::SHOULD_WAIT`.
    ///
    /// Returns the number of elements processed.
    pub fn try_write(&self, entries: &[W], cx: &mut task::Context) -> Poll<usize, zx::Status> {
        try_ready!(self.poll_write(cx));
        let elem_size = ::std::mem::size_of::<W>();
        let elembuf = unsafe {
            ::std::slice::from_raw_parts(entries.as_ptr() as *const u8, elem_size * entries.len())
        };
        match self.as_ref().write(elem_size, elembuf) {
            Err(e) => {
                if e == zx::Status::SHOULD_WAIT {
                    self.handle.need_write(cx)?;
                    return Ok(Async::Pending);
                }
                return Err(e);
            }
            Ok(count) => {
                return Ok(Async::Ready(count));
            }
        }
    }

    /// Test whether this fifo is ready to be read or not.
    ///
    /// If the fifo is *not* readable then the current task is scheduled to
    /// get a notification when the fifo does become readable. That is, this
    /// is only suitable for calling in a `Future::poll` method and will
    /// automatically handle ensuring a retry once the fifo is readable again.
    pub fn poll_read(&self, cx: &mut task::Context) -> Poll<(), zx::Status> {
        self.handle.poll_read(cx)
    }

    /// Reads an entry from the fifo and registers this `Fifo` as
    /// needing a read on receiving a `zx::Status::SHOULD_WAIT`.
    pub fn try_read(&self, cx: &mut task::Context) -> Poll<Option<R>, zx::Status> {
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
}

impl<R: FifoEntry, W: FifoEntry> FifoReadable<R> for Fifo<R, W> {
    fn read(&self, cx: &mut task::Context) -> Poll<Option<R>, zx::Status> {
        self.try_read(cx)
    }
}

impl<R: FifoEntry, W: FifoEntry> FifoWritable<W> for Fifo<R, W> {
    fn write(&self, entries: &[W], cx: &mut task::Context) -> Poll<usize, zx::Status> {
        self.try_write(entries, cx)
    }
}

impl<R: FifoEntry, W: FifoEntry> fmt::Debug for Fifo<R, W> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.handle.get_ref().fmt(f)
    }
}

/// WriteEntry represents the future of one or more writes.
pub struct WriteEntry<F: FifoWritable<W>, W: FifoEntry> {
    fifo: Option<F>,
    vector: Vec<W>,
    index: usize,
}

impl<F: FifoWritable<W>, W: FifoEntry> WriteEntry<F, W> {
    /// Create a new WriteEntry, which owns the `FifoWritable` type
    /// until the future completes.
    pub fn new(fifo: F, vector: Vec<W>) -> WriteEntry<F, W> {
        WriteEntry {
            fifo: Some(fifo),
            vector,
            index: 0,
        }
    }
}

impl<F: FifoWritable<W>, W: FifoEntry> Future for WriteEntry<F, W> {
    type Item = (F, Option<()>);
    type Error = zx::Status;

    fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        {
            let fifo = self
                .fifo
                .as_mut()
                .expect("polled a WriteEntry after completion");
            while self.index < self.vector.len() {
                self.index += try_ready!(fifo.write(&self.vector[self.index..], cx));
            }
        }
        let fifo = self.fifo.take().unwrap();
        Ok(Async::Ready((fifo, Some(()))))
    }
}

/// ReadEntry represents the future of a single read.
pub struct ReadEntry<F: FifoReadable<R>, R: FifoEntry> {
    fifo: Option<F>,
    read_marker: PhantomData<R>,
}

impl<F: FifoReadable<R>, R: FifoEntry> ReadEntry<F, R> {
    /// Create a new ReadEntry, which owns the `FifoReadable` type
    /// until the future completes.
    pub fn new(fifo: F) -> ReadEntry<F, R> {
        ReadEntry {
            fifo: Some(fifo),
            read_marker: PhantomData,
        }
    }
}

impl<F: FifoReadable<R>, R: FifoEntry> Future for ReadEntry<F, R> {
    type Item = (F, Option<R>);
    type Error = zx::Status;

    fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        let element: Option<R>;
        {
            let fifo = self
                .fifo
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

    #[derive(Clone, Debug, PartialEq, Eq)]
    #[repr(C)]
    struct wrong_entry {
        a: u16,
    }
    unsafe impl FifoEntry for wrong_entry {}

    #[test]
    fn can_read_write() {
        let mut exec = Executor::new().expect("failed to create executor");
        let elements: &[entry; 1] = &[entry { a: 10, b: 20 }];

        let (tx, rx) = zx::Fifo::create(2, ::std::mem::size_of::<entry>() as u32)
            .expect("failed to create zx fifo");
        let (tx, rx) = (
            Fifo::<entry>::from_fifo(tx).expect("failed to create async tx fifo"),
            Fifo::<entry>::from_fifo(rx).expect("failed to create async rx fifo"),
        );

        let receive_future = rx.read_entry().map(|(_rx, entry)| {
            assert_eq!(elements[0], entry.expect("peer closed"));
        });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receive_future
            .on_timeout(300.millis().after_now(), || panic!("timeout"))
            .expect("failed to add timeout to receive future");

        // Sends an entry after the timeout has passed
        let sender = Timer::new(10.millis().after_now()).and_then(|()| tx.write_entries(elements.to_vec()));

        let done = receiver.join(sender);
        exec.run_singlethreaded(done)
            .expect("failed to run receive future on executor");
    }

    #[test]
    fn read_wrong_size() {
        let mut exec = Executor::new().expect("failed to create executor");
        let elements: &[entry; 1] = &[entry { a: 10, b: 20 }];

        let (tx, rx) = zx::Fifo::create(2, ::std::mem::size_of::<entry>() as u32)
            .expect("failed to create zx fifo");
        let (tx, rx) = (
            Fifo::<entry>::from_fifo(tx).expect("failed to create async tx fifo"),
            Fifo::<wrong_entry>::from_fifo(rx).expect("failed to create async rx fifo"),
        );

        let receive_future = rx
            .read_entry()
            .map(|(_rx, _entry)| panic!("read should have failed"));

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receive_future
            .on_timeout(300.millis().after_now(), || panic!("timeout"))
            .expect("failed to add timeout to receive future");

        // Sends an entry after the timeout has passed
        let sender = Timer::new(10.millis().after_now()).and_then(|()| tx.write_entries(elements.to_vec()));

        let done = receiver.join(sender);
        let res = exec.run_singlethreaded(done);
        match res {
            Err(zx::Status::OUT_OF_RANGE) => (),
            _ => panic!("did not get out-of-range error"),
        }
    }

    #[test]
    fn write_wrong_size() {
        let mut exec = Executor::new().expect("failed to create executor");
        let elements: &[wrong_entry; 1] = &[wrong_entry { a: 10 }];

        let (tx, rx) = zx::Fifo::create(2, ::std::mem::size_of::<entry>() as u32)
            .expect("failed to create zx fifo");
        let (tx, _rx) = (
            Fifo::<wrong_entry>::from_fifo(tx).expect("failed to create async tx fifo"),
            Fifo::<entry>::from_fifo(rx).expect("failed to create async rx fifo"),
        );

        let sender = Timer::new(10.millis().after_now())
            .and_then(|()| tx.write_entries(elements.to_vec()));

        let res = exec.run_singlethreaded(sender);
        match res {
            Err(zx::Status::OUT_OF_RANGE) => (),
            _ => panic!("did not get out-of-range error"),
        }
    }

    #[test]
    fn write_into_full() {
        use std::sync::atomic::{AtomicUsize, Ordering};

        let mut exec = Executor::new().expect("failed to create executor");
        let elements: &[entry; 3] = &[
            entry { a: 10, b: 20 },
            entry { a: 30, b: 40 },
            entry { a: 50, b: 60 },
        ];

        let (tx, rx) = zx::Fifo::create(2, ::std::mem::size_of::<entry>() as u32)
            .expect("failed to create zx fifo");
        let (tx, rx) = (
            Fifo::<entry>::from_fifo(tx).expect("failed to create async tx fifo"),
            Fifo::<entry>::from_fifo(rx).expect("failed to create async rx fifo"),
        );

        // Use `writes_completed` to verify that not all writes
        // are transmitted at once, and the last write is actually blocked.
        let writes_completed = AtomicUsize::new(0);
        let sender = tx
            .write_entries(elements[..2].to_vec())
            .and_then(|(tx, _)| {
                writes_completed.fetch_add(1, Ordering::SeqCst);
                tx.write_entries(elements[2..].to_vec())
            })
            .map(|(tx, _)| {
                writes_completed.fetch_add(1, Ordering::SeqCst);
                tx
            });

        // Wait 10 ms, then read the messages from the fifo.
        let receive_future = Timer::new(10.millis().after_now())
            .and_then(|()| rx.read_entry())
            .map(|(rx, entry)| {
                assert_eq!(writes_completed.load(Ordering::SeqCst), 1);
                assert_eq!(elements[0], entry.expect("peer closed"));
                rx
            })
            .and_then(|rx| rx.read_entry())
            .map(|(rx, entry)| {
                // At this point, the last write may or may not have
                // been written.
                assert_eq!(elements[1], entry.expect("peer closed"));
                rx
            })
            .and_then(|rx| rx.read_entry())
            .map(|(rx, entry)| {
                assert_eq!(writes_completed.load(Ordering::SeqCst), 2);
                assert_eq!(elements[2], entry.expect("peer closed"));
                rx
            });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receive_future
            .on_timeout(300.millis().after_now(), || panic!("timeout"))
            .expect("failed to add timeout to receive future");

        let done = receiver.join(sender);

        exec.run_singlethreaded(done)
            .expect("failed to run receive future on executor");
    }

    #[test]
    fn write_more_than_full() {
        let mut exec = Executor::new().expect("failed to create executor");
        let elements: &[entry; 3] = &[
            entry { a: 10, b: 20 },
            entry { a: 30, b: 40 },
            entry { a: 50, b: 60 },
        ];

        let (tx, rx) = zx::Fifo::create(2, ::std::mem::size_of::<entry>() as u32)
            .expect("failed to create zx fifo");
        let (tx, rx) = (
            Fifo::<entry>::from_fifo(tx).expect("failed to create async tx fifo"),
            Fifo::<entry>::from_fifo(rx).expect("failed to create async rx fifo"),
        );

        let sender = tx.write_entries(elements.to_vec());

        // Wait 10 ms, then read the messages from the fifo.
        let receive_future = Timer::new(10.millis().after_now())
            .and_then(|()| rx.read_entry())
            .map(|(rx, entry)| {
                assert_eq!(elements[0], entry.expect("peer closed"));
                rx
            })
            .and_then(|rx| rx.read_entry())
            .map(|(rx, entry)| {
                assert_eq!(elements[1], entry.expect("peer closed"));
                rx
            })
            .and_then(|rx| rx.read_entry())
            .map(|(rx, entry)| {
                assert_eq!(elements[2], entry.expect("peer closed"));
                rx
            });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receive_future
            .on_timeout(300.millis().after_now(), || panic!("timeout"))
            .expect("failed to add timeout to receive future");

        let done = receiver.join(sender);

        exec.run_singlethreaded(done)
            .expect("failed to run receive future on executor");
    }
}
