// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Read kernel logs, convert them to LogMessages and serve them.

use crate::logger;
use byteorder::{ByteOrder, LittleEndian};
use fidl_fuchsia_logger::{self, LogMessage};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{future, stream, TryFutureExt, TryStreamExt};

pub fn add_listener<F>(callback: F) -> Result<(), zx::Status>
where
    F: 'static + Send + Fn(LogMessage, usize) + Sync,
{
    let debuglog = zx::DebugLog::create(zx::DebugLogOpts::READABLE)?;

    let f = stream::repeat(Ok(()))
        .try_fold((callback, debuglog), |(callback, debuglog), ()| {
            // TODO: change OnSignals to wrap this so that is is not created again and again.
            fasync::OnSignals::new(&debuglog, zx::Signals::LOG_READABLE).and_then(|_| {
                let mut buf = Vec::with_capacity(zx::sys::ZX_LOG_RECORD_MAX);
                loop {
                    match debuglog.read(&mut buf) {
                        Err(status) if status == zx::Status::SHOULD_WAIT => {
                            return future::ready(Ok((callback, debuglog)));
                        }
                        Err(status) => {
                            return future::ready(Err(status));
                        }
                        Ok(()) => {
                            let mut l = LogMessage {
                                time: LittleEndian::read_i64(&buf[8..16]),
                                pid: LittleEndian::read_u64(&buf[16..24]),
                                tid: LittleEndian::read_u64(&buf[24..32]),
                                severity: fidl_fuchsia_logger::LogLevelFilter::Info as i32,
                                dropped_logs: 0,
                                tags: vec!["klog".to_string()],
                                msg: String::new(),
                            };
                            let data_len = LittleEndian::read_u16(&buf[4..6]) as usize;
                            l.msg = match String::from_utf8(buf[32..(32 + data_len)].to_vec()) {
                                Err(e) => {
                                    eprintln!("logger: invalid log record: {:?}", e);
                                    continue;
                                }
                                Ok(s) => s,
                            };
                            if let Some(b'\n') = l.msg.bytes().last() {
                                l.msg.pop();
                            }
                            let size = logger::METADATA_SIZE + 5/*tag*/ + l.msg.len() + 1;
                            callback(l, size);
                        }
                    }
                }
            })
        })
        .map_ok(|_| ());

    fasync::spawn(f.unwrap_or_else(|e| {
        eprintln!("logger: not able to apply listener to kernel logs, failed: {:?}", e)
    }));

    Ok(())
}
