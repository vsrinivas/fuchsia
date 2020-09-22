// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon resources.

use crate::ok;
use crate::{AsHandleRef, Handle, HandleBased, HandleRef, Resource, Status};
use bitflags::bitflags;
use fuchsia_zircon_sys as sys;

/// An object representing a Zircon 'debuglog' object.
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct DebugLog(Handle);
impl_handle_based!(DebugLog);

bitflags! {
    #[repr(transparent)]
    pub struct DebugLogOpts: u32 {
        const READABLE = sys::ZX_LOG_FLAG_READABLE;
    }
}

impl DebugLog {
    /// Create a debug log object that allows access to read from and write to the kernel debug
    /// logging facility.
    ///
    /// Wraps the
    /// [zx_debuglog_create]((https://fuchsia.dev/fuchsia-src/reference/syscalls/debuglog_create.md)
    /// syscall.
    pub fn create(resource: &Resource, opts: DebugLogOpts) -> Result<DebugLog, Status> {
        let mut handle = 0;
        let status =
            unsafe { sys::zx_debuglog_create(resource.raw_handle(), opts.bits(), &mut handle) };
        ok(status)?;
        unsafe { Ok(DebugLog::from(Handle::from_raw(handle))) }
    }

    /// Write a message to the kernel debug log.
    ///
    /// Wraps the
    /// [zx_debuglog_write]((https://fuchsia.dev/fuchsia-src/reference/syscalls/debuglog_write.md)
    /// syscall.
    pub fn write(&self, message: &[u8]) -> Result<(), Status> {
        // TODO(fxbug.dev/32998): Discussion ongoing over whether debuglog levels are supported, so no
        // options parameter for now.
        let status = unsafe {
            sys::zx_debuglog_write(self.raw_handle(), 0, message.as_ptr(), message.len())
        };
        ok(status)
    }

    /// Read a single log record from the kernel debug log.
    ///
    /// The DebugLog object must have been created with DebugLogOpts::READABLE, or this will return
    /// an error.
    ///
    /// Wraps the
    /// [zx_debuglog_read]((https://fuchsia.dev/fuchsia-src/reference/syscalls/debuglog_read.md)
    /// syscall.
    // TODO(fxbug.dev/32998): Return a safe wrapper type for zx_log_record_t rather than raw bytes
    // depending on resolution.
    pub fn read(&self, record: &mut Vec<u8>) -> Result<(), Status> {
        use std::convert::TryInto;

        let mut buf = [0; sys::ZX_LOG_RECORD_MAX];

        // zx_debuglog_read options appear to be unused.
        // zx_debuglog_read returns either an error status or, on success, the actual size of bytes
        // read into the buffer.
        let status_or_actual =
            unsafe { sys::zx_debuglog_read(self.raw_handle(), 0, buf.as_mut_ptr(), buf.len()) };
        let actual = status_or_actual
            .try_into()
            .map_err(|std::num::TryFromIntError { .. }| Status::from_raw(status_or_actual))?;

        record.clear();
        record.extend_from_slice(&buf[0..actual]);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{cprng_draw, Signals, Time};

    // expect_message_in_debuglog will read the last 10000 messages in zircon's debuglog, looking
    // for a message that equals `sent_msg`. If found, the function returns. If the first 10,000
    // messages doesn't contain `sent_msg`, it will panic.
    fn expect_message_in_debuglog(sent_msg: String) {
        let resource = Resource::from(Handle::invalid());
        let debuglog = DebugLog::create(&resource, DebugLogOpts::READABLE).unwrap();
        let mut record = Vec::with_capacity(sys::ZX_LOG_RECORD_MAX);
        for _ in 0..10000 {
            match debuglog.read(&mut record) {
                Ok(()) => {
                    // TODO(fxbug.dev/32998): Manually unpack log record until DebugLog::read returns
                    // an wrapper type.
                    let mut len_bytes = [0; 2];
                    len_bytes.copy_from_slice(&record[4..6]);
                    let data_len = u16::from_le_bytes(len_bytes) as usize;
                    let log = &record[32..(32 + data_len)];
                    if log == sent_msg.as_bytes() {
                        // We found our log!
                        return;
                    }
                }
                Err(status) if status == Status::SHOULD_WAIT => {
                    debuglog
                        .wait_handle(Signals::LOG_READABLE, Time::INFINITE)
                        .expect("Failed to wait for log readable");
                    continue;
                }
                Err(status) => {
                    panic!("Unexpected error from zx_debuglog_read: {}", status);
                }
            }
        }
        panic!("first 10000 log messages didn't include the one we sent!");
    }

    #[test]
    fn read_from_nonreadable() {
        let mut data = vec![];
        let resource = Resource::from(Handle::invalid());
        let debuglog = DebugLog::create(&resource, DebugLogOpts::empty()).unwrap();
        assert!(debuglog.read(&mut data).err() == Some(Status::ACCESS_DENIED));
    }

    #[test]
    fn write_and_read_back() {
        let mut bytes = [0; 8];
        cprng_draw(&mut bytes).unwrap();
        let rand = u64::from_ne_bytes(bytes);
        let message = format!("log message {}", rand);

        let resource = Resource::from(Handle::invalid());
        let debuglog = DebugLog::create(&resource, DebugLogOpts::empty()).unwrap();
        debuglog.write(message.as_bytes()).unwrap();
        expect_message_in_debuglog(message);
    }
}
