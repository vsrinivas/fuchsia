// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_ui_composition::FlatlandProxy,
    fuchsia_scenic, fuchsia_trace as ftrace, fuchsia_zircon as zx,
    std::{
        cell::{Cell, RefCell},
        rc::Rc,
    },
};

#[cfg(feature = "flatland")]
use {
    fidl_fuchsia_ui_composition::{ContentId, PresentArgs, TransformId},
    std::sync::atomic::{AtomicUsize, Ordering},
};

#[cfg(not(feature = "flatland"))]
use fidl_fuchsia_images::PresentationInfo;

pub type FlatlandInstanceId = usize;

#[cfg(feature = "flatland")]
static NEXT_FLATLAND_INSTANCE_ID: AtomicUsize = AtomicUsize::new(1);

#[derive(Clone)]
pub struct Flatland {
    flatland: FlatlandProxy,
    id: FlatlandInstanceId,
    release_fences: Rc<RefCell<Vec<zx::Event>>>,
    next_transform_id: Cell<u64>,
    next_content_id: Cell<u64>,
    next_trace_id: Cell<u64>,
}

/// A thin wrapper around `FlatlandProxy`, which currently exits
/// to allocate transform/content ids, and implement flow events
/// for `present` calls.
///
/// Scenic will maintain it's own present counter so we need to ensure
/// each an every present call increments the trace_id otherwise our
/// traces will not be generated correctly.
#[cfg(feature = "flatland")]
impl Flatland {
    pub fn new(flatland: FlatlandProxy) -> Self {
        Flatland {
            flatland,
            id: NEXT_FLATLAND_INSTANCE_ID.fetch_add(1, Ordering::SeqCst),
            release_fences: Rc::new(RefCell::new(vec![])),
            next_transform_id: Cell::new(1),
            next_content_id: Cell::new(1),
            next_trace_id: Cell::new(0),
        }
    }

    pub fn id(&self) -> FlatlandInstanceId {
        self.id
    }

    pub fn proxy(&self) -> &FlatlandProxy {
        &self.flatland
    }

    pub fn add_release_fence(&self, fence: zx::Event) {
        self.release_fences.borrow_mut().push(fence);
    }

    pub fn present(&self, presentation_time: i64) {
        ftrace::flow_begin!("gfx", "Flatland::Present", self.alloc_trace_id());
        let release_fences = Some(self.release_fences.replace(vec![]));
        self.flatland
            .present(PresentArgs {
                requested_presentation_time: Some(presentation_time),
                acquire_fences: None,
                release_fences,
                unsquashable: Some(true),
                ..PresentArgs::EMPTY
            })
            .unwrap_or_else(|e| eprintln!("present error: {:?}", e));
    }

    pub fn alloc_transform_id(&self) -> TransformId {
        let transform_id = self.next_transform_id.get();
        self.next_transform_id.set(transform_id + 1);
        TransformId { value: transform_id }
    }

    pub fn alloc_content_id(&self) -> ContentId {
        let content_id = self.next_content_id.get();
        self.next_content_id.set(content_id + 1);
        ContentId { value: content_id }
    }

    fn alloc_trace_id(&self) -> u64 {
        let trace_id = self.next_trace_id.get();
        self.next_trace_id.set(trace_id + 1);
        trace_id
    }
}

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

#[cfg(not(feature = "flatland"))]
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
