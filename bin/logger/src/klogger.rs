// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Read kernel logs, convert them to LogMessages and serve them.

use async;
use byteorder::{ByteOrder, LittleEndian};
use fidl_fuchsia_logger::{self, LogMessage};
use futures::future::{loop_fn, Loop};
use futures::FutureExt;
use logger;
use zx::sys::zx_handle_t;
use zx::{self, AsHandleRef, Signals};

const ZX_LOG_FLAG_READABLE: u32 = 0x40000000;
const ZX_LOG_RECORD_MAX: u32 = 256;

pub fn add_listener<F>(callback: F) -> Result<(), zx::Status>
where
    F: 'static + Send + Fn(LogMessage, usize) + Sync,
{
    let mut handle: zx_handle_t = 0;
    let handle_ptr = &mut handle as *mut _ as *mut u32;

    unsafe {
        zx::ok(zx::sys::zx_debuglog_create(zx::sys::ZX_HANDLE_INVALID, ZX_LOG_FLAG_READABLE, handle_ptr))?;
    };
    let h = unsafe { zx::Handle::from_raw(handle) };

    let f = loop_fn((callback, h), |(callback, handle)| {
        // TODO: change OnSignals to wrap this so that is is not created again and again.
        async::OnSignals::new(&handle, Signals::LOG_READABLE).and_then(|_| {
            loop {
                let mut buf = [0; ZX_LOG_RECORD_MAX as usize];
                let status = unsafe {
                    zx::sys::zx_log_read(
                        handle.raw_handle(),
                        ZX_LOG_RECORD_MAX,
                        buf.as_mut_ptr(),
                        0,
                    )
                };

                if status == zx::sys::ZX_ERR_SHOULD_WAIT {
                    return Ok(Loop::Continue((callback, handle)));
                }
                if status < 0 {
                    return Err(zx::Status::from_raw(status));
                }
                let mut l = LogMessage {
                    time: LittleEndian::read_u64(&buf[8..16]),
                    pid: LittleEndian::read_u64(&buf[16..24]),
                    tid: LittleEndian::read_u64(&buf[24..32]),
                    severity: fidl_fuchsia_logger::LogLevelFilter::Info as i32,
                    dropped_logs: 0,
                    tags: vec!["klog".to_string()],
                    msg: String::new(),
                };
                let data_len = LittleEndian::read_u16(&buf[4..8]) as usize;
                l.msg = match String::from_utf8(buf[32..(32 + data_len)].to_vec()) {
                    Err(e) => {
                        eprintln!("logger: invalid log record: {:?}", e);
                        continue;
                    }
                    Ok(s) => s,
                };
                if l.msg.len() > 0 && l.msg.as_bytes()[l.msg.len() - 1] == '\n' as u8 {
                    l.msg.pop();
                }
                let size = logger::METADATA_SIZE + 5/*tag*/ + l.msg.len() + 1;
                callback(l, size);
            }
        })
    });

    async::spawn(f.recover(|e| {
        eprintln!(
            "logger: not able to apply listener to kernel logs, failed: {:?}",
            e
        )
    }));

    Ok(())
}
