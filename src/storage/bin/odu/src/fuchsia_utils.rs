// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::operations::OperationType, cstr::cstr, fuchsia_trace as ftrace,
    fuchsia_trace_provider::trace_provider_create_with_fdio,
};

pub struct DurationScope<'a> {
    _duration_scope: Option<ftrace::DurationScope<'a>>,
}

pub fn get_trace_event(
    sequential: bool,
    operation: OperationType,
    alignment: u64,
) -> DurationScope<'static> {
    // This is kind of a hack. The ftrace::duration expects static CStr. Odu relies on command line
    // arguments through which most of the parameters are sent. Converting str/String to static CStr
    // is not possible. Since we want ftrace only for those run that are started by bots and we
    // intend to limit different combination of these parameters in bot, this hack seems ok!
    //
    // For operations other than writes, we are not going to log any message since we do not do
    // mounting, unmounting at the time when test starts and tears down, respectively.
    DurationScope {
        _duration_scope: match (sequential, operation, alignment) {
            (true, OperationType::Write, 8192) => {
                Some(ftrace::duration(cstr!("benchmark"), cstr!("sequential/write/8192"), &[]))
            }
            (false, OperationType::Write, 8192) => {
                Some(ftrace::duration(cstr!("benchmark"), cstr!("random/write/8192"), &[]))
            }
            (true, OperationType::Write, 1) => Some(ftrace::duration(
                cstr!("benchmark"),
                cstr!("sequential/write/random_size"),
                &[],
            )),
            (false, OperationType::Write, 1) => {
                Some(ftrace::duration(cstr!("benchmark"), cstr!("random/write/random_size"), &[]))
            }
            (true, OperationType::Read, 8192) => {
                Some(ftrace::duration(cstr!("benchmark"), cstr!("sequential/read/8192"), &[]))
            }
            (false, OperationType::Read, 8192) => {
                Some(ftrace::duration(cstr!("benchmark"), cstr!("random/read/8192"), &[]))
            }
            (true, OperationType::Read, 1) => Some(ftrace::duration(
                cstr!("benchmark"),
                cstr!("sequential/read/random_size"),
                &[],
            )),
            (false, OperationType::Read, 1) => {
                Some(ftrace::duration(cstr!("benchmark"), cstr!("random/read/random_size"), &[]))
            }
            _ => None,
        },
    }
}

pub fn create_tracer(log_ftrace: bool) {
    if log_ftrace {
        trace_provider_create_with_fdio();
    }
}
