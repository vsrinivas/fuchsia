// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Read kernel logs, convert them to LogMessages and serve them.

use crate::logs::logger;
use byteorder::{ByteOrder, LittleEndian};
use fidl_fuchsia_logger::{self, LogMessage};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::stream;
use futures::stream::Stream;

pub struct KernelLogger {
    debuglogger: zx::DebugLog,
    buf: Vec<u8>,
}

pub struct KernelLogsIterator<'a> {
    klogger: &'a mut KernelLogger,
}

impl KernelLogger {
    pub fn create() -> Result<KernelLogger, zx::Status> {
        let resource = zx::Resource::from(zx::Handle::invalid());
        Ok(KernelLogger {
            debuglogger: zx::DebugLog::create(&resource, zx::DebugLogOpts::READABLE)?,
            buf: Vec::with_capacity(zx::sys::ZX_LOG_RECORD_MAX),
        })
    }

    fn read_log(&mut self) -> Result<(LogMessage, usize), zx::Status> {
        loop {
            match self.debuglogger.read(&mut self.buf) {
                Err(status) => {
                    return Err(status);
                }
                Ok(()) => {
                    let mut l = LogMessage {
                        time: LittleEndian::read_i64(&self.buf[8..16]),
                        pid: LittleEndian::read_u64(&self.buf[16..24]),
                        tid: LittleEndian::read_u64(&self.buf[24..32]),
                        severity: fidl_fuchsia_logger::LogLevelFilter::Info as i32,
                        dropped_logs: 0,
                        tags: vec!["klog".to_string()],
                        msg: String::new(),
                    };
                    let data_len = LittleEndian::read_u16(&self.buf[4..6]) as usize;
                    l.msg = match String::from_utf8(self.buf[32..(32 + data_len)].to_vec()) {
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
                    return Ok((l, size));
                }
            }
        }
    }

    pub fn log_stream<'a>(&'a mut self) -> KernelLogsIterator<'a> {
        KernelLogsIterator { klogger: self }
    }
}

impl<'a> Iterator for KernelLogsIterator<'a> {
    type Item = Result<(LogMessage, usize), zx::Status>;
    fn next(&mut self) -> Option<Self::Item> {
        match self.klogger.read_log() {
            Err(zx::Status::SHOULD_WAIT) => {
                return None;
            }
            Err(status) => {
                return Some(Err(status));
            }
            Ok(n) => {
                return Some(Ok(n));
            }
        }
    }
}

pub fn listen(
    klogger: KernelLogger,
) -> impl Stream<Item = Result<(LogMessage, usize), zx::Status>> {
    stream::unfold((true, klogger), move |(mut is_readable, mut klogger)| async move {
        loop {
            if !is_readable {
                if let Err(e) =
                    fasync::OnSignals::new(&klogger.debuglogger, zx::Signals::LOG_READABLE).await
                {
                    break Some((Err(e), (is_readable, klogger)));
                }
            }
            is_readable = true;
            match klogger.read_log() {
                Err(zx::Status::SHOULD_WAIT) => {
                    is_readable = false;
                    continue;
                }
                x => break Some((x, (is_readable, klogger))),
            }
        }
    })
}
