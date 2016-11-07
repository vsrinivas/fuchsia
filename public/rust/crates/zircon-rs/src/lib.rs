// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate core;
extern crate magenta_sys;
extern crate conv;

use std::marker::PhantomData;

use conv::{ValueInto, ValueFrom, UnwrapOrSaturate};

use magenta_sys as sys;

type Time = sys::mx_time_t;
pub use magenta_sys::MX_TIME_INFINITE;

#[derive(Debug)]
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

pub type Rights = sys::mx_rights_t;

pub type Signals = sys::mx_signals_t;

pub use magenta_sys::{MX_OBJECT_SIGNAL_0, MX_OBJECT_SIGNAL_1, MX_OBJECT_SIGNAL_2,
    MX_OBJECT_SIGNAL_3, MX_OBJECT_SIGNAL_4, MX_OBJECT_SIGNAL_5};

#[repr(u32)]
pub enum ChannelOpts {
    Normal = 0,
    ReplyChannel = sys::MX_CHANNEL_CREATE_REPLY_CHANNEL,
}

#[repr(u32)]
pub enum WaitSetOpts {
    Default = 0,
}

#[repr(u32)]
pub enum VmoOpts {
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

#[repr(C)]
pub struct WaitItem<'a> {
    handle: HandleRef<'a>,
    waitfor: Signals,
    pending: Signals,
}


#[repr(u32)]
pub enum ClockId {
    Monotonic = 0,
}

pub fn time_get(clock_id: ClockId) -> Time {
    unsafe { sys::mx_time_get(clock_id as u32) }
}

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

pub trait HandleBase: Sized {
    fn get_ref(&self) -> HandleRef;

    fn raw_handle(&self) -> sys::mx_handle_t {
        self.get_ref().handle
    }

    fn duplicate(&self, rights: Rights) -> Result<Self, Status> {
        self.get_ref().duplicate(rights).map(|handle|
            Self::from_handle(handle))
    }

    fn wait(&self, signals: Signals, timeout: Time) -> Result<Signals, Status> {
        self.get_ref().wait(signals, timeout)
    }

    fn from_handle(handle: Handle) -> Self;

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
/// See: https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/handle_wait_many.md
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
    pub unsafe fn from_raw(raw: sys::mx_handle_t) -> Handle {
        Handle(raw)
    }
}

// Channels

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

    pub fn read(&self, opts: u32, buf: &mut MessageBuf) -> Result<(), Status> {
        unsafe {
            buf.reset_handles();
            let raw_handle = self.raw_handle();
            let mut num_bytes: u32 = size_to_u32_sat(buf.bytes.capacity());
            let mut num_handles: u32 = size_to_u32_sat(buf.handles.capacity());
            let mut status = sys::mx_channel_read(raw_handle, opts,
                buf.bytes.as_mut_ptr(), num_bytes, &mut num_bytes,
                buf.handles.as_mut_ptr(), num_handles, &mut num_handles);
            if status == sys::ERR_BUFFER_TOO_SMALL {
                ensure_capacity(&mut buf.bytes, num_bytes as usize);
                ensure_capacity(&mut buf.handles, num_handles as usize);
                num_bytes = size_to_u32_sat(buf.bytes.capacity());
                num_handles = size_to_u32_sat(buf.handles.capacity());
                status = sys::mx_channel_read(raw_handle, opts,
                    buf.bytes.as_mut_ptr(), num_bytes, &mut num_bytes,
                    buf.handles.as_mut_ptr(), num_handles, &mut num_handles);
            }
            into_result(status, || {
                buf.bytes.set_len(num_bytes as usize);
                buf.handles.set_len(num_handles as usize);
            })
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

    pub fn write(&self, bytes: &[u8], handles: &mut Vec<Handle>, opts: u32)
            -> Result<(), Status>
    {
        Self::write_raw(self.raw_handle(), bytes, handles, opts)
    }

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

#[derive(Default)]
pub struct MessageBuf {
    bytes: Vec<u8>,
    handles: Vec<sys::mx_handle_t>,
    unused_ix: usize,
}

impl MessageBuf {
    pub fn new() -> Self {
        Default::default()
    }

    pub fn bytes(&self) -> &[u8] {
        self.bytes.as_slice()
    }

    pub fn n_handles(&self) -> usize {
        self.handles.len() - self.unused_ix
    }

    pub fn handles(&mut self) -> HandleIter {
        HandleIter(self)
    }

    fn drop_handles(&mut self) {
        for &handle in &self.handles[self.unused_ix..] {
            handle_drop(handle);
        }
    }

    fn reset_handles(&mut self) {
        self.drop_handles();
        self.unused_ix = 0;
        self.handles.clear();
    }
}

impl Drop for MessageBuf {
    fn drop(&mut self) {
        self.drop_handles();
    }
}

pub struct HandleIter<'a>(&'a mut MessageBuf);

impl<'a> Iterator for HandleIter<'a> {
    type Item = Handle;

    fn next(&mut self) -> Option<Handle> {
        if self.0.unused_ix == self.0.handles.len() {
            return None
        }
        let handle = self.0.handles[self.0.unused_ix];
        self.0.unused_ix += 1;
        Some(Handle(handle))
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let size = self.0.n_handles();
        (size, Some(size))
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
    pub fn create(options: WaitSetOpts) -> Result<WaitSet, Status> {
        let mut handle = 0;
        let status = unsafe { sys::mx_waitset_create(options as u32, &mut handle) };
        into_result(status, ||
            WaitSet::from_handle(Handle(handle)))
    }

    pub fn add<H>(&self, handle: &H, cookie: u64, signals: Signals) -> Result<(), Status>
        where H: HandleBase
    {
        let status = unsafe {
            sys::mx_waitset_add(self.raw_handle(), cookie, handle.raw_handle(), signals)
        };
        into_result(status, || ())
    }

    pub fn remove(&self, cookie: u64) -> Result<(), Status> {
        let status = unsafe { sys::mx_waitset_remove(self.raw_handle(), cookie) };
        into_result(status, || ())
    }

    // The caller must make sure `results` has enough capacity. If the length is
    // equal to the capacity on return, that may be interpreted as a sign that
    // the capacity should be expanded.
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

pub struct WaitSetResult(sys::mx_waitset_result_t);

impl WaitSetResult {
    pub fn cookie(&self) -> u64 {
        self.0.cookie
    }

    pub fn status(&self) -> Status {
        Status::from_raw(self.0.status)
    }

    pub fn observed(&self) -> Signals {
        self.0.observed
    }
}

// Virtual Memory Objects

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
    pub fn create(size: u64, options: VmoOpts) -> Result<Vmo, Status> {
        let mut handle = 0;
        let status = unsafe { sys::mx_vmo_create(size, options as u32, &mut handle) };
        into_result(status, ||
            Vmo::from_handle(Handle(handle)))
    }

    pub fn read(&self, data: &mut [u8], offset: u64) -> Result<usize, Status> {
        unsafe {
            let mut actual = 0;
            let status = sys::mx_vmo_read(self.raw_handle(), data.as_mut_ptr(),
                offset, data.len(), &mut actual);
            into_result(status, || actual)
        }
    }

    pub fn write(&self, data: &[u8], offset: u64) -> Result<usize, Status> {
        unsafe {
            let mut actual = 0;
            let status = sys::mx_vmo_write(self.raw_handle(), data.as_ptr(),
                offset, data.len(), &mut actual);
            into_result(status, || actual)
        }
    }

    pub fn get_size(&self) -> Result<u64, Status> {
        let mut size = 0;
        let status = unsafe { sys::mx_vmo_get_size(self.raw_handle(), &mut size) };
        into_result(status, || size)
    }

    pub fn set_size(&self, size: u64) -> Result<(), Status> {
        let status = unsafe { sys::mx_vmo_set_size(self.raw_handle(), size) };
        into_result(status, || ())
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
