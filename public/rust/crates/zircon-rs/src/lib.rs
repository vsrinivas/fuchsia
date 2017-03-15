// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Magenta kernel
//! [syscalls](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls.md).

extern crate core;
extern crate magenta_sys;
extern crate conv;

use std::marker::PhantomData;
use std::mem;

use conv::{ValueInto, ValueFrom, UnwrapOrSaturate};

mod event;
mod eventpair;
mod socket;

pub use event::{Event, EventOpts};
pub use eventpair::{EventPair, EventPairOpts};
pub use socket::{Socket, SocketOpts, SocketReadOpts, SocketWriteOpts};

use magenta_sys as sys;

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
    ErrNoResources = -5,
    ErrNoMemory = -4,
    ErrInvalidArgs = -10,
    ErrWrongType = -54,
    ErrBadSyscall = -11,
    ErrBadHandle = -12,
    ErrOutOfRange = -13,
    ErrBufferTooSmall = -14,
    ErrBadState = -20,
    ErrNotFound = -3,
    ErrAlreadyExists = -15,
    ErrAlreadyBound = -16,
    ErrTimedOut = -23,
    ErrHandleClosed = -24,
    ErrRemoteClosed = -25,
    ErrUnavailable = -26,
    ErrShouldWait = -27,
    ErrAccessDenied = -30,
    ErrIo = -40,
    ErrIoRefused = -41,
    ErrIoDataIntegrity = -42,
    ErrIoDataLoss = -43,
    ErrBadPath = -50,
    ErrNotDir = -51,
    ErrNotFile = -52,

    /// Any mx_status_t not in the set above will map to the following:
    UnknownOther = -32768,
}

impl Status {
    // should these conversions be public?
    fn from_raw(raw: sys::mx_status_t) -> Self {
        match raw {
            // Auto-generated using tools/gen_status.py
            sys::NO_ERROR => Status::NoError,
            sys::ERR_INTERNAL => Status::ErrInternal,
            sys::ERR_NOT_SUPPORTED => Status::ErrNotSupported,
            sys::ERR_NO_RESOURCES => Status::ErrNoResources,
            sys::ERR_NO_MEMORY => Status::ErrNoMemory,
            sys::ERR_INVALID_ARGS => Status::ErrInvalidArgs,
            sys::ERR_WRONG_TYPE => Status::ErrWrongType,
            sys::ERR_BAD_SYSCALL => Status::ErrBadSyscall,
            sys::ERR_BAD_HANDLE => Status::ErrBadHandle,
            sys::ERR_OUT_OF_RANGE => Status::ErrOutOfRange,
            sys::ERR_BUFFER_TOO_SMALL => Status::ErrBufferTooSmall,
            sys::ERR_BAD_STATE => Status::ErrBadState,
            sys::ERR_NOT_FOUND => Status::ErrNotFound,
            sys::ERR_ALREADY_EXISTS => Status::ErrAlreadyExists,
            sys::ERR_ALREADY_BOUND => Status::ErrAlreadyBound,
            sys::ERR_TIMED_OUT => Status::ErrTimedOut,
            sys::ERR_HANDLE_CLOSED => Status::ErrHandleClosed,
            sys::ERR_REMOTE_CLOSED => Status::ErrRemoteClosed,
            sys::ERR_UNAVAILABLE => Status::ErrUnavailable,
            sys::ERR_SHOULD_WAIT => Status::ErrShouldWait,
            sys::ERR_ACCESS_DENIED => Status::ErrAccessDenied,
            sys::ERR_IO => Status::ErrIo,
            sys::ERR_IO_REFUSED => Status::ErrIoRefused,
            sys::ERR_IO_DATA_INTEGRITY => Status::ErrIoDataIntegrity,
            sys::ERR_IO_DATA_LOSS => Status::ErrIoDataLoss,
            sys::ERR_BAD_PATH => Status::ErrBadPath,
            sys::ERR_NOT_DIR => Status::ErrNotDir,
            sys::ERR_NOT_FILE => Status::ErrNotFile,
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
        MX_OBJECT_SIGNAL_0,
        MX_OBJECT_SIGNAL_1,
        MX_OBJECT_SIGNAL_2,
        MX_OBJECT_SIGNAL_3,
        MX_OBJECT_SIGNAL_4,
        MX_OBJECT_SIGNAL_5,
        MX_OBJECT_SIGNAL_6,
        MX_OBJECT_SIGNAL_7,
        MX_OBJECT_SIGNAL_8,
        MX_OBJECT_SIGNAL_9,
        MX_OBJECT_SIGNAL_10,
        MX_OBJECT_SIGNAL_11,
        MX_OBJECT_SIGNAL_12,
        MX_OBJECT_SIGNAL_13,
        MX_OBJECT_SIGNAL_14,
        MX_OBJECT_SIGNAL_15,
        MX_OBJECT_SIGNAL_16,
        MX_OBJECT_SIGNAL_17,
        MX_OBJECT_SIGNAL_18,
        MX_OBJECT_SIGNAL_19,
        MX_OBJECT_SIGNAL_20,
        MX_OBJECT_SIGNAL_21,
        MX_OBJECT_SIGNAL_22,
        MX_OBJECT_SIGNAL_23,

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

        // Data pipe
        MX_DATAPIPE_READABLE,
        MX_DATAPIPE_WRITABLE,
        MX_DATAPIPE_PEER_CLOSED,
        MX_DATAPIPE_READ_THRESHOLD,
        MX_DATAPIPE_WRITE_THRESHOLD,
};

/// Options for creating a channel.
#[repr(u32)]
pub enum ChannelOpts {
    /// A normal channel.
    Normal = 0,
}

/// Options for creating wait sets. None supported yet.
#[repr(u32)]
pub enum WaitSetOpts {
    /// Default options.
    Default = 0,
}

/// Options for creating virtual memory objects. None supported yet.
#[repr(u32)]
pub enum VmoOpts {
    /// Default options.
    Default = 0,
}

impl Default for ChannelOpts {
    fn default() -> Self {
        ChannelOpts::Normal
    }
}

impl Default for WaitSetOpts {
    fn default() -> Self {
        WaitSetOpts::Default
    }
}

impl Default for VmoOpts {
    fn default() -> Self {
        VmoOpts::Default
    }
}

/// A "wait item" containing a handle reference and information about what signals
/// to wait on, and, on return from `object_wait_many`, which are pending.
#[repr(C)]
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

/// Sleep the given number of nanoseconds.
///
/// Wraps the
/// [mx_nanosleep](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/nanosleep.md)
/// syscall.
pub fn nanosleep(time: Time) {
    unsafe { sys::mx_nanosleep(time); }
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

    fn wait(&self, signals: Signals, timeout: Time) -> Result<Signals, Status> {
        let handle = self.handle;
        let mut pending = sys::mx_signals_t::empty();
        let status = unsafe {
            sys::mx_object_wait_one(handle, signals, timeout, &mut pending)
        };
        into_result(status, || pending)
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
    /// [object_wait_one](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/object_wait_one.md)
    /// syscall.
    fn wait(&self, signals: Signals, timeout: Time) -> Result<Signals, Status> {
        self.get_ref().wait(signals, timeout)
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
pub fn object_wait_many(items: &mut [WaitItem], timeout: Time) -> Result<bool, Status>
{
    let len = try!(items.len().value_into().map_err(|_| Status::ErrOutOfRange));
    let items_ptr = items.as_mut_ptr() as *mut sys::mx_wait_item_t;
    let status = unsafe { sys::mx_object_wait_many( items_ptr, len, timeout) };
    if status == sys::ERR_HANDLE_CLOSED {
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

// Channels

/// An object representing a Magenta
/// [channel](https://fuchsia.googlesource.com/magenta/+/master/docs/objects/channel.md).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
pub struct Channel(Handle);

impl HandleBase for Channel {
    fn get_ref(&self) -> HandleRef {
        self.0.get_ref()
    }

    fn from_handle(handle: Handle) -> Self {
        Channel(handle)
    }
}

impl Peered for Channel {
}

impl Channel {
    /// Create a channel, resulting an a pair of `Channel` objects representing both
    /// sides of the channel. Messages written into one maybe read from the opposite.
    ///
    /// Wraps the
    /// [mx_channel_create](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/channel_create.md)
    /// syscall.
    pub fn create(opts: ChannelOpts) -> Result<(Channel, Channel), Status> {
        unsafe {
            let mut handle0 = 0;
            let mut handle1 = 0;
            let status = sys::mx_channel_create(opts as u32, &mut handle0, &mut handle1);
            into_result(status, ||
                (Self::from_handle(Handle(handle0)),
                    Self::from_handle(Handle(handle1))))
        }
    }

    /// Read a message from a channel. Wraps the
    /// [mx_channel_read](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/channel_read.md)
    /// syscall.
    ///
    /// If the `MessageBuf` lacks the capacity to hold the pending message,
    /// returns an `Err` with the number of bytes and number of handles needed.
    /// Otherwise returns an `Ok` with the result as usual.
    pub fn read_raw(&self, opts: u32, buf: &mut MessageBuf)
        -> Result<Result<(), Status>, (usize, usize)>
    {
        unsafe {
            buf.reset_handles();
            let raw_handle = self.raw_handle();
            let mut num_bytes: u32 = size_to_u32_sat(buf.bytes.capacity());
            let mut num_handles: u32 = size_to_u32_sat(buf.handles.capacity());
            let status = sys::mx_channel_read(raw_handle, opts,
                buf.bytes.as_mut_ptr(), num_bytes, &mut num_bytes,
                buf.handles.as_mut_ptr(), num_handles, &mut num_handles);
            if status == sys::ERR_BUFFER_TOO_SMALL {
                Err((num_bytes as usize, num_handles as usize))
            } else {
                Ok(into_result(status, || {
                    buf.bytes.set_len(num_bytes as usize);
                    buf.handles.set_len(num_handles as usize);
                }))
            }
        }
    }

    /// Read a message from a channel.
    ///
    /// Note that this method can cause internal reallocations in the `MessageBuf`
    /// if it is lacks capacity to hold the full message. If such reallocations
    /// are not desirable, use `read_raw` instead.
    pub fn read(&self, opts: u32, buf: &mut MessageBuf) -> Result<(), Status> {
        loop {
            match self.read_raw(opts, buf) {
                Ok(result) => return result,
                Err((num_bytes, num_handles)) => {
                    buf.ensure_capacity_bytes(num_bytes);
                    buf.ensure_capacity_handles(num_handles);
                }
            }
        }
    }

    /// Write a message to a channel. Wraps the
    /// [mx_channel_write](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/channel_write.md)
    /// syscall.
    pub fn write(&self, bytes: &[u8], handles: &mut Vec<Handle>, opts: u32)
            -> Result<(), Status>
    {
        unsafe {
            let n_bytes = try!(bytes.len().value_into().map_err(|_| Status::ErrOutOfRange));
            let n_handles = try!(handles.len().value_into().map_err(|_| Status::ErrOutOfRange));
            let status = sys::mx_channel_write(self.raw_handle(), opts, bytes.as_ptr(), n_bytes,
                handles.as_ptr() as *const sys::mx_handle_t, n_handles);
            into_result(status, || {
                // Handles were successfully transferred, forget them on sender side
                handles.set_len(0);
            })
        }
    }
}

/// A buffer for _receiving_ messages from a channel.
///
/// A `MessageBuf` is essentially a byte buffer and a vector of
/// handles, but move semantics for "taking" handles requires special handling.
///
/// Note that for sending messages to a channel, the caller manages the buffers,
/// using a plain byte slice and `Vec<Handle>`.
#[derive(Default)]
pub struct MessageBuf {
    bytes: Vec<u8>,
    handles: Vec<sys::mx_handle_t>,
}

impl MessageBuf {
    /// Create a new, empty, message buffer.
    pub fn new() -> Self {
        Default::default()
    }

    /// Ensure that the buffer has the capacity to hold at least `n_bytes` bytes.
    pub fn ensure_capacity_bytes(&mut self, n_bytes: usize) {
        ensure_capacity(&mut self.bytes, n_bytes);
    }

    /// Ensure that the buffer has the capacity to hold at least `n_handles` handles.
    pub fn ensure_capacity_handles(&mut self, n_handles: usize) {
        ensure_capacity(&mut self.handles, n_handles);
    }

    /// Get a reference to the bytes of the message buffer, as a `&[u8]` slice.
    pub fn bytes(&self) -> &[u8] {
        self.bytes.as_slice()
    }

    /// The number of handles in the message buffer. Note this counts the number
    /// available when the message was received; `take_handle` does not affect
    /// the count.
    pub fn n_handles(&self) -> usize {
        self.handles.len()
    }

    /// Take the handle at the specified index from the message buffer. If the
    /// method is called again with the same index, it will return `None`, as
    /// will happen if the index exceeds the number of handles available.
    pub fn take_handle(&mut self, index: usize) -> Option<Handle> {
        self.handles.get_mut(index).and_then(|handleref|
            if *handleref == INVALID_HANDLE {
                None
            } else {
                Some(Handle(mem::replace(handleref, INVALID_HANDLE)))
            }
        )
    }

    fn drop_handles(&mut self) {
        for &handle in &self.handles {
            if handle != 0 {
                handle_drop(handle);
            }
        }
    }

    fn reset_handles(&mut self) {
        self.drop_handles();
        self.handles.clear();
    }
}

impl Drop for MessageBuf {
    fn drop(&mut self) {
        self.drop_handles();
    }
}

fn size_to_u32_sat(size: usize) -> u32 {
    u32::value_from(size).unwrap_or_saturate()
}

fn ensure_capacity<T>(vec: &mut Vec<T>, size: usize) {
    let len = vec.len();
    if size > len {
        vec.reserve(size - len);
    }
}

// Wait sets

// This is the lowest level interface, strictly in terms of cookies.

/// An object representing a Magenta
/// [waitset](https://fuchsia.googlesource.com/magenta/+/master/docs/objects/waitset.md).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
pub struct WaitSet(Handle);

impl HandleBase for WaitSet {
    fn get_ref(&self) -> HandleRef {
        self.0.get_ref()
    }

    fn from_handle(handle: Handle) -> Self {
        WaitSet(handle)
    }
}

impl WaitSet {
    /// Create a wait set.
    ///
    /// Wraps the
    /// [mx_waitset_create](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/waitset_create.md)
    /// sycall.
    pub fn create(options: WaitSetOpts) -> Result<WaitSet, Status> {
        let mut handle = 0;
        let status = unsafe { sys::mx_waitset_create(options as u32, &mut handle) };
        into_result(status, ||
            WaitSet::from_handle(Handle(handle)))
    }

    /// Add an entry to a wait set.
    ///
    /// Wraps the
    /// [mx_waitset_add](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/waitset_add.md)
    /// sycall.
    pub fn add<H>(&self, handle: &H, cookie: u64, signals: Signals) -> Result<(), Status>
        where H: HandleBase
    {
        let status = unsafe {
            sys::mx_waitset_add(self.raw_handle(), cookie, handle.raw_handle(), signals)
        };
        into_result(status, || ())
    }

    /// Remove an entry from a wait set.
    ///
    /// Wraps the
    /// [mx_waitset_remove](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/waitset_remove.md)
    /// sycall.
    pub fn remove(&self, cookie: u64) -> Result<(), Status> {
        let status = unsafe { sys::mx_waitset_remove(self.raw_handle(), cookie) };
        into_result(status, || ())
    }

    /// Wait for one or more entires to be signalled.
    ///
    /// Wraps the
    /// [mx_waitset_wait](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/waitset_wait.md)
    /// sycall.
    ///
    /// The caller must make sure `results` has enough capacity. If the length is
    /// equal to the capacity on return, that may be interpreted as a sign that
    /// the capacity should be expanded.
    pub fn wait(&self, timeout: Time, results: &mut Vec<WaitSetResult>)
        -> Result<(), Status>
    {
        unsafe {
            let mut count = size_to_u32_sat(results.capacity());
            let status = sys::mx_waitset_wait(self.raw_handle(), timeout,
                results.as_mut_ptr() as *mut sys::mx_waitset_result_t,
                &mut count);
            if status != sys::NO_ERROR {
                results.clear();
                return Err(Status::from_raw(status));
            }
            results.set_len(count as usize);
            Ok(())
        }
    }
}

/// An element of the result of `WaitSet::wait`. See
/// [waitset_wait](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/waitset_wait.md)
/// for more information about the underlying structure.
pub struct WaitSetResult(sys::mx_waitset_result_t);

impl WaitSetResult {
    /// The cookie used to identify the wait, same as was given in `WaitSet::add`.
    pub fn cookie(&self) -> u64 {
        self.0.cookie
    }

    /// The status. NoError if the signals are satisfied, ErrBadState if the signals
    /// became unsatisfiable, or ErrHandleClosed if the handle was dropped.
    pub fn status(&self) -> Status {
        Status::from_raw(self.0.status)
    }

    /// The observed signals at some point shortly before `WaitSet::wait` returned.
    pub fn observed(&self) -> Signals {
        self.0.observed
    }
}

// Virtual Memory Objects

/// An object representing a Magenta
/// [virtual memory object](https://fuchsia.googlesource.com/magenta/+/master/docs/objects/vm_object.md).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
pub struct Vmo(Handle);

impl HandleBase for Vmo {
    fn get_ref(&self) -> HandleRef {
        self.0.get_ref()
    }

    fn from_handle(handle: Handle) -> Self {
        Vmo(handle)
    }
}

impl Vmo {
    /// Create a virtual memory object.
    ///
    /// Wraps the
    /// `mx_vmo_create`
    /// syscall. See the
    /// [Shared Memory: Virtual Memory Objects (VMOs)](https://fuchsia.googlesource.com/magenta/+/master/docs/concepts.md#Shared-Memory_Virtual-Memory-Objects-VMOs)
    /// for more information.
    pub fn create(size: u64, options: VmoOpts) -> Result<Vmo, Status> {
        let mut handle = 0;
        let status = unsafe { sys::mx_vmo_create(size, options as u32, &mut handle) };
        into_result(status, ||
            Vmo::from_handle(Handle(handle)))
    }

    /// Read from a virtual memory object.
    ///
    /// Wraps the `mx_vmo_read` syscall.
    pub fn read(&self, data: &mut [u8], offset: u64) -> Result<usize, Status> {
        unsafe {
            let mut actual = 0;
            let status = sys::mx_vmo_read(self.raw_handle(), data.as_mut_ptr(),
                offset, data.len(), &mut actual);
            into_result(status, || actual)
        }
    }

    /// Write to a virtual memory object.
    ///
    /// Wraps the `mx_vmo_write` syscall.
    pub fn write(&self, data: &[u8], offset: u64) -> Result<usize, Status> {
        unsafe {
            let mut actual = 0;
            let status = sys::mx_vmo_write(self.raw_handle(), data.as_ptr(),
                offset, data.len(), &mut actual);
            into_result(status, || actual)
        }
    }

    /// Get the size of a virtual memory object.
    ///
    /// Wraps the `mx_vmo_get_size` syscall.
    pub fn get_size(&self) -> Result<u64, Status> {
        let mut size = 0;
        let status = unsafe { sys::mx_vmo_get_size(self.raw_handle(), &mut size) };
        into_result(status, || size)
    }

    /// Attempt to change the size of a virtual memory object.
    ///
    /// Wraps the `mx_vmo_set_size` syscall.
    pub fn set_size(&self, size: u64) -> Result<(), Status> {
        let status = unsafe { sys::mx_vmo_set_size(self.raw_handle(), size) };
        into_result(status, || ())
    }
}

// Data pipes (just a stub for now)

/// An object representing a Magenta
/// [data pipe](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/datapipe_create.md)
/// producer.
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
pub struct DataPipeProducer(Handle);

impl HandleBase for DataPipeProducer {
    fn get_ref(&self) -> HandleRef {
        self.0.get_ref()
    }

    fn from_handle(handle: Handle) -> Self {
        DataPipeProducer(handle)
    }
}

/// An object representing a Magenta
/// [data pipe](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/datapipe_create.md)
/// consumer.
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
pub struct DataPipeConsumer(Handle);

impl HandleBase for DataPipeConsumer {
    fn get_ref(&self) -> HandleRef {
        self.0.get_ref()
    }

    fn from_handle(handle: Handle) -> Self {
        DataPipeConsumer(handle)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn monotonic_time_increases() {
        let time1 = time_get(ClockId::Monotonic);
        let time2 = time_get(ClockId::Monotonic);
        assert!(time2 > time1);
    }

    #[test]
    fn utc_time_increases() {
        let time1 = time_get(ClockId::UTC);
        let time2 = time_get(ClockId::UTC);
        assert!(time2 > time1);
    }

    #[test]
    fn thread_time_increases() {
        let time1 = time_get(ClockId::Thread);
        let time2 = time_get(ClockId::Thread);
        assert!(time2 > time1);
    }

    #[test]
    fn sleep() {
        let sleep_ns = 1_000_000;  // 1ms
        let time1 = time_get(ClockId::Monotonic);
        nanosleep(sleep_ns);
        let time2 = time_get(ClockId::Monotonic);
        assert!(time2 > time1 + sleep_ns);
    }

    #[test]
    fn vmo_get_size() {
        let size = 16 * 1024 * 1024;
        let vmo = Vmo::create(size, VmoOpts::Default).unwrap();
        assert_eq!(size, vmo.get_size().unwrap());
    }

    #[test]
    fn vmo_set_size() {
        let start_size = 12;
        let vmo = Vmo::create(start_size, VmoOpts::Default).unwrap();
        assert_eq!(start_size, vmo.get_size().unwrap());

        // Change the size and make sure the new size is reported
        let new_size = 23;
        assert!(vmo.set_size(new_size).is_ok());
        assert_eq!(new_size, vmo.get_size().unwrap());
    }

    #[test]
    fn vmo_read_write() {
        let mut vec1 = vec![0; 16];
        let vmo = Vmo::create(vec1.len() as u64, VmoOpts::Default).unwrap();
        vmo.write(b"abcdef", 0).unwrap();
        assert_eq!(16, vmo.read(&mut vec1, 0).unwrap());
        assert_eq!(b"abcdef", &vec1[0..6]);
        vmo.write(b"123", 2).unwrap();
        assert_eq!(16, vmo.read(&mut vec1, 0).unwrap());
        assert_eq!(b"ab123f", &vec1[0..6]);
        assert_eq!(15, vmo.read(&mut vec1, 1).unwrap());
        assert_eq!(b"b123f", &vec1[0..5]);
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
    fn channel_basic() {
        let (p1, p2) = Channel::create(ChannelOpts::Normal).unwrap();

        let mut empty = vec![];
        assert!(p1.write(b"hello", &mut empty, 0).is_ok());

        let mut buf = MessageBuf::new();
        assert!(p2.read(0, &mut buf).is_ok());
        assert_eq!(buf.bytes(), b"hello");
    }

    #[test]
    fn channel_read_raw_too_small() {
        let (p1, p2) = Channel::create(ChannelOpts::Normal).unwrap();

        let mut empty = vec![];
        assert!(p1.write(b"hello", &mut empty, 0).is_ok());

        let mut buf = MessageBuf::new();
        let result = p2.read_raw(0, &mut buf);
        assert_eq!(result, Err((5, 0)));
        assert_eq!(buf.bytes(), b"");
    }

    #[test]
    fn channel_send_handle() {
        let hello_length: usize = 5;

        // Create a pair of channels and a virtual memory object.
        let (p1, p2) = Channel::create(ChannelOpts::Normal).unwrap();
        let vmo = Vmo::create(hello_length as u64, VmoOpts::Default).unwrap();

        // Create a virtual memory object and send it down the channel.
        let duplicate_vmo_handle = vmo.duplicate(MX_RIGHT_SAME_RIGHTS).unwrap().into_handle();
        let mut handles_to_send: Vec<Handle> = vec![duplicate_vmo_handle];
        assert!(p1.write(b"", &mut handles_to_send, 0).is_ok());

        // Read the handle from the receiving channel.
        let mut buf = MessageBuf::new();
        assert!(p2.read(0, &mut buf).is_ok());
        assert_eq!(buf.n_handles(), 1);
        // Take the handle from the buffer.
        let received_handle = buf.take_handle(0).unwrap();
        // Should not affect number of handles.
        assert_eq!(buf.n_handles(), 1);
        // Trying to take it again should fail.
        assert!(buf.take_handle(0).is_none());

        // Now to test that we got the right handle, try writing something to it...
        let received_vmo = Vmo::from_handle(received_handle);
        assert_eq!(received_vmo.write(b"hello", 0).unwrap(), hello_length);

        // ... and reading it back from the original VMO.
        let mut read_vec = vec![0; hello_length];
        assert_eq!(vmo.read(&mut read_vec, 0).unwrap(), hello_length);
        assert_eq!(read_vec, b"hello");
    }

    #[test]
    fn wait_and_signal() {
        let event = Event::create(EventOpts::Default).unwrap();
        let ten_ms: Time = 10_000_000;

        // Waiting on it without setting any signal should time out.
        assert_eq!(event.wait(MX_USER_SIGNAL_0, ten_ms), Err(Status::ErrTimedOut));

        // If we set a signal, we should be able to wait for it.
        assert!(event.signal(MX_SIGNAL_NONE, MX_USER_SIGNAL_0).is_ok());
        assert_eq!(event.wait(MX_USER_SIGNAL_0, ten_ms).unwrap(), MX_USER_SIGNAL_0);

        // Should still work, signals aren't automatically cleared.
        assert_eq!(event.wait(MX_USER_SIGNAL_0, ten_ms).unwrap(), MX_USER_SIGNAL_0);

        // Now clear it, and waiting should time out again.
        assert!(event.signal(MX_USER_SIGNAL_0, MX_SIGNAL_NONE).is_ok());
        assert_eq!(event.wait(MX_USER_SIGNAL_0, ten_ms), Err(Status::ErrTimedOut));
    }

    #[test]
    fn wait_and_signal_peer() {
        let (p1, p2) = EventPair::create(EventPairOpts::Default).unwrap();
        let ten_ms: Time = 10_000_000;

        // Waiting on one without setting any signal should time out.
        assert_eq!(p2.wait(MX_USER_SIGNAL_0, ten_ms), Err(Status::ErrTimedOut));

        // If we set a signal, we should be able to wait for it.
        assert!(p1.signal_peer(MX_SIGNAL_NONE, MX_USER_SIGNAL_0).is_ok());
        assert_eq!(p2.wait(MX_USER_SIGNAL_0, ten_ms).unwrap(), MX_USER_SIGNAL_0);

        // Should still work, signals aren't automatically cleared.
        assert_eq!(p2.wait(MX_USER_SIGNAL_0, ten_ms).unwrap(), MX_USER_SIGNAL_0);

        // Now clear it, and waiting should time out again.
        assert!(p1.signal_peer(MX_USER_SIGNAL_0, MX_SIGNAL_NONE).is_ok());
        assert_eq!(p2.wait(MX_USER_SIGNAL_0, ten_ms), Err(Status::ErrTimedOut));
    }

    #[test]
    fn waitset() {
        let ten_ms: Time = 10_000_000;
        let cookie1 = 1;
        let cookie2 = 2;
        let e1 = Event::create(EventOpts::Default).unwrap();
        let e2 = Event::create(EventOpts::Default).unwrap();

        let waitset = WaitSet::create(WaitSetOpts::Default).unwrap();
        assert!(waitset.add(&e1, cookie1, MX_USER_SIGNAL_0).is_ok());
        // Adding another handle with the same cookie should fail
        assert_eq!(waitset.add(&e2, cookie1, MX_USER_SIGNAL_0), Err(Status::ErrAlreadyExists));
        assert!(waitset.add(&e2, cookie2, MX_USER_SIGNAL_1).is_ok());

        // Waiting on the waitset now should time out.
        let mut results = Vec::with_capacity(2);
        assert_eq!(waitset.wait(ten_ms, &mut results), Err(Status::ErrTimedOut));
        assert_eq!(results.len(), 0);

        // Signal one object and it should return success.
        assert!(e1.signal(MX_SIGNAL_NONE, MX_USER_SIGNAL_0).is_ok());
        assert!(waitset.wait(ten_ms, &mut results).is_ok());
        assert_eq!(results.len(), 1);
        assert_eq!(results[0].cookie(), cookie1);
        assert_eq!(results[0].status(), Status::NoError);
        assert_eq!(results[0].observed(), MX_USER_SIGNAL_0);

        // Signal the other and it should return both.
        assert!(e2.signal(MX_SIGNAL_NONE, MX_USER_SIGNAL_1).is_ok());
        assert!(waitset.wait(ten_ms, &mut results).is_ok());
        assert_eq!(results.len(), 2);
        assert_eq!(results[0].cookie(), cookie1);
        assert_eq!(results[0].status(), Status::NoError);
        assert_eq!(results[0].observed(), MX_USER_SIGNAL_0);
        assert_eq!(results[1].cookie(), cookie2);
        assert_eq!(results[1].status(), Status::NoError);
        assert_eq!(results[1].observed(), MX_USER_SIGNAL_1);

        // Remove one and clear signals on the other; now it should time out again.
        assert!(waitset.remove(cookie1).is_ok());
        assert!(e2.signal(MX_USER_SIGNAL_1, MX_SIGNAL_NONE).is_ok());
        assert_eq!(waitset.wait(ten_ms, &mut results), Err(Status::ErrTimedOut));
        assert_eq!(results.len(), 0);
    }
}
