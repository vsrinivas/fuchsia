// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains fuzzing targets for Archivist.

use arbitrary::{Arbitrary, Result, Unstructured};
use archivist_lib::logs;
use diagnostics_message::Message;
use fuchsia_zircon as zx;
use fuzz::fuzz;

#[derive(Clone, Debug)]
struct RandomLogRecord(zx::sys::zx_log_record_t);

/// Fuzzer for kernel debuglog parser.
#[fuzz]
fn convert_debuglog_to_log_message_fuzzer(record: RandomLogRecord) -> Option<Message> {
    logs::convert_debuglog_to_log_message(&record.0)
}

impl Arbitrary for RandomLogRecord {
    fn arbitrary(u: &mut Unstructured<'_>) -> Result<Self> {
        let sequence = u64::arbitrary(u)?;
        let padding1: [zx::sys::PadByte; 4] = Default::default();
        let mut datalen = u16::arbitrary(u)?;
        if usize::from(datalen) > zx::sys::ZX_LOG_RECORD_DATA_MAX {
            datalen = zx::sys::ZX_LOG_RECORD_DATA_MAX.to_string().parse::<u16>().unwrap();
        }
        let severity = u8::arbitrary(u)?;
        let flags = u8::arbitrary(u)?;
        let timestamp = i64::arbitrary(u)? as zx::sys::zx_time_t;
        let pid = u64::arbitrary(u)?;
        let tid = u64::arbitrary(u)?;

        // Fill the first datalen bytes of data.
        let mut data = [0 as u8; zx::sys::ZX_LOG_RECORD_DATA_MAX];
        let mut partial = &mut data[0..datalen as usize];
        u.fill_buffer(&mut partial)?;

        Ok(RandomLogRecord(zx::sys::zx_log_record_t {
            sequence,
            padding1,
            datalen,
            severity,
            flags,
            timestamp,
            pid,
            tid,
            data,
        }))
    }

    fn size_hint(_: usize) -> (usize, Option<usize>) {
        (
            std::mem::size_of::<zx::sys::zx_log_record_t>(),
            Some(std::mem::size_of::<zx::sys::zx_log_record_t>()),
        )
    }
}
