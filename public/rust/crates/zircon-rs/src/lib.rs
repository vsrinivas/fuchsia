// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Magenta kernel
//! [syscalls](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls.md).

extern crate core;
extern crate magenta_sys;
extern crate conv;

use std::marker::PhantomData;

use conv::{ValueInto, ValueFrom, UnwrapOrSaturate};

mod channel;
mod event;
mod eventpair;
mod fifo;
mod job;
mod port;
mod process;
mod socket;
mod timer;
mod thread;
mod vmo;

pub use channel::{Channel, ChannelOpts, MessageBuf};
pub use event::{Event, EventOpts};
pub use eventpair::{EventPair, EventPairOpts};
pub use fifo::{Fifo, FifoOpts};
pub use job::Job;
pub use port::{Packet, PacketContents, Port, PortOpts, SignalPacket, UserPacket, WaitAsyncOpts};
pub use process::Process;
pub use socket::{Socket, SocketOpts, SocketReadOpts, SocketWriteOpts};
pub use timer::{Timer, TimerOpts};
pub use thread::Thread;
pub use vmo::{Vmo, VmoCloneOpts, VmoOp, VmoOpts};

use magenta_sys as sys;

type Duration = sys::mx_duration_t;
type Time = sys::mx_time_t;
pub use magenta_sys::MX_TIME_INFINITE;

// A placeholder value used for handles that have been taken from the message buf.
// We rely on the kernel never to produce any actual handles with this value.
const INVALID_HANDLE: sys::mx_handle_t = 0;

/// A status code returned from the Magenta kernel.
///
/// See
/// [errors.md](https://fuchsia.googlesource.com/magenta/+/master/docs/errors.md)
/// in the Magenta documentation for more information about the meaning of these
/// codes.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
#[repr(i32)]
// Auto-generated using tools/gen_status.py
pub enum Status {
    NoError = 0,
    ErrInternal = -1,
    ErrNotSupported = -2,
    ErrNoResources = -3,
    ErrNoMemory = -4,
    ErrCallFailed = -5,
    ErrInterruptedRetry = -6,
    ErrInvalidArgs = -10,
    ErrBadHandle = -11,
    ErrWrongType = -12,
    ErrBadSyscall = -13,
    ErrOutOfRange = -14,
    ErrBufferTooSmall = -15,
    ErrBadState = -20,
    ErrTimedOut = -21,
    ErrShouldWait = -22,
    ErrCanceled = -23,
    ErrPeerClosed = -24,
    ErrNotFound = -25,
    ErrAlreadyExists = -26,
    ErrAlreadyBound = -27,
    ErrUnavailable = -28,
    ErrAccessDenied = -30,
    ErrIo = -40,
    ErrIoRefused = -41,
    ErrIoDataIntegrity = -42,
    ErrIoDataLoss = -43,
    ErrBadPath = -50,
    ErrNotDir = -51,
    ErrNotFile = -52,
    ErrFileBig = -53,
    ErrNoSpace = -54,
    ErrStop = -60,
    ErrNext = -61,

    /// Any mx_status_t not in the set above will map to the following:
    UnknownOther = -32768,
}

impl Status {
    pub fn from_raw(raw: sys::mx_status_t) -> Self {
        match raw {
            // Auto-generated using tools/gen_status.py
            sys::MX_OK => Status::NoError,
            sys::MX_ERR_INTERNAL => Status::ErrInternal,
            sys::MX_ERR_NOT_SUPPORTED => Status::ErrNotSupported,
            sys::MX_ERR_NO_RESOURCES => Status::ErrNoResources,
            sys::MX_ERR_NO_MEMORY => Status::ErrNoMemory,
            sys::MX_ERR_CALL_FAILED => Status::ErrCallFailed,
            sys::MX_ERR_INTERRUPTED_RETRY => Status::ErrInterruptedRetry,
            sys::MX_ERR_INVALID_ARGS => Status::ErrInvalidArgs,
            sys::MX_ERR_BAD_HANDLE => Status::ErrBadHandle,
            sys::MX_ERR_WRONG_TYPE => Status::ErrWrongType,
            sys::MX_ERR_BAD_SYSCALL => Status::ErrBadSyscall,
            sys::MX_ERR_OUT_OF_RANGE => Status::ErrOutOfRange,
            sys::MX_ERR_BUFFER_TOO_SMALL => Status::ErrBufferTooSmall,
            sys::MX_ERR_BAD_STATE => Status::ErrBadState,
            sys::MX_ERR_TIMED_OUT => Status::ErrTimedOut,
            sys::MX_ERR_SHOULD_WAIT => Status::ErrShouldWait,
            sys::MX_ERR_CANCELED => Status::ErrCanceled,
            sys::MX_ERR_PEER_CLOSED => Status::ErrPeerClosed,
            sys::MX_ERR_NOT_FOUND => Status::ErrNotFound,
            sys::MX_ERR_ALREADY_EXISTS => Status::ErrAlreadyExists,
            sys::MX_ERR_ALREADY_BOUND => Status::ErrAlreadyBound,
            sys::MX_ERR_UNAVAILABLE => Status::ErrUnavailable,
            sys::MX_ERR_ACCESS_DENIED => Status::ErrAccessDenied,
            sys::MX_ERR_IO => Status::ErrIo,
            sys::MX_ERR_IO_REFUSED => Status::ErrIoRefused,
            sys::MX_ERR_IO_DATA_INTEGRITY => Status::ErrIoDataIntegrity,
            sys::MX_ERR_IO_DATA_LOSS => Status::ErrIoDataLoss,
            sys::MX_ERR_BAD_PATH => Status::ErrBadPath,
            sys::MX_ERR_NOT_DIR => Status::ErrNotDir,
            sys::MX_ERR_NOT_FILE => Status::ErrNotFile,
            sys::MX_ERR_FILE_BIG => Status::ErrFileBig,
            sys::MX_ERR_NO_SPACE => Status::ErrNoSpace,
            sys::MX_ERR_STOP => Status::ErrStop,
            sys::MX_ERR_NEXT => Status::ErrNext,
            _ => Status::UnknownOther,
        }
    }

    // Note: no to_raw, even though it's easy to implement, partly because
    // handling of UnknownOther would be tricky.
}

/// Rights associated with a handle.
///
/// See [rights.md](https://fuchsia.googlesource.com/magenta/+/master/docs/rights.md)
/// for more information.
pub type Rights = sys::mx_rights_t;

pub use magenta_sys::{
    MX_RIGHT_NONE,
    MX_RIGHT_DUPLICATE,
    MX_RIGHT_TRANSFER,
    MX_RIGHT_READ,
    MX_RIGHT_WRITE,
    MX_RIGHT_EXECUTE,
    MX_RIGHT_MAP,
    MX_RIGHT_GET_PROPERTY,
    MX_RIGHT_SET_PROPERTY,
    MX_RIGHT_DEBUG,
    MX_RIGHT_SAME_RIGHTS,
};

/// Signals that can be waited upon.
///
/// See
/// [Objects and signals](https://fuchsia.googlesource.com/magenta/+/master/docs/concepts.md#Objects-and-Signals)
/// in the Magenta kernel documentation. Note: the names of signals are still in flux.
pub type Signals = sys::mx_signals_t;

pub use magenta_sys::{
        MX_SIGNAL_NONE,

        MX_SIGNAL_HANDLE_CLOSED,
        MX_SIGNAL_LAST_HANDLE,

        MX_USER_SIGNAL_0,
        MX_USER_SIGNAL_1,
        MX_USER_SIGNAL_2,
        MX_USER_SIGNAL_3,
        MX_USER_SIGNAL_4,
        MX_USER_SIGNAL_5,
        MX_USER_SIGNAL_6,
        MX_USER_SIGNAL_7,

        // Event
        MX_EVENT_SIGNALED,

        // EventPair
        MX_EPAIR_SIGNALED,
        MX_EPAIR_CLOSED,

        // Task signals (process, thread, job)
        MX_TASK_TERMINATED,

        // Channel
        MX_CHANNEL_READABLE,
        MX_CHANNEL_WRITABLE,
        MX_CHANNEL_PEER_CLOSED,

        // Socket
        MX_SOCKET_READABLE,
        MX_SOCKET_WRITABLE,
        MX_SOCKET_PEER_CLOSED,

        // Timer
        MX_TIMER_SIGNALED,
};

/// A "wait item" containing a handle reference and information about what signals
/// to wait on, and, on return from `object_wait_many`, which are pending.
#[repr(C)]
#[derive(Debug)]
pub struct WaitItem<'a> {
    /// The handle to wait on.
    pub handle: HandleRef<'a>,
    /// A set of signals to wait for.
    pub waitfor: Signals,
    /// The set of signals pending, on return of `object_wait_many`.
    pub pending: Signals,
}


/// An identifier to select a particular clock. See
/// [mx_time_get](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/time_get.md)
/// for more information about the possible values.
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum ClockId {
    /// The number of nanoseconds since the system was powered on. Corresponds to
    /// `MX_CLOCK_MONOTONIC`.
    Monotonic = 0,
    /// The number of wall clock nanoseconds since the Unix epoch (midnight on January 1 1970) in
    /// UTC. Corresponds to MX_CLOCK_UTC.
    UTC = 1,
    /// The number of nanoseconds the current thread has been running for. Corresponds to
    /// MX_CLOCK_THREAD.
    Thread = 2,
}

/// Get the current time, from the specific clock id.
///
/// Wraps the
/// [mx_time_get](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/time_get.md)
/// syscall.
pub fn time_get(clock_id: ClockId) -> Time {
    unsafe { sys::mx_time_get(clock_id as u32) }
}

/// Read the number of high-precision timer ticks since boot. These ticks may be processor cycles,
/// high speed timer, profiling timer, etc. They are not guaranteed to continue advancing when the
/// system is asleep.
///
/// Wraps the
/// [mx_ticks_get](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/ticks_get.md)
/// syscall.
pub fn ticks_get() -> u64 {
    unsafe { sys::mx_ticks_get() }
}

/// Compute a deadline for the time in the future that is the given `Duration` away.
///
/// Wraps the
/// [mx_deadline_after](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/deadline_after.md)
/// syscall.
pub fn deadline_after(nanos: Duration) -> Time {
    unsafe { sys::mx_deadline_after(nanos) }
}

/// Sleep until the given deadline.
///
/// Wraps the
/// [mx_nanosleep](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/nanosleep.md)
/// syscall.
pub fn nanosleep(deadline: Time) {
    unsafe { sys::mx_nanosleep(deadline); }
}

/// Return the number of high-precision timer ticks in a second.
///
/// Wraps the
/// [mx_ticks_per_second](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/ticks_per_second.md)
/// syscall.
pub fn ticks_per_second() -> u64 {
    unsafe { sys::mx_ticks_per_second() }
}

pub use magenta_sys::{
    MX_CPRNG_DRAW_MAX_LEN,
    MX_CPRNG_ADD_ENTROPY_MAX_LEN,
};

/// Draw random bytes from the kernel's CPRNG to fill the given buffer. Returns the actual number of
/// bytes drawn, which may sometimes be less than the size of the buffer provided.
///
/// The buffer must have length less than `MX_CPRNG_DRAW_MAX_LEN`.
///
/// Wraps the
/// [mx_cprng_draw](https://fuchsia.googlesource.com/magenta/+/HEAD/docs/syscalls/cprng_draw.md)
/// syscall.
pub fn cprng_draw(buffer: &mut [u8]) -> Result<usize, Status> {
    let mut actual = 0;
    let status = unsafe { sys::mx_cprng_draw(buffer.as_mut_ptr(), buffer.len(), &mut actual) };
    into_result(status, || actual)
}

/// Mix the given entropy into the kernel CPRNG.
///
/// The buffer must have length less than `MX_CPRNG_ADD_ENTROPY_MAX_LEN`.
///
/// Wraps the
/// [mx_cprng_add_entropy](https://fuchsia.googlesource.com/magenta/+/HEAD/docs/syscalls/cprng_add_entropy.md)
/// syscall.
pub fn cprng_add_entropy(buffer: &[u8]) -> Result<(), Status> {
    let status = unsafe { sys::mx_cprng_add_entropy(buffer.as_ptr(), buffer.len()) };
    into_result(status, || ())
}

fn into_result<T, F>(status: sys::mx_status_t, f: F) -> Result<T, Status>
    where F: FnOnce() -> T {
    // All non-negative values are assumed successful. Note: calls that don't try
    // to multiplex success values into status return could be more strict here.
    if status >= 0 {
        Ok(f())
    } else {
        Err(Status::from_raw(status))
    }
}

// Handles

/// A borrowed reference to a `Handle`.
///
/// Mostly useful as part of a `WaitItem`.
#[derive(Debug)]
pub struct HandleRef<'a> {
    handle: sys::mx_handle_t,
    phantom: PhantomData<&'a sys::mx_handle_t>,
}

impl<'a> HandleRef<'a> {
    fn duplicate(&self, rights: Rights) -> Result<Handle, Status> {
        let handle = self.handle;
        let mut out = 0;
        let status = unsafe { sys::mx_handle_duplicate(handle, rights, &mut out) };
        into_result(status, || Handle(out))
    }

    fn replace(self, rights: Rights) -> Result<Handle, Status> {
        let handle = self.handle;
        let mut out = 0;
        let status = unsafe { sys::mx_handle_replace(handle, rights, &mut out) };
        into_result(status, || Handle(out))
    }

    fn signal(&self, clear_mask: Signals, set_mask: Signals) -> Result<(), Status> {
        let handle = self.handle;
        let status = unsafe { sys::mx_object_signal(handle, clear_mask.bits(), set_mask.bits()) };
        into_result(status, || ())
    }

    fn wait(&self, signals: Signals, deadline: Time) -> Result<Signals, Status> {
        let handle = self.handle;
        let mut pending = sys::mx_signals_t::empty();
        let status = unsafe {
            sys::mx_object_wait_one(handle, signals, deadline, &mut pending)
        };
        into_result(status, || pending)
    }

    fn wait_async(&self, port: &Port, key: u64, signals: Signals, options: WaitAsyncOpts)
        -> Result<(), Status>
    {
        let handle = self.handle;
        let status = unsafe {
            sys::mx_object_wait_async(handle, port.raw_handle(), key, signals, options as u32)
        };
        into_result(status, || ())
    }
}

/// A trait implemented by all handle objects.
///
/// Note: it is reasonable for user-defined objects wrapping a handle to implement
/// this trait. For example, a specific interface in some protocol might be
/// represented as a newtype of `Channel`, and implement the `get_ref` and
/// `from_handle` methods to facilitate conversion from and to the interface.
pub trait HandleBase: Sized {
    /// Get a reference to the handle. One important use of such a reference is
    /// for `object_wait_many`.
    fn get_ref(&self) -> HandleRef;

    /// Interpret the reference as a raw handle (an integer type). Two distinct
    /// handles will have different raw values (so it can perhaps be used as a
    /// key in a data structure).
    fn raw_handle(&self) -> sys::mx_handle_t {
        self.get_ref().handle
    }

    /// Duplicate a handle, possibly reducing the rights available. Wraps the
    /// [mx_handle_duplicate](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/handle_duplicate.md)
    /// syscall.
    fn duplicate(&self, rights: Rights) -> Result<Self, Status> {
        self.get_ref().duplicate(rights).map(|handle|
            Self::from_handle(handle))
    }

    /// Create a replacement for a handle, possibly reducing the rights available. This invalidates
    /// the original handle. Wraps the
    /// [mx_handle_replace](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/handle_replace.md)
    /// syscall.
    fn replace(self, rights: Rights) -> Result<Self, Status> {
        self.get_ref().replace(rights).map(|handle| Self::from_handle(handle))
    }

    /// Set and clear userspace-accessible signal bits on an object. Wraps the
    /// [mx_object_signal](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/object_signal.md)
    /// syscall.
    fn signal(&self, clear_mask: Signals, set_mask: Signals) -> Result<(), Status> {
        self.get_ref().signal(clear_mask, set_mask)
    }

    /// Waits on a handle. Wraps the
    /// [mx_object_wait_one](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/object_wait_one.md)
    /// syscall.
    fn wait(&self, signals: Signals, deadline: Time) -> Result<Signals, Status> {
        self.get_ref().wait(signals, deadline)
    }

    /// Causes packet delivery on the given port when the object changes state and matches signals.
    /// [mx_object_wait_async](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/object_wait_async.md)
    /// syscall.
    fn wait_async(&self, port: &Port, key: u64, signals: Signals, options: WaitAsyncOpts)
        -> Result<(), Status>
    {
        self.get_ref().wait_async(port, key, signals, options)
    }

    /// A method for converting an untyped `Handle` into a more specific reference.
    fn from_handle(handle: Handle) -> Self;

    /// A method for converting the object into a generic Handle.
    // Not implemented as "From" because it would conflict in From<Handle> case
    fn into_handle(self) -> Handle {
        let raw_handle = self.get_ref().handle;
        std::mem::forget(self);
        Handle(raw_handle)
    }
}

/// A trait implemented by all handles for objects which have a peer.
pub trait Peered: HandleBase {
    /// Set and clear userspace-accessible signal bits on the object's peer. Wraps the
    /// [mx_object_signal_peer](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/object_signal.md)
    /// syscall.
    fn signal_peer(&self, clear_mask: Signals, set_mask: Signals) -> Result<(), Status> {
        let handle = self.get_ref().handle;
        let status = unsafe {
            sys::mx_object_signal_peer(handle, clear_mask.bits(), set_mask.bits())
        };
        into_result(status, || ())
    }
}

/// A trait implemented by all handles for objects which can have a cookie attached.
pub trait Cookied: HandleBase {
    /// Get the cookie attached to this object, if any. Wraps the
    /// [mx_object_get_cookie](https://fuchsia.googlesource.com/magenta/+/HEAD/docs/syscalls/object_get_cookie.md)
    /// syscall.
    fn get_cookie(&self, scope: &HandleRef) -> Result<u64, Status> {
        let handle = self.get_ref().handle;
        let mut cookie = 0;
        let status = unsafe { sys::mx_object_get_cookie(handle, scope.handle, &mut cookie) };
        into_result(status, || cookie)
    }

    /// Attach an opaque cookie to this object with the given scope. The cookie may be read or
    /// changed in future only with the same scope. Wraps the
    /// [mx_object_set_cookie](https://fuchsia.googlesource.com/magenta/+/HEAD/docs/syscalls/object_set_cookie.md)
    /// syscall.
    fn set_cookie(&self, scope: &HandleRef, cookie: u64) -> Result<(), Status> {
        let handle = self.get_ref().handle;
        let status = unsafe { sys::mx_object_set_cookie(handle, scope.handle, cookie) };
        into_result(status, || ())
    }
}

fn handle_drop(handle: sys::mx_handle_t) {
    let _ = unsafe { sys::mx_handle_close(handle) };
}

/// Wait on multiple handles.
/// The success return value is a bool indicating whether one or more of the
/// provided handle references was closed during the wait.
///
/// Wraps the
/// [mx_object_wait_many](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/object_wait_many.md)
/// syscall.
pub fn object_wait_many(items: &mut [WaitItem], deadline: Time) -> Result<bool, Status>
{
    let len = try!(items.len().value_into().map_err(|_| Status::ErrOutOfRange));
    let items_ptr = items.as_mut_ptr() as *mut sys::mx_wait_item_t;
    let status = unsafe { sys::mx_object_wait_many( items_ptr, len, deadline) };
    if status == sys::MX_ERR_CANCELED {
        return Ok((true))
    }
    into_result(status, || false)
}

// An untyped handle

/// An object representing a Magenta
/// [handle](https://fuchsia.googlesource.com/magenta/+/master/docs/handles.md).
///
/// Internally, it is represented as a 32-bit integer, but this wrapper enforces
/// strict ownership semantics. The `Drop` implementation closes the handle.
///
/// This type represents the most general reference to a kernel object, and can
/// be interconverted to and from more specific types. Those conversions are not
/// enforced in the type system; attempting to use them will result in errors
/// returned by the kernel. These conversions don't change the underlying
/// representation, but do change the type and thus what operations are available.
#[derive(Debug, Eq, PartialEq)]
pub struct Handle(sys::mx_handle_t);

impl HandleBase for Handle {
    fn get_ref(&self) -> HandleRef {
        HandleRef { handle: self.0, phantom: Default::default() }
    }

    fn from_handle(handle: Handle) -> Self {
        handle
    }
}

impl Drop for Handle {
    fn drop(&mut self) {
        handle_drop(self.0)
    }
}

impl Handle {
    /// If a raw handle is obtained from some other source, this method converts
    /// it into a type-safe owned handle.
    pub unsafe fn from_raw(raw: sys::mx_handle_t) -> Handle {
        Handle(raw)
    }
}

fn size_to_u32_sat(size: usize) -> u32 {
    u32::value_from(size).unwrap_or_saturate()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn monotonic_time_increases() {
        let time1 = time_get(ClockId::Monotonic);
        nanosleep(deadline_after(1_000));
        let time2 = time_get(ClockId::Monotonic);
        assert!(time2 > time1);
    }

    #[test]
    fn utc_time_increases() {
        let time1 = time_get(ClockId::UTC);
        nanosleep(deadline_after(1_000));
        let time2 = time_get(ClockId::UTC);
        assert!(time2 > time1);
    }

    #[test]
    fn thread_time_increases() {
        let time1 = time_get(ClockId::Thread);
        nanosleep(deadline_after(1_000));
        let time2 = time_get(ClockId::Thread);
        assert!(time2 > time1);
    }

    #[test]
    fn ticks_increases() {
        let ticks1 = ticks_get();
        nanosleep(deadline_after(1_000));
        let ticks2 = ticks_get();
        assert!(ticks2 > ticks1);
    }

    #[test]
    fn tick_length() {
        let sleep_ns = 1_000_000;  // 1ms
        let one_second_ns = 1_000_000_000; // 1 second in ns
        let ticks1 = ticks_get();
        nanosleep(deadline_after(sleep_ns));
        let ticks2 = ticks_get();
        // The number of ticks should have increased by at least 1 ms worth
        assert!(ticks2 > ticks1 + sleep_ns * ticks_per_second() / one_second_ns);
        // And not more than 4 ms worth
        assert!(ticks2 < ticks1 + 4 * sleep_ns * ticks_per_second() / one_second_ns);
    }

    #[test]
    fn sleep() {
        let sleep_ns = 1_000_000;  // 1ms
        let time1 = time_get(ClockId::Monotonic);
        nanosleep(deadline_after(sleep_ns));
        let time2 = time_get(ClockId::Monotonic);
        assert!(time2 > time1 + sleep_ns);
    }

    /// Test duplication by means of a VMO
    #[test]
    fn duplicate() {
        let hello_length: usize = 5;

        // Create a VMO and write some data to it.
        let vmo = Vmo::create(hello_length as u64, VmoOpts::Default).unwrap();
        assert!(vmo.write(b"hello", 0).is_ok());

        // Replace, reducing rights to read.
        let readonly_vmo = vmo.duplicate(MX_RIGHT_READ).unwrap();
        // Make sure we can read but not write.
        let mut read_vec = vec![0; hello_length];
        assert_eq!(readonly_vmo.read(&mut read_vec, 0).unwrap(), hello_length);
        assert_eq!(read_vec, b"hello");
        assert_eq!(readonly_vmo.write(b"", 0), Err(Status::ErrAccessDenied));

        // Write new data to the original handle, and read it from the new handle
        assert!(vmo.write(b"bye", 0).is_ok());
        assert_eq!(readonly_vmo.read(&mut read_vec, 0).unwrap(), hello_length);
        assert_eq!(read_vec, b"byelo");
    }

    // Test replace by means of a VMO
    #[test]
    fn replace() {
        let hello_length: usize = 5;

        // Create a VMO and write some data to it.
        let vmo = Vmo::create(hello_length as u64, VmoOpts::Default).unwrap();
        assert!(vmo.write(b"hello", 0).is_ok());

        // Replace, reducing rights to read.
        let readonly_vmo = vmo.replace(MX_RIGHT_READ).unwrap();
        // Make sure we can read but not write.
        let mut read_vec = vec![0; hello_length];
        assert_eq!(readonly_vmo.read(&mut read_vec, 0).unwrap(), hello_length);
        assert_eq!(read_vec, b"hello");
        assert_eq!(readonly_vmo.write(b"", 0), Err(Status::ErrAccessDenied));
    }

    #[test]
    fn wait_and_signal() {
        let event = Event::create(EventOpts::Default).unwrap();
        let ten_ms: Duration = 10_000_000;

        // Waiting on it without setting any signal should time out.
        assert_eq!(event.wait(MX_USER_SIGNAL_0, deadline_after(ten_ms)), Err(Status::ErrTimedOut));

        // If we set a signal, we should be able to wait for it.
        assert!(event.signal(MX_SIGNAL_NONE, MX_USER_SIGNAL_0).is_ok());
        assert_eq!(event.wait(MX_USER_SIGNAL_0, deadline_after(ten_ms)).unwrap(),
            MX_USER_SIGNAL_0 | MX_SIGNAL_LAST_HANDLE);

        // Should still work, signals aren't automatically cleared.
        assert_eq!(event.wait(MX_USER_SIGNAL_0, deadline_after(ten_ms)).unwrap(),
            MX_USER_SIGNAL_0 | MX_SIGNAL_LAST_HANDLE);

        // Now clear it, and waiting should time out again.
        assert!(event.signal(MX_USER_SIGNAL_0, MX_SIGNAL_NONE).is_ok());
        assert_eq!(event.wait(MX_USER_SIGNAL_0, deadline_after(ten_ms)), Err(Status::ErrTimedOut));
    }

    #[test]
    fn wait_many_and_signal() {
        let ten_ms: Duration = 10_000_000;
        let e1 = Event::create(EventOpts::Default).unwrap();
        let e2 = Event::create(EventOpts::Default).unwrap();

        // Waiting on them now should time out.
        let mut items = vec![
          WaitItem { handle: e1.get_ref(), waitfor: MX_USER_SIGNAL_0, pending: MX_SIGNAL_NONE },
          WaitItem { handle: e2.get_ref(), waitfor: MX_USER_SIGNAL_1, pending: MX_SIGNAL_NONE },
        ];
        assert_eq!(object_wait_many(&mut items, deadline_after(ten_ms)), Err(Status::ErrTimedOut));
        assert_eq!(items[0].pending, MX_SIGNAL_LAST_HANDLE);
        assert_eq!(items[1].pending, MX_SIGNAL_LAST_HANDLE);

        // Signal one object and it should return success.
        assert!(e1.signal(MX_SIGNAL_NONE, MX_USER_SIGNAL_0).is_ok());
        assert!(object_wait_many(&mut items, deadline_after(ten_ms)).is_ok());
        assert_eq!(items[0].pending, MX_USER_SIGNAL_0 | MX_SIGNAL_LAST_HANDLE);
        assert_eq!(items[1].pending, MX_SIGNAL_LAST_HANDLE);

        // Signal the other and it should return both.
        assert!(e2.signal(MX_SIGNAL_NONE, MX_USER_SIGNAL_1).is_ok());
        assert!(object_wait_many(&mut items, deadline_after(ten_ms)).is_ok());
        assert_eq!(items[0].pending, MX_USER_SIGNAL_0 | MX_SIGNAL_LAST_HANDLE);
        assert_eq!(items[1].pending, MX_USER_SIGNAL_1 | MX_SIGNAL_LAST_HANDLE);

        // Clear signals on both; now it should time out again.
        assert!(e1.signal(MX_USER_SIGNAL_0, MX_SIGNAL_NONE).is_ok());
        assert!(e2.signal(MX_USER_SIGNAL_1, MX_SIGNAL_NONE).is_ok());
        assert_eq!(object_wait_many(&mut items, deadline_after(ten_ms)), Err(Status::ErrTimedOut));
        assert_eq!(items[0].pending, MX_SIGNAL_LAST_HANDLE);
        assert_eq!(items[1].pending, MX_SIGNAL_LAST_HANDLE);
    }

    #[test]
    fn cookies() {
        let event = Event::create(EventOpts::Default).unwrap();
        let scope = Event::create(EventOpts::Default).unwrap();

        // Getting a cookie when none has been set should fail.
        assert_eq!(event.get_cookie(&scope.get_ref()), Err(Status::ErrAccessDenied));

        // Set a cookie.
        assert_eq!(event.set_cookie(&scope.get_ref(), 42), Ok(()));

        // Should get it back....
        assert_eq!(event.get_cookie(&scope.get_ref()), Ok(42));

        // but not with the wrong scope!
        assert_eq!(event.get_cookie(&event.get_ref()), Err(Status::ErrAccessDenied));

        // Can change it, with the same scope...
        assert_eq!(event.set_cookie(&scope.get_ref(), 123), Ok(()));

        // but not with a different scope.
        assert_eq!(event.set_cookie(&event.get_ref(), 123), Err(Status::ErrAccessDenied));
    }

    #[test]
    fn cprng() {
        let mut buffer = [0; 20];
        assert_eq!(cprng_draw(&mut buffer), Ok(20));
        assert_ne!(buffer[0], 0);
        assert_ne!(buffer[19], 0);
    }

    #[test]
    fn cprng_too_large() {
        let mut buffer = [0; MX_CPRNG_DRAW_MAX_LEN + 1];
        assert_eq!(cprng_draw(&mut buffer), Err(Status::ErrInvalidArgs));

        for mut s in buffer.chunks_mut(MX_CPRNG_DRAW_MAX_LEN) {
            assert_eq!(cprng_draw(&mut s), Ok(s.len()));
        }
    }

    #[test]
    fn cprng_add() {
        let buffer = [0, 1, 2];
        assert_eq!(cprng_add_entropy(&buffer), Ok(()));
    }
}
