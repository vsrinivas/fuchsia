// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::Cell;

use fidl_fuchsia_images::PresentationInfo;
use fuchsia_scenic;
use fuchsia_trace as ftrace;

/// A thin wrapper around `fuchsia_scenic::ScenicSession`, which currently exits
/// to properly implement flow events for `present` calls.
///
/// Scenic will maintain it's own counter so we need to ensure each an every
/// present call increments the trace_id otherwise our traces will not be
/// generated correctly.
#[derive(Clone)]
pub struct ScenicSession {
    session: fuchsia_scenic::SessionPtr,
    next_trace_id: Cell<u64>,
}

impl ScenicSession {
    pub fn new(session: fuchsia_scenic::SessionPtr) -> Self {
        ScenicSession { session, next_trace_id: Cell::new(0) }
    }

    pub fn as_inner(&self) -> &fuchsia_scenic::SessionPtr {
        &self.session
    }

    pub fn present(
        &self,
        presentation_time: u64,
    ) -> fidl::client::QueryResponseFut<PresentationInfo> {
        ftrace::flow_begin!("gfx", "Session::Present", self.alloc_trace_id());
        self.session.lock().present(presentation_time)
    }

    fn alloc_trace_id(&self) -> u64 {
        let trace_id = self.next_trace_id.get();
        self.next_trace_id.set(trace_id + 1);
        trace_id
    }
}
