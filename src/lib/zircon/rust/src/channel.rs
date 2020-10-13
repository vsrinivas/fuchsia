// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon channel objects.

use crate::ok;
use crate::{
    size_to_u32_sat, usize_into_u32, AsHandleRef, Handle, HandleBased, HandleDisposition,
    HandleInfo, HandleOp, HandleRef, ObjectType, Peered, Rights, Status, Time,
};
use fuchsia_zircon_sys as sys;
use std::mem;

impl HandleDisposition<'_> {
    const fn invalid<'a>() -> HandleDisposition<'a> {
        HandleDisposition {
            handle_op: HandleOp::Move(Handle::invalid()),
            object_type: ObjectType::NONE,
            rights: Rights::NONE,
            result: Status::OK,
        }
    }
}

/// An object representing a Zircon
/// [channel](https://fuchsia.dev/fuchsia-src/concepts/objects/channel.md).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Channel(Handle);
impl_handle_based!(Channel);
impl Peered for Channel {}

impl Channel {
    /// Create a channel, resulting in a pair of `Channel` objects representing both
    /// sides of the channel. Messages written into one maybe read from the opposite.
    ///
    /// Wraps the
    /// [zx_channel_create](https://fuchsia.dev/fuchsia-src/reference/syscalls/channel_create.md)
    /// syscall.
    pub fn create() -> Result<(Channel, Channel), Status> {
        unsafe {
            let mut handle0 = 0;
            let mut handle1 = 0;
            let opts = 0;
            ok(sys::zx_channel_create(opts, &mut handle0, &mut handle1))?;
            Ok((Self::from(Handle::from_raw(handle0)), Self::from(Handle::from_raw(handle1))))
        }
    }

    /// Read a message from a channel. Wraps the
    /// [zx_channel_read](https://fuchsia.dev/fuchsia-src/reference/syscalls/channel_read.md)
    /// syscall.
    ///
    /// If the vectors lack the capacity to hold the pending message,
    /// returns an `Err` with the number of bytes and number of handles needed.
    /// Otherwise returns an `Ok` with the result as usual.
    pub fn read_raw(
        &self,
        bytes: &mut [u8],
        handles: &mut [Handle],
    ) -> Result<(Result<(), Status>, usize, usize), (usize, usize)> {
        let opts = 0;
        unsafe {
            let raw_handle = self.raw_handle();
            let mut actual_bytes = 0;
            let mut actual_handles = 0;
            let status = ok(sys::zx_channel_read(
                raw_handle,
                opts,
                bytes.as_mut_ptr(),
                handles.as_mut_ptr() as *mut _,
                bytes.len() as u32,
                handles.len() as u32,
                &mut actual_bytes,
                &mut actual_handles,
            ));
            if status == Err(Status::BUFFER_TOO_SMALL) {
                Err((actual_bytes as usize, actual_handles as usize))
            } else {
                Ok((status, actual_bytes as usize, actual_handles as usize))
            }
        }
    }

    /// Read a message from a channel.
    ///
    /// Note that this method can cause internal reallocations in the `MessageBuf`
    /// if it is lacks capacity to hold the full message. If such reallocations
    /// are not desirable, use `read_raw` instead.
    pub fn read(&self, buf: &mut MessageBuf) -> Result<(), Status> {
        let (bytes, handles) = buf.split_mut();
        self.read_split(bytes, handles)
    }

    /// Read a message from a channel into a separate byte vector and handle vector.
    ///
    /// Note that this method can cause internal reallocations in the `Vec`s
    /// if they lacks capacity to hold the full message. If such reallocations
    /// are not desirable, use `read_raw` instead.
    pub fn read_split(&self, bytes: &mut Vec<u8>, handles: &mut Vec<Handle>) -> Result<(), Status> {
        loop {
            unsafe {
                bytes.set_len(bytes.capacity());
                handles.set_len(handles.capacity());
            }
            match self.read_raw(bytes, handles) {
                Ok((result, num_bytes, num_handles)) => {
                    unsafe {
                        bytes.set_len(num_bytes);
                        handles.set_len(num_handles);
                    }
                    return result;
                }
                Err((num_bytes, num_handles)) => {
                    ensure_capacity(bytes, num_bytes);
                    ensure_capacity(handles, num_handles);
                }
            }
        }
    }

    /// Read a message from a channel.
    /// Wraps the [zx_channel_read_etc](https://fuchsia.dev/fuchsia-src/reference/syscalls/channel_read_etc.md)
    /// syscall.
    ///
    /// This differs from `read_raw` in that it returns extended information on
    /// the handles.
    ///
    /// If the vectors lack the capacity to hold the pending message,
    /// returns an `Err` with the number of bytes and number of handles needed.
    /// Otherwise returns an `Ok` with the result as usual.
    pub fn read_etc_raw(
        &self,
        bytes: &mut [u8],
        handle_infos: &mut [HandleInfo],
    ) -> Result<(Result<(), Status>, usize, usize), (usize, usize)> {
        let opts = 0;
        unsafe {
            let raw_handle = self.raw_handle();
            let mut zx_handle_infos: [std::mem::MaybeUninit<sys::zx_handle_info_t>;
                sys::ZX_CHANNEL_MAX_MSG_HANDLES as usize] =
                std::mem::MaybeUninit::uninit().assume_init();
            let mut actual_bytes = 0;
            let mut actual_handle_infos = 0;
            let status = ok(sys::zx_channel_read_etc(
                raw_handle,
                opts,
                bytes.as_mut_ptr(),
                zx_handle_infos.as_mut_ptr() as *mut sys::zx_handle_info_t,
                bytes.len() as u32,
                handle_infos.len() as u32,
                &mut actual_bytes,
                &mut actual_handle_infos,
            ));
            if status == Err(Status::BUFFER_TOO_SMALL) {
                Err((actual_bytes as usize, actual_handle_infos as usize))
            } else {
                Ok((
                    status.map(|()| {
                        for i in 0..handle_infos.len() {
                            handle_infos[i] =
                                HandleInfo::from_raw(zx_handle_infos[i].assume_init());
                        }
                    }),
                    actual_bytes as usize,
                    actual_handle_infos as usize,
                ))
            }
        }
    }

    /// Read a message from a channel.
    ///
    /// This differs from `read` in that it returns extended information on
    /// the handles.
    ///
    /// Note that this method can cause internal reallocations in the `MessageBufEtc`
    /// if it is lacks capacity to hold the full message. If such reallocations
    /// are not desirable, use `read_etc_raw` instead.
    pub fn read_etc(&self, buf: &mut MessageBufEtc) -> Result<(), Status> {
        let (bytes, handles) = buf.split_mut();
        self.read_etc_split(bytes, handles)
    }

    /// Read a message from a channel into a separate byte vector and handle vector.
    ///
    /// This differs from `read_split` in that it returns extended information on
    /// the handles.
    ///
    /// Note that this method can cause internal reallocations in the `Vec`s
    /// if they lacks capacity to hold the full message. If such reallocations
    /// are not desirable, use `read_raw` instead.
    pub fn read_etc_split(
        &self,
        bytes: &mut Vec<u8>,
        handle_infos: &mut Vec<HandleInfo>,
    ) -> Result<(), Status> {
        loop {
            unsafe {
                bytes.set_len(bytes.capacity());
                handle_infos.set_len(handle_infos.capacity());
            }
            match self.read_etc_raw(bytes, handle_infos) {
                Ok((result, num_bytes, num_handle_infos)) => {
                    unsafe {
                        bytes.set_len(num_bytes);
                        handle_infos.set_len(num_handle_infos);
                    }
                    return result;
                }
                Err((num_bytes, num_handle_infos)) => {
                    ensure_capacity(bytes, num_bytes);
                    ensure_capacity(handle_infos, num_handle_infos);
                }
            }
        }
    }

    /// Write a message to a channel. Wraps the
    /// [zx_channel_write](https://fuchsia.dev/fuchsia-src/reference/syscalls/channel_write.md)
    /// syscall.
    pub fn write(&self, bytes: &[u8], handles: &mut [Handle]) -> Result<(), Status> {
        let opts = 0;
        let n_bytes = usize_into_u32(bytes.len()).map_err(|_| Status::OUT_OF_RANGE)?;
        let n_handles = usize_into_u32(handles.len()).map_err(|_| Status::OUT_OF_RANGE)?;
        assert!(n_bytes <= sys::ZX_CHANNEL_MAX_MSG_BYTES);
        assert!(n_handles <= sys::ZX_CHANNEL_MAX_MSG_HANDLES);
        unsafe {
            let status = sys::zx_channel_write(
                self.raw_handle(),
                opts,
                bytes.as_ptr(),
                n_bytes,
                handles.as_ptr() as *const sys::zx_handle_t,
                n_handles,
            );
            // Handles are consumed by zx_channel_write so prevent the destructor from being called.
            for handle in handles {
                std::mem::forget(std::mem::replace(handle, Handle::invalid()));
            }
            ok(status)?;
            Ok(())
        }
    }

    /// Write a message to a channel. Wraps the
    /// [zx_channel_write_etc](https://fuchsia.dev/fuchsia-src/reference/syscalls/channel_write_etc.md)
    /// syscall.
    pub fn write_etc(
        &self,
        bytes: &[u8],
        handle_dispositions: &mut [HandleDisposition<'_>],
    ) -> Result<(), Status> {
        let opts = 0;
        let n_bytes = usize_into_u32(bytes.len()).map_err(|_| Status::OUT_OF_RANGE)?;
        let n_handle_dispositions =
            usize_into_u32(handle_dispositions.len()).map_err(|_| Status::OUT_OF_RANGE)?;
        assert!(n_bytes <= sys::ZX_CHANNEL_MAX_MSG_BYTES);
        assert!(n_handle_dispositions <= sys::ZX_CHANNEL_MAX_MSG_HANDLES);
        unsafe {
            let mut zx_handle_dispositions: [std::mem::MaybeUninit<sys::zx_handle_disposition_t>;
                sys::ZX_CHANNEL_MAX_MSG_HANDLES as usize] =
                std::mem::MaybeUninit::uninit().assume_init();
            for i in 0..n_handle_dispositions as usize {
                let handle_disposition =
                    std::mem::replace(&mut handle_dispositions[i], HandleDisposition::invalid());
                zx_handle_dispositions[i] =
                    std::mem::MaybeUninit::new(handle_disposition.into_raw());
            }
            let status = sys::zx_channel_write_etc(
                self.raw_handle(),
                opts,
                bytes.as_ptr(),
                n_bytes,
                zx_handle_dispositions.as_mut_ptr() as *mut sys::zx_handle_disposition_t,
                n_handle_dispositions,
            );
            ok(status)?;
            Ok(())
        }
    }

    /// Send a message consisting of the given bytes and handles to a channel and await a reply.
    ///
    /// The first four bytes of the written and read back messages are treated as a transaction ID
    /// of type `zx_txid_t`. The kernel generates a txid for the written message, replacing that
    /// part of the message as read from userspace. In other words, the first four bytes of
    /// `bytes` will be ignored, and the first four bytes of the response will contain a
    /// kernel-generated txid.
    ///
    /// Wraps the
    /// [zx_channel_call](https://fuchsia.dev/fuchsia-src/reference/syscalls/channel_call.md)
    /// syscall.
    ///
    /// Note that unlike [`read`][read], the caller must ensure that the MessageBuf has enough
    /// capacity for the bytes and handles which will be received, as replies which are too large
    /// are discarded.
    ///
    /// On failure returns the both the main and read status.
    ///
    /// [read]: struct.Channel.html#method.read
    pub fn call(
        &self,
        timeout: Time,
        bytes: &[u8],
        handles: &mut [Handle],
        buf: &mut MessageBuf,
    ) -> Result<(), Status> {
        let write_num_bytes = usize_into_u32(bytes.len()).map_err(|_| Status::OUT_OF_RANGE)?;
        let write_num_handles = usize_into_u32(handles.len()).map_err(|_| Status::OUT_OF_RANGE)?;
        assert!(write_num_bytes <= sys::ZX_CHANNEL_MAX_MSG_BYTES);
        assert!(write_num_handles <= sys::ZX_CHANNEL_MAX_MSG_HANDLES);
        buf.clear();
        let read_num_bytes: u32 = size_to_u32_sat(buf.bytes.capacity());
        let read_num_handles: u32 = size_to_u32_sat(buf.handles.capacity());
        let args = sys::zx_channel_call_args_t {
            wr_bytes: bytes.as_ptr(),
            wr_handles: handles.as_ptr() as *const sys::zx_handle_t,
            rd_bytes: buf.bytes.as_mut_ptr(),
            rd_handles: buf.handles.as_mut_ptr() as *mut _,
            wr_num_bytes: write_num_bytes,
            wr_num_handles: write_num_handles,
            rd_num_bytes: read_num_bytes,
            rd_num_handles: read_num_handles,
        };
        let mut actual_read_bytes: u32 = 0;
        let mut actual_read_handles: u32 = 0;
        let options = 0;
        let status = unsafe {
            Status::from_raw(sys::zx_channel_call(
                self.raw_handle(),
                options,
                timeout.into_nanos(),
                &args,
                &mut actual_read_bytes,
                &mut actual_read_handles,
            ))
        };
        unsafe {
            // Outgoing handles are consumed by zx_channel_call so prevent the destructor from being called.
            for handle in handles {
                std::mem::forget(std::mem::replace(handle, Handle::invalid()));
            }
            buf.bytes.set_len(actual_read_bytes as usize);
            buf.handles.set_len(actual_read_handles as usize);
        }
        if Status::OK == status {
            Ok(())
        } else {
            Err(status)
        }
    }

    /// Send a message consisting of the given bytes and handles to a channel and await a reply.
    ///
    /// The first four bytes of the written and read back messages are treated as a transaction ID
    /// of type `zx_txid_t`. The kernel generates a txid for the written message, replacing that
    /// part of the message as read from userspace. In other words, the first four bytes of
    /// `bytes` will be ignored, and the first four bytes of the response will contain a
    /// kernel-generated txid.
    ///
    /// This differs from `call`, in that it uses extended handle info.
    ///
    /// Wraps the
    /// [zx_channel_call_etc](https://fuchsia.dev/fuchsia-src/reference/syscalls/channel_call_etc.md)
    /// syscall.
    ///
    /// Note that unlike [`read_etc`][read_etc], the caller must ensure that the MessageBufEtc
    /// has enough capacity for the bytes and handles which will be received, as replies which are
    /// too large are discarded.
    ///
    /// On failure returns the both the main and read status.
    ///
    /// [read_etc]: struct.Channel.html#method.read_etc
    pub fn call_etc(
        &self,
        timeout: Time,
        bytes: &[u8],
        handle_dispositions: &mut [HandleDisposition<'_>],
        buf: &mut MessageBufEtc,
    ) -> Result<(), Status> {
        let write_num_bytes = usize_into_u32(bytes.len()).map_err(|_| Status::OUT_OF_RANGE)?;
        let write_num_handle_dispositions =
            usize_into_u32(handle_dispositions.len()).map_err(|_| Status::OUT_OF_RANGE)?;
        assert!(write_num_bytes <= sys::ZX_CHANNEL_MAX_MSG_BYTES);
        assert!(write_num_handle_dispositions <= sys::ZX_CHANNEL_MAX_MSG_HANDLES);
        let mut zx_handle_dispositions: [std::mem::MaybeUninit<sys::zx_handle_disposition_t>;
            sys::ZX_CHANNEL_MAX_MSG_HANDLES as usize] =
            unsafe { std::mem::MaybeUninit::uninit().assume_init() };
        for i in 0..write_num_handle_dispositions as usize {
            unsafe {
                let handle_disposition =
                    std::mem::replace(&mut handle_dispositions[i], HandleDisposition::invalid());
                zx_handle_dispositions[i] =
                    std::mem::MaybeUninit::new(handle_disposition.into_raw());
            }
        }
        buf.clear();
        let read_num_bytes: u32 = size_to_u32_sat(buf.bytes.capacity());
        let read_num_handle_infos: u32 = size_to_u32_sat(buf.handle_infos.capacity());
        let mut zx_handle_infos: [std::mem::MaybeUninit<sys::zx_handle_info_t>;
            sys::ZX_CHANNEL_MAX_MSG_HANDLES as usize] =
            unsafe { std::mem::MaybeUninit::uninit().assume_init() };
        let mut args = sys::zx_channel_call_etc_args_t {
            wr_bytes: bytes.as_ptr(),
            wr_handles: zx_handle_dispositions.as_mut_ptr() as *mut sys::zx_handle_disposition_t,
            rd_bytes: buf.bytes.as_mut_ptr(),
            rd_handles: zx_handle_infos.as_mut_ptr() as *mut sys::zx_handle_info_t,
            wr_num_bytes: write_num_bytes,
            wr_num_handles: write_num_handle_dispositions,
            rd_num_bytes: read_num_bytes,
            rd_num_handles: read_num_handle_infos,
        };
        let mut actual_read_bytes: u32 = 0;
        let mut actual_read_handle_infos: u32 = 0;
        let options = 0;
        let status = unsafe {
            Status::from_raw(sys::zx_channel_call_etc(
                self.raw_handle(),
                options,
                timeout.into_nanos(),
                &mut args,
                &mut actual_read_bytes,
                &mut actual_read_handle_infos,
            ))
        };
        unsafe {
            buf.ensure_capacity_handle_infos(actual_read_handle_infos as usize);
            for i in 0..actual_read_handle_infos as usize {
                buf.handle_infos.push(HandleInfo::from_raw(zx_handle_infos[i].assume_init()));
            }
            buf.bytes.set_len(actual_read_bytes as usize);
        }
        if Status::OK == status {
            Ok(())
        } else {
            Err(status)
        }
    }
}

#[test]
pub fn test_handle_repr() {
    assert_eq!(::std::mem::size_of::<sys::zx_handle_t>(), 4);
    assert_eq!(::std::mem::size_of::<Handle>(), 4);
    assert_eq!(::std::mem::align_of::<sys::zx_handle_t>(), ::std::mem::align_of::<Handle>());

    // This test asserts that repr(transparent) still works for Handle -> zx_handle_t

    let n: Vec<sys::zx_handle_t> = vec![0, 100, 2 << 32 - 1];
    let v: Vec<Handle> = n.iter().map(|h| unsafe { Handle::from_raw(*h) }).collect();

    for (handle, raw) in v.iter().zip(n.iter()) {
        unsafe {
            assert_eq!(
                *(handle as *const _ as *const [u8; 4]),
                *(raw as *const _ as *const [u8; 4])
            );
        }
    }

    for h in v.into_iter() {
        ::std::mem::forget(h);
    }
}

impl AsRef<Channel> for Channel {
    fn as_ref(&self) -> &Self {
        &self
    }
}

/// A buffer for _receiving_ messages from a channel.
///
/// A `MessageBuf` is essentially a byte buffer and a vector of
/// handles, but move semantics for "taking" handles requires special handling.
///
/// Note that for sending messages to a channel, the caller manages the buffers,
/// using a plain byte slice and `Vec<Handle>`.
#[derive(Debug, Default)]
pub struct MessageBuf {
    bytes: Vec<u8>,
    handles: Vec<Handle>,
}

impl MessageBuf {
    /// Create a new, empty, message buffer.
    pub fn new() -> Self {
        Default::default()
    }

    /// Create a new non-empty message buffer.
    pub fn new_with(v: Vec<u8>, h: Vec<Handle>) -> Self {
        Self { bytes: v, handles: h }
    }

    /// Splits apart the message buf into a vector of bytes and a vector of handles.
    pub fn split_mut(&mut self) -> (&mut Vec<u8>, &mut Vec<Handle>) {
        (&mut self.bytes, &mut self.handles)
    }

    /// Splits apart the message buf into a vector of bytes and a vector of handles.
    pub fn split(self) -> (Vec<u8>, Vec<Handle>) {
        (self.bytes, self.handles)
    }

    /// Ensure that the buffer has the capacity to hold at least `n_bytes` bytes.
    pub fn ensure_capacity_bytes(&mut self, n_bytes: usize) {
        ensure_capacity(&mut self.bytes, n_bytes);
    }

    /// Ensure that the buffer has the capacity to hold at least `n_handles` handles.
    pub fn ensure_capacity_handles(&mut self, n_handles: usize) {
        ensure_capacity(&mut self.handles, n_handles);
    }

    /// Ensure that at least n_bytes bytes are initialized (0 fill).
    pub fn ensure_initialized_bytes(&mut self, n_bytes: usize) {
        if n_bytes <= self.bytes.len() {
            return;
        }
        self.bytes.resize(n_bytes, 0);
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
        self.handles.get_mut(index).and_then(|handle| {
            if handle.is_invalid() {
                None
            } else {
                Some(mem::replace(handle, Handle::invalid()))
            }
        })
    }

    /// Clear the bytes and handles contained in the buf. This will drop any
    /// contained handles, resulting in their resources being freed.
    pub fn clear(&mut self) {
        self.bytes.clear();
        self.handles.clear();
    }
}

/// A buffer for _receiving_ messages from a channel.
///
/// This differs from `MessageBuf` in that it holds `HandleInfo` with
/// extended handle information.
///
/// A `MessageBufEtc` is essentially a byte buffer and a vector of handle
/// infos, but move semantics for "taking" handles requires special handling.
///
/// Note that for sending messages to a channel, the caller manages the buffers,
/// using a plain byte slice and `Vec<HandleDisposition>`.
#[derive(Debug, Default)]
pub struct MessageBufEtc {
    bytes: Vec<u8>,
    handle_infos: Vec<HandleInfo>,
}

impl MessageBufEtc {
    /// Create a new, empty, message buffer.
    pub fn new() -> Self {
        Default::default()
    }

    /// Create a new non-empty message buffer.
    pub fn new_with(v: Vec<u8>, h: Vec<HandleInfo>) -> Self {
        Self { bytes: v, handle_infos: h }
    }

    /// Splits apart the message buf into a vector of bytes and a vector of handle infos.
    pub fn split_mut(&mut self) -> (&mut Vec<u8>, &mut Vec<HandleInfo>) {
        (&mut self.bytes, &mut self.handle_infos)
    }

    /// Splits apart the message buf into a vector of bytes and a vector of handle infos.
    pub fn split(self) -> (Vec<u8>, Vec<HandleInfo>) {
        (self.bytes, self.handle_infos)
    }

    /// Ensure that the buffer has the capacity to hold at least `n_bytes` bytes.
    pub fn ensure_capacity_bytes(&mut self, n_bytes: usize) {
        ensure_capacity(&mut self.bytes, n_bytes);
    }

    /// Ensure that the buffer has the capacity to hold at least `n_handles` handle infos.
    pub fn ensure_capacity_handle_infos(&mut self, n_handle_infos: usize) {
        ensure_capacity(&mut self.handle_infos, n_handle_infos);
    }

    /// Ensure that at least n_bytes bytes are initialized (0 fill).
    pub fn ensure_initialized_bytes(&mut self, n_bytes: usize) {
        if n_bytes <= self.bytes.len() {
            return;
        }
        self.bytes.resize(n_bytes, 0);
    }

    /// Get a reference to the bytes of the message buffer, as a `&[u8]` slice.
    pub fn bytes(&self) -> &[u8] {
        self.bytes.as_slice()
    }

    /// The number of handles in the message buffer. Note this counts the number
    /// available when the message was received; `take_handle` does not affect
    /// the count.
    pub fn n_handle_infos(&self) -> usize {
        self.handle_infos.len()
    }

    /// Take the handle at the specified index from the message buffer. If the
    /// method is called again with the same index, it will return `None`, as
    /// will happen if the index exceeds the number of handles available.
    pub fn take_handle_info(&mut self, index: usize) -> Option<HandleInfo> {
        self.handle_infos.get_mut(index).and_then(|handle_info| {
            if handle_info.handle.is_invalid() {
                None
            } else {
                Some(mem::replace(
                    handle_info,
                    HandleInfo {
                        handle: Handle::invalid(),
                        object_type: ObjectType::NONE,
                        rights: Rights::NONE,
                    },
                ))
            }
        })
    }

    /// Clear the bytes and handles contained in the buf. This will drop any
    /// contained handles, resulting in their resources being freed.
    pub fn clear(&mut self) {
        self.bytes.clear();
        self.handle_infos.clear();
    }
}

fn ensure_capacity<T>(vec: &mut Vec<T>, size: usize) {
    let len = vec.len();
    if size > len {
        vec.reserve(size - len);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{DurationNum, HandleOp, Port, Rights, Signals, Vmo};
    use std::thread;

    #[test]
    fn channel_basic() {
        let (p1, p2) = Channel::create().unwrap();

        let mut empty = vec![];
        assert!(p1.write(b"hello", &mut empty).is_ok());

        let mut buf = MessageBuf::new();
        assert!(p2.read(&mut buf).is_ok());
        assert_eq!(buf.bytes(), b"hello");
    }

    #[test]
    fn channel_basic_etc() {
        let (p1, p2) = Channel::create().unwrap();

        let mut empty = vec![];
        assert!(p1.write_etc(b"hello", &mut empty).is_ok());

        let mut buf = MessageBufEtc::new();
        assert!(p2.read_etc(&mut buf).is_ok());
        assert_eq!(buf.bytes(), b"hello");
    }

    #[test]
    fn channel_basic_etc_with_handle_move() {
        let (p1, p2) = Channel::create().unwrap();

        let mut handles = vec![HandleDisposition {
            handle_op: HandleOp::Move(Port::create().unwrap().into()),
            rights: Rights::TRANSFER,
            object_type: ObjectType::PORT,
            result: Status::OK,
        }];
        match p1.write_etc(b"", &mut handles) {
            Err(err) => {
                panic!("error: {}", err);
            }
            _ => {}
        }

        let mut buf = MessageBufEtc::new();
        assert!(p2.read_etc(&mut buf).is_ok());
        assert_eq!(buf.bytes(), b"");
        assert_eq!(buf.n_handle_infos(), 1);
        let out_handles = buf.handle_infos;
        assert_eq!(out_handles.len(), 1);
        assert_ne!(out_handles[0].handle, Handle::invalid());
        assert_eq!(out_handles[0].rights, Rights::TRANSFER);
        assert_eq!(out_handles[0].object_type, ObjectType::PORT);
    }

    #[test]
    fn channel_basic_etc_with_handle_duplicate() {
        let (p1, p2) = Channel::create().unwrap();

        let port = Port::create().unwrap();
        let mut handles = vec![HandleDisposition {
            handle_op: HandleOp::Duplicate(port.as_handle_ref()),
            rights: Rights::SAME_RIGHTS,
            object_type: ObjectType::NONE,
            result: Status::OK,
        }];
        p1.write_etc(b"", &mut handles).unwrap();

        let orig_port_info = port.basic_info().unwrap();
        let mut buf = MessageBufEtc::new();
        assert!(p2.read_etc(&mut buf).is_ok());
        assert_eq!(buf.bytes(), b"");
        assert_eq!(buf.n_handle_infos(), 1);
        let out_handles = buf.handle_infos;
        assert_eq!(out_handles.len(), 1);
        assert_ne!(out_handles[0].handle.raw_handle(), 0);
        assert_ne!(out_handles[0].handle.raw_handle(), port.raw_handle());
        assert_eq!(out_handles[0].rights, orig_port_info.rights);
        assert_eq!(out_handles[0].object_type, ObjectType::PORT);
    }

    #[test]
    fn channel_read_raw_too_small() {
        let (p1, p2) = Channel::create().unwrap();

        let mut empty = vec![];
        assert!(p1.write(b"hello", &mut empty).is_ok());

        let result = p2.read_raw(&mut vec![], &mut vec![]);
        assert_eq!(result, Err((5, 0)));
    }

    #[test]
    fn channel_read_etc_raw_too_small() {
        let (p1, p2) = Channel::create().unwrap();

        let mut empty = vec![];
        assert!(p1.write_etc(b"hello", &mut empty).is_ok());

        let result = p2.read_etc_raw(&mut vec![], &mut vec![]);
        assert_eq!(result, Err((5, 0)));
    }

    #[test]
    fn channel_send_handle() {
        let hello_length: usize = 5;

        // Create a pair of channels and a virtual memory object.
        let (p1, p2) = Channel::create().unwrap();
        let vmo = Vmo::create(hello_length as u64).unwrap();

        // Duplicate VMO handle and send it down the channel.
        let duplicate_vmo_handle = vmo.duplicate_handle(Rights::SAME_RIGHTS).unwrap().into();
        let mut handles_to_send: Vec<Handle> = vec![duplicate_vmo_handle];
        assert!(p1.write(b"", &mut handles_to_send).is_ok());
        // The handle vector should only contain invalid handles.
        for handle in handles_to_send {
            assert!(handle.is_invalid());
        }

        // Read the handle from the receiving channel.
        let mut buf = MessageBuf::new();
        assert!(p2.read(&mut buf).is_ok());
        assert_eq!(buf.n_handles(), 1);
        // Take the handle from the buffer.
        let received_handle = buf.take_handle(0).unwrap();
        // Should not affect number of handles.
        assert_eq!(buf.n_handles(), 1);
        // Trying to take it again should fail.
        assert!(buf.take_handle(0).is_none());

        // Now to test that we got the right handle, try writing something to it...
        let received_vmo = Vmo::from(received_handle);
        assert!(received_vmo.write(b"hello", 0).is_ok());

        // ... and reading it back from the original VMO.
        let mut read_vec = vec![0; hello_length];
        assert!(vmo.read(&mut read_vec, 0).is_ok());
        assert_eq!(read_vec, b"hello");
    }

    #[test]
    fn channel_call_timeout() {
        let ten_ms = 10.millis();

        // Create a pair of channels and a virtual memory object.
        let (p1, p2) = Channel::create().unwrap();
        let vmo = Vmo::create(0 as u64).unwrap();

        // Duplicate VMO handle and send it along with the call.
        let duplicate_vmo_handle = vmo.duplicate_handle(Rights::SAME_RIGHTS).unwrap().into();
        let mut handles_to_send: Vec<Handle> = vec![duplicate_vmo_handle];
        let mut buf = MessageBuf::new();
        assert_eq!(
            p1.call(Time::after(ten_ms), b"0000call", &mut handles_to_send, &mut buf),
            Err(Status::TIMED_OUT)
        );
        // Despite not getting a response, the handles were sent so the handle slice
        // should only contain invalid handles.
        for handle in handles_to_send {
            assert!(handle.is_invalid());
        }

        // Should be able to read call even though it timed out waiting for a response.
        let mut buf = MessageBuf::new();
        assert!(p2.read(&mut buf).is_ok());
        assert_eq!(&buf.bytes()[4..], b"call");
        assert_eq!(buf.n_handles(), 1);
    }

    #[test]
    fn channel_call_etc_timeout() {
        let ten_ms = 10.millis();

        // Create a pair of channels and a virtual memory object.
        let (p1, p2) = Channel::create().unwrap();

        // Duplicate VMO handle and send it along with the call.
        let mut empty: Vec<HandleDisposition<'_>> = vec![];
        let mut buf = MessageBufEtc::new();
        assert_eq!(
            p1.call_etc(Time::after(ten_ms), b"0000call", &mut empty, &mut buf),
            Err(Status::TIMED_OUT)
        );

        // Should be able to read call even though it timed out waiting for a response.
        let mut buf = MessageBuf::new();
        assert!(p2.read(&mut buf).is_ok());
        assert_eq!(&buf.bytes()[4..], b"call");
        assert_eq!(buf.n_handles(), 0);
    }

    #[test]
    fn channel_call() {
        // Create a pair of channels
        let (p1, p2) = Channel::create().unwrap();

        // create an mpsc channel for communicating the call data for later assertion
        let (tx, rx) = ::std::sync::mpsc::channel();

        // Start a new thread to respond to the call.
        thread::spawn(move || {
            let mut buf = MessageBuf::new();
            // if either the read or the write fail, this thread will panic,
            // resulting in tx being dropped, which will be noticed by the rx.
            p2.wait_handle(Signals::CHANNEL_READABLE, Time::after(1.seconds()))
                .expect("callee wait error");
            p2.read(&mut buf).expect("callee read error");

            let (bytes, handles) = buf.split_mut();
            tx.send(bytes.clone()).expect("callee mpsc send error");
            assert_eq!(handles.len(), 0);

            bytes.truncate(4); // Drop the received message, leaving only the txid
            bytes.extend_from_slice(b"response");

            p2.write(bytes, handles).expect("callee write error");
        });

        // Make the call.
        let mut buf = MessageBuf::new();
        buf.ensure_capacity_bytes(12);
        // NOTE(raggi): CQ has been seeing some long stalls from channel call,
        // and it's as yet unclear why. The timeout here has been made much
        // larger in order to avoid that, as the issues are not issues with this
        // crate's concerns. The timeout is here just to prevent the tests from
        // stalling forever if a developer makes a mistake locally in this
        // crate. Tests of Zircon behavior or virtualization behavior should be
        // covered elsewhere. See fxbug.dev/31235.
        p1.call(Time::after(30.seconds()), b"txidcall", &mut vec![], &mut buf)
            .expect("channel call error");
        assert_eq!(&buf.bytes()[4..], b"response");
        assert_eq!(buf.n_handles(), 0);

        let sbuf = rx.recv().expect("mpsc channel recv error");
        assert_eq!(&sbuf[4..], b"call");
    }

    #[test]
    fn channel_call_etc() {
        // Create a pair of channels
        let (p1, p2) = Channel::create().unwrap();

        // create an mpsc channel for communicating the call data for later assertion
        let (tx, rx) = ::std::sync::mpsc::channel();

        // Start a new thread to respond to the call.
        thread::spawn(move || {
            let mut buf = MessageBuf::new();
            // if either the read or the write fail, this thread will panic,
            // resulting in tx being dropped, which will be noticed by the rx.
            p2.wait_handle(Signals::CHANNEL_READABLE, Time::after(1.seconds()))
                .expect("callee wait error");
            p2.read(&mut buf).expect("callee read error");

            let (bytes, handles) = buf.split_mut();
            tx.send(bytes.clone()).expect("callee mpsc send error");
            assert_eq!(handles.len(), 1);

            bytes.truncate(4); // Drop the received message, leaving only the txid
            bytes.extend_from_slice(b"response");

            p2.write(bytes, handles).expect("callee write error");
        });

        // Make the call.
        let mut buf = MessageBufEtc::new();
        buf.ensure_capacity_bytes(12);
        buf.ensure_capacity_handle_infos(1);
        let mut handle_dispositions = [HandleDisposition {
            handle_op: HandleOp::Move(Port::create().unwrap().into()),
            object_type: ObjectType::PORT,
            rights: Rights::TRANSFER,
            result: Status::OK,
        }];
        // NOTE(raggi): CQ has been seeing some long stalls from channel call,
        // and it's as yet unclear why. The timeout here has been made much
        // larger in order to avoid that, as the issues are not issues with this
        // crate's concerns. The timeout is here just to prevent the tests from
        // stalling forever if a developer makes a mistake locally in this
        // crate. Tests of Zircon behavior or virtualization behavior should be
        // covered elsewhere. See fxbug.dev/31235.
        p1.call_etc(Time::after(30.seconds()), b"txidcall", &mut handle_dispositions, &mut buf)
            .expect("channel call error");
        assert_eq!(&buf.bytes()[4..], b"response");
        assert_eq!(buf.n_handle_infos(), 1);
        assert_ne!(buf.handle_infos[0].handle.raw_handle(), 0);
        assert_eq!(buf.handle_infos[0].object_type, ObjectType::PORT);
        assert_eq!(buf.handle_infos[0].rights, Rights::TRANSFER);

        let sbuf = rx.recv().expect("mpsc channel recv error");
        assert_eq!(&sbuf[4..], b"call");
    }
}
