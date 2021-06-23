// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::operations::OperationType, std::marker::PhantomData};

pub struct DurationScope<'a> {
    _phantom: PhantomData<&'a u8>,
}

pub fn get_trace_event(
    _sequential: bool,
    _operation: OperationType,
    _alignment: u64,
) -> DurationScope<'static> {
    DurationScope { _phantom: PhantomData }
}

pub fn create_tracer(_log_ftrace: bool) {}
