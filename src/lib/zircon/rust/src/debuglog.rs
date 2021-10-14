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
    pub fn read(&self) -> Result<sys::zx_log_record_t, Status> {
        let mut record = sys::zx_log_record_t::default();
        // zx_debuglog_read options appear to be unused.
        // zx_debuglog_read returns either an error status or, on success, the actual size of bytes
        // read into the buffer.
        let raw_status = unsafe {
            sys::zx_debuglog_read(
                self.raw_handle(),
                0,
                &mut record as *mut _ as *mut u8,
                std::mem::size_of_val(&record),
            )
        };
        // On error, zx_debuglog_read returns a negative value. All other values indicate success.
        if raw_status < 0 {
            Err(Status::from_raw(raw_status))
        } else {
            Ok(record)
        }
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
        for _ in 0..10000 {
            match debuglog.read() {
                Ok(record) => {
                    let len = record.datalen as usize;
                    let log = &record.data[0..len];
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
        let resource = Resource::from(Handle::invalid());
        let debuglog = DebugLog::create(&resource, DebugLogOpts::empty()).unwrap();
        assert!(debuglog.read().err() == Some(Status::ACCESS_DENIED));
    }

    #[test]
    fn write_and_read_back() {
        let mut bytes = [0; 8];
        cprng_draw(&mut bytes);
        let rand = u64::from_ne_bytes(bytes);
        let message = format!("log message {}", rand);

        let resource = Resource::from(Handle::invalid());
        let debuglog = DebugLog::create(&resource, DebugLogOpts::empty()).unwrap();
        debuglog.write(message.as_bytes()).unwrap();
        expect_message_in_debuglog(message);
    }
}
