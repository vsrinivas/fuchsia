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

mod eventpair;
mod socket;

pub use eventpair::EventPair;
pub use socket::Socket;

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
    /// A "reply channel", where writing a message also passes ownership of the
    /// channel back to the peer.
    ReplyChannel = sys::MX_CHANNEL_CREATE_REPLY_CHANNEL,
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
/// to wait on, and, on return from `handle_wait_many`, which are pending.
#[repr(C)]
pub struct WaitItem<'a> {
    /// The handle to wait on.
    pub handle: HandleRef<'a>,
    /// A set of signals to wait for.
    pub waitfor: Signals,
    /// The set of signals pending, on return of `handle_wait_many`.
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
/// [mx_nanosleep](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/nano_sleep.md)
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
        let result = unsafe { sys::mx_handle_duplicate(handle, rights) };
        if result < 0 {
            Err(Status::from_raw(result))
        } else {
            Ok(Handle(result))
        }
    }

    fn wait(&self, signals: Signals, timeout: Time) -> Result<Signals, Status> {
        let handle = self.handle;
        let mut pending = sys::mx_signals_t::empty();
        let status = unsafe {
            sys::mx_handle_wait_one(handle, signals, timeout, &mut pending)
        };
        into_result(status, || pending)
    }
}

/// A trait implemented by all handle objects.
///
/// Note: it is reasonable for user-defined objects wrapping a handle to implement
/// this trait. For example, a speficic interface in some protocol might be
/// represented as a newtype of `Channel`, and implement the `get_ref` and
/// `from_handle` methods to facilitate conversion from and to the interface.
pub trait HandleBase: Sized {
    /// Get a reference to the handle. One important use of such a reference is
    /// for `handle_wait_many`.
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

    /// Waits on a handle. Wraps the
    /// [handle_wait_one](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/handle_wait_one.md)
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

fn handle_drop(handle: sys::mx_handle_t) {
    let _ = unsafe { sys::mx_handle_close(handle) };
}

/// Wait on multiple handles.
/// The success return value is a bool indicating whether one or more of the
/// provided handle references was closed during the wait.
///
/// Wraps the
/// [mx_handle_wait_many](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/handle_wait_many.md)
/// syscall.
pub fn handle_wait_many(items: &mut [WaitItem], timeout: Time) -> Result<bool, Status>
{
    let len = try!(items.len().value_into().map_err(|_| Status::ErrOutOfRange));
    let items_ptr = items.as_mut_ptr() as *mut sys::mx_wait_item_t;
    let status = unsafe { sys::mx_handle_wait_many( items_ptr, len, timeout) };
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

    fn write_raw(handle: sys::mx_handle_t, bytes: &[u8], handles: &mut Vec<Handle>,
            opts: u32) -> Result<(), Status>
    {
        unsafe {
            let n_bytes = try!(bytes.len().value_into().map_err(|_| Status::ErrOutOfRange));
            let n_handles = try!(handles.len().value_into().map_err(|_| Status::ErrOutOfRange));
            let status = sys::mx_channel_write(handle, opts, bytes.as_ptr(), n_bytes,
                handles.as_ptr() as *const sys::mx_handle_t, n_handles);
            into_result(status, || {
                // Handles were successfully transferred, forget them on sender side
                handles.set_len(0);
            })
        }
    }

    /// Write a message to a channel. Wraps the
    /// [mx_channel_write](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/channel_write.md)
    /// syscall.
    ///
    /// This method should be used on normal channels. For reply channels, use
    /// `write_reply` instead.
    pub fn write(&self, bytes: &[u8], handles: &mut Vec<Handle>, opts: u32)
            -> Result<(), Status>
    {
        Self::write_raw(self.raw_handle(), bytes, handles, opts)
    }

    /// Write a message to a channel. Wraps the
    /// [mx_channel_write](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/channel_write.md)
    /// syscall.
    ///
    /// This method should be used on reply channels. For normal channels, use
    /// `write` instead. On success, ownership of `self` is transferred across
    /// the channel. On error, the `Err` result contains the `self` handle so
    /// ownership is passed back to the caller.
    pub fn write_reply(self, bytes: &[u8], handles: &mut Vec<Handle>, opts: u32)
            -> Result<(), (Self, Status)>
    {
        let raw_handle = self.raw_handle();
        handles.push(self.into_handle());
        Self::write_raw(raw_handle, bytes, handles, opts).map_err(|status|
            (Self::from_handle(handles.pop().unwrap()), status)
        )
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
    fn time_increases() {
        let time1 = time_get(ClockId::Monotonic);
        let time2 = time_get(ClockId::Monotonic);
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
    fn vmo_size() {
        let size = 16 * 1024 * 1024;
        let vmo = Vmo::create(size, VmoOpts::Default).unwrap();
        assert_eq!(size as u64, vmo.get_size().unwrap());
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

    #[test]
    fn channel_reply_basic() {
        let (p1, p2) = Channel::create(ChannelOpts::Normal).unwrap();

        // Don't need to test trying to include self-handle, ownership forbids it
        let mut empty = vec![];
        let (p1, _status) = p1.write_reply(b"hello", &mut empty, 0).err().unwrap();
        assert!(p1.write(b"hello", &mut empty, 0).is_ok());
        let (p2, _status) = p2.write_reply(b"hello", &mut empty, 0).err().unwrap();
        assert!(p2.write(b"hello", &mut empty, 0).is_ok());

        let (p1, p2) = Channel::create(ChannelOpts::ReplyChannel).unwrap();
        let (p1, _status) = p1.write_reply(b"hello", &mut empty, 0).err().unwrap();
        assert!(p1.write(b"hello", &mut empty, 0).is_ok());
        assert!(p2.write(b"hello", &mut empty, 0).is_err());
        assert!(p2.write_reply(b"hello", &mut empty, 0).is_ok());
    }
}
