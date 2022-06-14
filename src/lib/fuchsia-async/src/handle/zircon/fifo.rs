// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::rwhandle::RWHandle,
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::ready,
    std::{
        fmt,
        future::Future,
        marker::{PhantomData, Unpin},
        mem::MaybeUninit,
        pin::Pin,
        task::{Context, Poll},
    },
};

/// Marker trait for types that can be read/written with a `Fifo`. Unsafe
/// because not all types may be represented by arbitrary bit patterns.
///
/// # Safety
///
/// `FifoEntry` must only be implemented for types that may be represented by
/// arbitrary bit patterns. Typically `repr(C)` or `repr(transparent)` are
/// minimum requirements for safely implementing this trait.
pub unsafe trait FifoEntry {}

/// A buffer used to write `T` into [`Fifo`] objects.
///
///
/// # Safety
///
/// This trait is unsafe because the compiler cannot verify a correct
/// implementation of `as_bytes_ptr`. See [`FifoWriteBuffer::as_bytes_ptr`] for
/// safety notes.
pub unsafe trait FifoWriteBuffer<T> {
    /// Returns the number of entries to be written.
    fn count(&self) -> usize;
    /// Returns a byte pointer representation to be written into the underlying
    /// FIFO.
    ///
    /// # Safety
    ///
    /// The returned memory *must* be initialized and at least `count() *
    /// sizeof<T>()` bytes long.
    fn as_bytes_ptr(&self) -> *const u8;
}

/// A buffer used to read `T` from [`Fifo`] objects.
///
/// # Safety
///
/// This trait is unsafe because the compiler cannot verify a correct
/// implementation of `as_bytes_ptr_mut`. See
/// [`FifoReadBuffer::as_bytes_ptr_mut`] for safety notes.
pub unsafe trait FifoReadBuffer<T> {
    /// Returns the number of slots available in the buffer to be rceived.
    fn count(&self) -> usize;
    /// Returns a mutable pointer to the buffer contents where FIFO entries must
    /// be written into.
    ///
    /// # Safety
    ///
    /// The returned memory *must* be at least `count() * sizeof<T>()` bytes
    /// long.
    fn as_bytes_ptr_mut(&mut self) -> *mut u8;
}

unsafe impl<T: FifoEntry> FifoWriteBuffer<T> for [T] {
    fn count(&self) -> usize {
        self.len()
    }

    fn as_bytes_ptr(&self) -> *const u8 {
        // SAFETY: Guaranteed by bounds on T.
        self.as_ptr() as _
    }
}

unsafe impl<T: FifoEntry> FifoReadBuffer<T> for [T] {
    fn count(&self) -> usize {
        self.len()
    }

    fn as_bytes_ptr_mut(&mut self) -> *mut u8 {
        // SAFETY: Guaranteed by bounds on T.
        self.as_mut_ptr() as _
    }
}

unsafe impl<T: FifoEntry> FifoWriteBuffer<T> for T {
    fn count(&self) -> usize {
        1
    }

    fn as_bytes_ptr(&self) -> *const u8 {
        // SAFETY: Guaranteed by bounds on T.
        self as *const T as _
    }
}

unsafe impl<T: FifoEntry> FifoReadBuffer<T> for T {
    fn count(&self) -> usize {
        1
    }

    fn as_bytes_ptr_mut(&mut self) -> *mut u8 {
        // SAFETY: Guaranteed by bounds on T.
        self as *mut T as _
    }
}

unsafe impl<T: FifoEntry> FifoReadBuffer<T> for MaybeUninit<T> {
    fn count(&self) -> usize {
        1
    }

    fn as_bytes_ptr_mut(&mut self) -> *mut u8 {
        // SAFETY: Guaranteed by bounds on T if initialized. If uninitialized,
        // contract is that the returned bytes can only be written into.
        self.as_mut_ptr() as _
    }
}

unsafe impl<T: FifoEntry> FifoReadBuffer<T> for [MaybeUninit<T>] {
    fn count(&self) -> usize {
        self.len()
    }

    fn as_bytes_ptr_mut(&mut self) -> *mut u8 {
        // TODO(https://github.com/rust-lang/rust/issues/63569): Use
        // `MaybeUninit::slice_as_mut_ptr` once stable.
        // SAFETY: Guaranteed by bounds on T if initialized. If uninitialized,
        // contract is that the returned bytes can only be written into.
        self.as_mut_ptr() as _
    }
}

/// A helper struct providing an implementation of [`FifoWriteBuffer`]
/// supporting [`WriteEntries`] to be able to write all entries in a buffer
/// instead of providing only partial writes.
struct OffsetWriteBuffer<'a, B: ?Sized, T> {
    buffer: &'a B,
    offset: usize,
    marker: PhantomData<T>,
}

impl<'a, B: ?Sized + FifoWriteBuffer<T>, T: FifoEntry> OffsetWriteBuffer<'a, B, T> {
    fn new(buffer: &'a B) -> Self {
        Self { buffer, offset: 0, marker: PhantomData }
    }

    fn advance(mut self, len: usize) -> Option<Self> {
        self.offset += len;
        if self.offset == self.buffer.count() {
            None
        } else {
            debug_assert!(self.offset < self.buffer.count());
            Some(self)
        }
    }
}

unsafe impl<'a, T: FifoEntry, B: ?Sized + FifoWriteBuffer<T>> FifoWriteBuffer<T>
    for OffsetWriteBuffer<'a, B, T>
{
    fn count(&self) -> usize {
        debug_assert!(self.offset <= self.buffer.count());
        self.buffer.count() - self.offset
    }

    fn as_bytes_ptr(&self) -> *const u8 {
        debug_assert!(self.offset <= self.buffer.count());

        let buffer_bytes = self.buffer.as_bytes_ptr();
        let count = self.offset * std::mem::size_of::<T>();
        // SAFETY: Protected by the debug assertion above and a correct
        // implementation of `FifoWriteBuffer` by `B`.
        unsafe { buffer_bytes.add(count) }
    }
}

/// Identifies that the object may be used to write entries into a FIFO.
pub trait FifoWritable<W: FifoEntry>
where
    Self: Sized,
{
    /// Creates a future that transmits entries to be written.
    ///
    /// The returned future will return after an entry has been received on this
    /// fifo. The future will resolve to the fifo once all elements have been
    /// transmitted.
    fn write_entries<'a, B: ?Sized + FifoWriteBuffer<W>>(
        &'a self,
        entries: &'a B,
    ) -> WriteEntries<'a, Self, B, W> {
        WriteEntries::new(self, entries)
    }

    /// Writes entries to the fifo and registers this `Fifo` as needing a write
    /// on receiving a `zx::Status::SHOULD_WAIT`.
    ///
    /// Returns the number of elements processed.
    fn write<B: ?Sized + FifoWriteBuffer<W>>(
        &self,
        cx: &mut Context<'_>,
        entries: &B,
    ) -> Poll<Result<usize, zx::Status>>;
}

/// Identifies that the object may be used to read entries from a FIFO.
pub trait FifoReadable<R: FifoEntry>
where
    Self: Sized,
{
    /// Creates a future that receives entries into `entries`.
    ///
    /// The returned future will return after the FIFO becomes readable and up
    /// to `entries.len()` has been received. The future will resolve to the
    /// number of elements written into `entries`.
    ///
    fn read_entries<'a, B: ?Sized + FifoReadBuffer<R>>(
        &'a self,
        entries: &'a mut B,
    ) -> ReadEntries<'a, Self, B, R> {
        ReadEntries::new(self, entries)
    }

    /// Creates a future that receives a single entry.
    ///
    /// The returned future will return after the FIFO becomes readable and a
    /// single entry is available.
    fn read_entry<'a>(&'a self) -> ReadOne<'a, Self, R> {
        ReadOne::new(self)
    }

    /// Reads entries from the fifo and registers this `Fifo` as needing a read
    /// on receiving a `zx::Status::SHOULD_WAIT`.
    fn read<B: ?Sized + FifoReadBuffer<R>>(
        &self,
        cx: &mut Context<'_>,
        entries: &mut B,
    ) -> Poll<Result<usize, zx::Status>>;

    /// Reads a single entry and registers this `Fifo` as needing a read on
    /// receiving a `zx::Status::SHOULD_WAIT`.
    fn read_one(&self, cx: &mut Context<'_>) -> Poll<Result<R, zx::Status>> {
        let mut entry = MaybeUninit::uninit();
        self.read(cx, &mut entry).map_ok(|count| {
            debug_assert_eq!(count, 1);
            // SAFETY: The entry was initialized by the fulfilled FIFO read.
            unsafe { entry.assume_init() }
        })
    }
}

/// An I/O object representing a `Fifo`.
pub struct Fifo<R, W = R> {
    handle: RWHandle<zx::Fifo>,
    read_marker: PhantomData<R>,
    write_marker: PhantomData<W>,
}

impl<R, W> AsRef<zx::Fifo> for Fifo<R, W> {
    fn as_ref(&self) -> &zx::Fifo {
        self.handle.get_ref()
    }
}

impl<R, W> AsHandleRef for Fifo<R, W> {
    fn as_handle_ref(&self) -> zx::HandleRef<'_> {
        self.handle.get_ref().as_handle_ref()
    }
}

impl<R, W> From<Fifo<R, W>> for zx::Fifo {
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
    ///
    /// Returns `true` if the CLOSED signal has been received.
    pub fn poll_write(&self, cx: &mut Context<'_>) -> Poll<Result<bool, zx::Status>> {
        self.handle.poll_write(cx)
    }

    /// Writes entries to the fifo and registers this `Fifo` as
    /// needing a write on receiving a `zx::Status::SHOULD_WAIT`.
    ///
    /// Returns the number of elements processed.
    pub fn try_write<B: ?Sized + FifoWriteBuffer<W>>(
        &self,
        cx: &mut Context<'_>,
        entries: &B,
    ) -> Poll<Result<usize, zx::Status>> {
        let clear_closed = ready!(self.poll_write(cx)?);

        let elem_size = std::mem::size_of::<W>();
        let bytes = entries.as_bytes_ptr();
        let count = entries.count();
        let fifo = self.as_ref();
        // SAFETY: Safety relies on the pointer returned by `B` being valid,
        // which itself depends on a correct implementation of `FifoEntry` for
        // `W`.
        let result = unsafe { fifo.write_ptr(elem_size, bytes, count) };
        match result {
            Err(e) => {
                if e == zx::Status::SHOULD_WAIT {
                    self.handle.need_write(cx, clear_closed)?;
                    Poll::Pending
                } else {
                    Poll::Ready(Err(e))
                }
            }
            Ok(count) => Poll::Ready(Ok(count)),
        }
    }

    /// Test whether this fifo is ready to be read or not.
    ///
    /// If the fifo is *not* readable then the current task is scheduled to
    /// get a notification when the fifo does become readable. That is, this
    /// is only suitable for calling in a `Future::poll` method and will
    /// automatically handle ensuring a retry once the fifo is readable again.
    ///
    /// Returns `true` if the CLOSED signal has been received.
    pub fn poll_read(&self, cx: &mut Context<'_>) -> Poll<Result<bool, zx::Status>> {
        self.handle.poll_read(cx)
    }

    /// Reads entries from the fifo into `entries` and registers this `Fifo` as
    /// needing a read on receiving a `zx::Status::SHOULD_WAIT`.
    pub fn try_read<B: ?Sized + FifoReadBuffer<R>>(
        &self,
        cx: &mut Context<'_>,
        entries: &mut B,
    ) -> Poll<Result<usize, zx::Status>> {
        let clear_closed = ready!(self.handle.poll_read(cx)?);

        let elem_size = std::mem::size_of::<R>();
        let bytes = entries.as_bytes_ptr_mut();
        let count = entries.count();
        let fifo = self.as_ref();
        // SAFETY: Safety relies on the pointer returned by `B` being valid,
        // which itself depends on a correct implementation of `FifoEntry` for
        // `R`.
        let result = unsafe { fifo.read_ptr(elem_size, bytes, count) };

        match result {
            Err(e) => {
                if e == zx::Status::SHOULD_WAIT {
                    self.handle.need_read(cx, clear_closed)?;
                    return Poll::Pending;
                }
                return Poll::Ready(Err(e));
            }
            Ok(count) => {
                return Poll::Ready(Ok(count));
            }
        }
    }
}

impl<R: FifoEntry, W: FifoEntry> FifoReadable<R> for Fifo<R, W> {
    fn read<B: ?Sized + FifoReadBuffer<R>>(
        &self,
        cx: &mut Context<'_>,
        entries: &mut B,
    ) -> Poll<Result<usize, zx::Status>> {
        self.try_read(cx, entries)
    }
}

impl<R: FifoEntry, W: FifoEntry> FifoWritable<W> for Fifo<R, W> {
    fn write<B: ?Sized + FifoWriteBuffer<W>>(
        &self,
        cx: &mut Context<'_>,
        entries: &B,
    ) -> Poll<Result<usize, zx::Status>> {
        self.try_write(cx, entries)
    }
}

impl<R, W> fmt::Debug for Fifo<R, W> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.handle.get_ref().fmt(f)
    }
}

/// WriteEntries represents the future of one or more writes.
pub struct WriteEntries<'a, F, B: ?Sized, T> {
    fifo: &'a F,
    entries: Option<OffsetWriteBuffer<'a, B, T>>,
    marker: PhantomData<T>,
}

impl<'a, F, B: ?Sized, T> Unpin for WriteEntries<'a, F, B, T> {}

impl<'a, T: FifoEntry, F: FifoWritable<T>, B: ?Sized + FifoWriteBuffer<T>>
    WriteEntries<'a, F, B, T>
{
    /// Create a new WriteEntries, which borrows the `FifoWritable` type
    /// until the future completes.
    pub fn new(fifo: &'a F, entries: &'a B) -> Self {
        WriteEntries { fifo, entries: Some(OffsetWriteBuffer::new(entries)), marker: PhantomData }
    }
}

impl<'a, T: FifoEntry, F: FifoWritable<T>, B: ?Sized + FifoWriteBuffer<T>> Future
    for WriteEntries<'a, F, B, T>
{
    type Output = Result<(), zx::Status>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        while let Some(entries) = this.entries.as_ref() {
            let advance = ready!(this.fifo.write(cx, entries)?);
            // Unwrap is okay because we know entries is `Some`. This is cleaner
            // than taking from entries and having to put it back on failed
            // poll.
            this.entries = this.entries.take().unwrap().advance(advance);
        }
        Poll::Ready(Ok(()))
    }
}

/// ReadEntries represents the future of a single read with multiple entries.
pub struct ReadEntries<'a, F, B: ?Sized, T> {
    fifo: &'a F,
    entries: &'a mut B,
    marker: PhantomData<T>,
}

impl<'a, F, B: ?Sized, T> Unpin for ReadEntries<'a, F, B, T> {}

impl<'a, T: FifoEntry, F: FifoReadable<T>, B: ?Sized + FifoReadBuffer<T>> ReadEntries<'a, F, B, T> {
    /// Create a new ReadEntries, which borrows the `FifoReadable` type
    /// until the future completes.
    pub fn new(fifo: &'a F, entries: &'a mut B) -> Self {
        ReadEntries { fifo, entries, marker: PhantomData }
    }
}

impl<'a, T: FifoEntry, F: FifoReadable<T>, B: ?Sized + FifoReadBuffer<T>> Future
    for ReadEntries<'a, F, B, T>
{
    type Output = Result<usize, zx::Status>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        this.fifo.read(cx, this.entries)
    }
}

/// ReadOne represents the future of a single read yielding a single entry.
pub struct ReadOne<'a, F, T> {
    fifo: &'a F,
    marker: PhantomData<T>,
}

impl<'a, F, T> Unpin for ReadOne<'a, F, T> {}

impl<'a, T: FifoEntry, F: FifoReadable<T>> ReadOne<'a, F, T> {
    /// Create a new ReadOne, which borrows the `FifoReadable` type
    /// until the future completes.
    pub fn new(fifo: &'a F) -> Self {
        ReadOne { fifo, marker: PhantomData }
    }
}

impl<'a, T: FifoEntry, F: FifoReadable<T>> Future for ReadOne<'a, F, T> {
    type Output = Result<T, zx::Status>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        this.fifo.read_one(cx)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{DurationExt, TestExecutor, TimeoutExt, Timer};
    use fuchsia_zircon::prelude::*;
    use futures::future::try_join;
    use futures::prelude::*;

    #[derive(Copy, Clone, Debug, PartialEq, Eq, Default)]
    #[repr(C)]
    struct entry {
        a: u32,
        b: u32,
    }
    unsafe impl FifoEntry for entry {}

    #[derive(Clone, Debug, PartialEq, Eq, Default)]
    #[repr(C)]
    struct wrong_entry {
        a: u16,
    }
    unsafe impl FifoEntry for wrong_entry {}

    #[test]
    fn can_read_write() {
        let mut exec = TestExecutor::new().expect("failed to create executor");
        let element = entry { a: 10, b: 20 };

        let (tx, rx) =
            zx::Fifo::create(2, ::std::mem::size_of::<entry>()).expect("failed to create zx fifo");
        let (tx, rx) = (
            Fifo::<entry>::from_fifo(tx).expect("failed to create async tx fifo"),
            Fifo::<entry>::from_fifo(rx).expect("failed to create async rx fifo"),
        );

        let mut buffer = entry::default();
        let receive_future = rx.read_entries(&mut buffer).map_ok(|count| {
            assert_eq!(count, 1);
        });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receive_future.on_timeout(300.millis().after_now(), || panic!("timeout"));

        // Sends an entry after the timeout has passed
        let sender = Timer::new(10.millis().after_now()).then(|()| tx.write_entries(&element));

        let done = try_join(receiver, sender);
        exec.run_singlethreaded(done).expect("failed to run receive future on executor");
        assert_eq!(buffer, element);
    }

    #[test]
    fn read_wrong_size() {
        let mut exec = TestExecutor::new().expect("failed to create executor");
        let elements = &[entry { a: 10, b: 20 }][..];

        let (tx, rx) =
            zx::Fifo::create(2, ::std::mem::size_of::<entry>()).expect("failed to create zx fifo");
        let (tx, rx) = (
            Fifo::<entry>::from_fifo(tx).expect("failed to create async tx fifo"),
            Fifo::<wrong_entry>::from_fifo(rx).expect("failed to create async rx fifo"),
        );

        let mut buffer = wrong_entry::default();
        let receive_future = rx
            .read_entries(&mut buffer)
            .map_ok(|count| panic!("read should have failed, got {}", count));

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receive_future.on_timeout(300.millis().after_now(), || panic!("timeout"));

        // Sends an entry after the timeout has passed
        let sender = Timer::new(10.millis().after_now()).then(|()| tx.write_entries(elements));

        let done = try_join(receiver, sender);
        let res = exec.run_singlethreaded(done);
        match res {
            Err(zx::Status::OUT_OF_RANGE) => (),
            _ => panic!("did not get out-of-range error"),
        }
    }

    #[test]
    fn write_wrong_size() {
        let mut exec = TestExecutor::new().expect("failed to create executor");
        let elements = &[wrong_entry { a: 10 }][..];

        let (tx, rx) =
            zx::Fifo::create(2, ::std::mem::size_of::<entry>()).expect("failed to create zx fifo");
        let (tx, _rx) = (
            Fifo::<wrong_entry>::from_fifo(tx).expect("failed to create async tx fifo"),
            Fifo::<entry>::from_fifo(rx).expect("failed to create async rx fifo"),
        );

        let sender = Timer::new(10.millis().after_now()).then(|()| tx.write_entries(elements));

        let res = exec.run_singlethreaded(sender);
        match res {
            Err(zx::Status::OUT_OF_RANGE) => (),
            _ => panic!("did not get out-of-range error"),
        }
    }

    #[test]
    fn write_into_full() {
        use std::sync::atomic::{AtomicUsize, Ordering};

        let mut exec = TestExecutor::new().expect("failed to create executor");
        let elements =
            &[entry { a: 10, b: 20 }, entry { a: 30, b: 40 }, entry { a: 50, b: 60 }][..];

        let (tx, rx) =
            zx::Fifo::create(2, ::std::mem::size_of::<entry>()).expect("failed to create zx fifo");
        let (tx, rx) = (
            Fifo::<entry>::from_fifo(tx).expect("failed to create async tx fifo"),
            Fifo::<entry>::from_fifo(rx).expect("failed to create async rx fifo"),
        );

        // Use `writes_completed` to verify that not all writes
        // are transmitted at once, and the last write is actually blocked.
        let writes_completed = AtomicUsize::new(0);
        let sender = async {
            tx.write_entries(&elements[..2]).await?;
            writes_completed.fetch_add(1, Ordering::SeqCst);
            tx.write_entries(&elements[2..]).await?;
            writes_completed.fetch_add(1, Ordering::SeqCst);
            Ok::<(), zx::Status>(())
        };

        // Wait 10 ms, then read the messages from the fifo.
        let receive_future = async {
            Timer::new(10.millis().after_now()).await;
            let mut buffer = entry::default();
            let count = rx.read_entries(&mut buffer).await?;
            assert_eq!(writes_completed.load(Ordering::SeqCst), 1);
            assert_eq!(count, 1);
            assert_eq!(buffer, elements[0]);
            let count = rx.read_entries(&mut buffer).await?;
            // At this point, the last write may or may not have
            // been written.
            assert_eq!(count, 1);
            assert_eq!(buffer, elements[1]);
            let count = rx.read_entries(&mut buffer).await?;
            assert_eq!(writes_completed.load(Ordering::SeqCst), 2);
            assert_eq!(count, 1);
            assert_eq!(buffer, elements[2]);
            Ok::<(), zx::Status>(())
        };

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receive_future.on_timeout(300.millis().after_now(), || panic!("timeout"));

        let done = try_join(receiver, sender);

        exec.run_singlethreaded(done).expect("failed to run receive future on executor");
    }

    #[test]
    fn write_more_than_full() {
        let mut exec = TestExecutor::new().expect("failed to create executor");
        let elements =
            &[entry { a: 10, b: 20 }, entry { a: 30, b: 40 }, entry { a: 50, b: 60 }][..];

        let (tx, rx) =
            zx::Fifo::create(2, ::std::mem::size_of::<entry>()).expect("failed to create zx fifo");
        let (tx, rx) = (
            Fifo::<entry>::from_fifo(tx).expect("failed to create async tx fifo"),
            Fifo::<entry>::from_fifo(rx).expect("failed to create async rx fifo"),
        );

        let sender = tx.write_entries(elements);

        // Wait 10 ms, then read the messages from the fifo.
        let receive_future = async {
            Timer::new(10.millis().after_now()).await;
            for e in elements {
                let mut buffer = [entry::default(); 1];
                let count = rx.read_entries(&mut buffer[..]).await?;
                assert_eq!(count, 1);
                assert_eq!(&buffer[0], e);
            }
            Ok::<(), zx::Status>(())
        };

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receive_future.on_timeout(300.millis().after_now(), || panic!("timeout"));

        let done = try_join(receiver, sender);

        exec.run_singlethreaded(done).expect("failed to run receive future on executor");
    }

    #[test]
    fn read_multiple() {
        let mut exec = TestExecutor::new().expect("failed to create executor");
        let elements =
            &[entry { a: 10, b: 20 }, entry { a: 30, b: 40 }, entry { a: 50, b: 60 }][..];
        let (tx, rx) = zx::Fifo::create(elements.len(), ::std::mem::size_of::<entry>())
            .expect("failed to create zx fifo");
        let (tx, rx) = (
            Fifo::<entry>::from_fifo(tx).expect("failed to create async tx fifo"),
            Fifo::<entry>::from_fifo(rx).expect("failed to create async rx fifo"),
        );

        let write_fut = async {
            tx.write_entries(&elements[..]).await.expect("failed write entries");
        };
        let read_fut = async {
            // Use a larger buffer to show partial reads.
            let mut buffer = [entry::default(); 5];
            let count = rx.read_entries(&mut buffer[..]).await.expect("failed to read entries");
            assert_eq!(count, elements.len());
            assert_eq!(&buffer[..count], &elements[..]);
        };
        let ((), ()) = exec.run_singlethreaded(futures::future::join(write_fut, read_fut));
    }

    #[test]
    fn read_one() {
        let mut exec = TestExecutor::new().expect("failed to create executor");
        let elements =
            &[entry { a: 10, b: 20 }, entry { a: 30, b: 40 }, entry { a: 50, b: 60 }][..];
        let (tx, rx) = zx::Fifo::create(elements.len(), ::std::mem::size_of::<entry>())
            .expect("failed to create zx fifo");
        let (tx, rx) = (
            Fifo::<entry>::from_fifo(tx).expect("failed to create async tx fifo"),
            Fifo::<entry>::from_fifo(rx).expect("failed to create async rx fifo"),
        );

        let write_fut = async {
            tx.write_entries(&elements[..]).await.expect("failed write entries");
        };
        let read_fut = async {
            for e in elements {
                let received = rx.read_entry().await.expect("failed to read entry");
                assert_eq!(&received, e);
            }
        };
        let ((), ()) = exec.run_singlethreaded(futures::future::join(write_fut, read_fut));
    }

    #[test]
    fn maybe_uninit_single() {
        let mut exec = TestExecutor::new().expect("failed to create executor");
        let element = entry { a: 10, b: 20 };
        let (tx, rx) =
            zx::Fifo::create(1, ::std::mem::size_of::<entry>()).expect("failed to create zx fifo");
        let (tx, rx) = (
            Fifo::<entry>::from_fifo(tx).expect("failed to create async tx fifo"),
            Fifo::<entry>::from_fifo(rx).expect("failed to create async rx fifo"),
        );

        let write_fut = async {
            tx.write_entries(&element).await.expect("failed write entries");
        };
        let read_fut = async {
            let mut buffer = MaybeUninit::<entry>::uninit();
            let count = rx.read_entries(&mut buffer).await.expect("failed to read entries");
            assert_eq!(count, 1);
            // SAFETY: We just read a new entry into the buffer.
            let read = unsafe { buffer.assume_init() };
            assert_eq!(read, element);
        };
        let ((), ()) = exec.run_singlethreaded(futures::future::join(write_fut, read_fut));
    }

    #[test]
    fn maybe_uninit_slice() {
        let mut exec = TestExecutor::new().expect("failed to create executor");
        let elements =
            &[entry { a: 10, b: 20 }, entry { a: 30, b: 40 }, entry { a: 50, b: 60 }][..];
        let (tx, rx) = zx::Fifo::create(elements.len(), ::std::mem::size_of::<entry>())
            .expect("failed to create zx fifo");
        let (tx, rx) = (
            Fifo::<entry>::from_fifo(tx).expect("failed to create async tx fifo"),
            Fifo::<entry>::from_fifo(rx).expect("failed to create async rx fifo"),
        );

        let write_fut = async {
            tx.write_entries(&elements[..]).await.expect("failed write entries");
        };
        let read_fut = async {
            // Use a larger buffer to show partial reads.
            let mut buffer = [MaybeUninit::<entry>::uninit(); 15];
            let count = rx.read_entries(&mut buffer[..]).await.expect("failed to read entries");
            assert_eq!(count, elements.len());
            let read = &mut buffer[..count];
            for (i, v) in read.iter_mut().enumerate() {
                // SAFETY: This is the read region of the buffer, initialized by
                // reading from the FIFO.
                let read = unsafe { v.assume_init_ref() };
                assert_eq!(read, &elements[i]);
                // SAFETY: The buffer was partially initialized by reading from
                // the FIFO, the correct thing to do here is to manually drop
                // the elements that were initialized.
                unsafe {
                    v.assume_init_drop();
                }
            }
        };
        let ((), ()) = exec.run_singlethreaded(futures::future::join(write_fut, read_fut));
    }
}
