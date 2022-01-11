// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_scenic, fuchsia_trace as ftrace};

#[cfg(feature = "flatland")]
use {
    fidl_fuchsia_ui_composition::{ContentId, FlatlandProxy, PresentArgs, TransformId},
    fuchsia_zircon as zx,
    std::sync::atomic::{AtomicUsize, Ordering},
    std::{cell::RefCell, rc::Rc},
};

#[cfg(not(feature = "flatland"))]
use {fidl_fuchsia_images::PresentationInfo, std::cell::Cell};

#[cfg(feature = "flatland")]
pub type FlatlandInstanceId = usize;

#[cfg(feature = "flatland")]
static NEXT_FLATLAND_INSTANCE_ID: AtomicUsize = AtomicUsize::new(1);

#[cfg(feature = "flatland")]
pub struct Flatland {
    flatland: FlatlandProxy,
    id: FlatlandInstanceId,
    id_generator: fuchsia_scenic::flatland::IdGenerator,
    release_fences: Vec<zx::Event>,
    next_trace_id: u64,
}

#[cfg(feature = "flatland")]
pub type FlatlandPtr = Rc<RefCell<Flatland>>;

/// A thin wrapper around `FlatlandProxy`, which currently exits
/// to allocate transform/content ids, and implement flow events
/// for `present` calls.
///
/// Scenic will maintain it's own present counter so we need to ensure
/// each an every present call increments the trace_id otherwise our
/// traces will not be generated correctly.
#[cfg(feature = "flatland")]
impl Flatland {
    pub fn new(flatland: FlatlandProxy) -> FlatlandPtr {
        let id = NEXT_FLATLAND_INSTANCE_ID.fetch_add(1, Ordering::SeqCst);
        let debug_name = format!("WaylandBridge:{}", id);
        flatland.set_debug_name(&debug_name).expect("fidl error");

        Rc::new(RefCell::new(Flatland {
            flatland,
            id,
            id_generator: fuchsia_scenic::flatland::IdGenerator::new(),
            release_fences: vec![],
            next_trace_id: 1,
        }))
    }

    pub fn id(&self) -> FlatlandInstanceId {
        self.id
    }

    pub fn proxy(&self) -> &FlatlandProxy {
        &self.flatland
    }

    pub fn add_release_fence(&mut self, fence: zx::Event) {
        self.release_fences.push(fence);
    }

    pub fn present(&mut self, presentation_time: i64) {
        ftrace::flow_begin!("gfx", "Flatland::Present", self.alloc_trace_id());
        let release_fences: Vec<_> = self.release_fences.drain(..).collect();
        self.flatland
            .present(PresentArgs {
                requested_presentation_time: Some(presentation_time),
                acquire_fences: None,
                release_fences: Some(release_fences),
                // Allow frames to be skipped when commit rate is too high.
                unsquashable: Some(false),
                ..PresentArgs::EMPTY
            })
            .unwrap_or_else(|e| eprintln!("present error: {:?}", e));
    }

    pub fn alloc_transform_id(&mut self) -> TransformId {
        self.id_generator.next_transform_id()
    }

    pub fn alloc_content_id(&mut self) -> ContentId {
        self.id_generator.next_content_id()
    }

    pub fn alloc_trace_id(&mut self) -> u64 {
        let id = self.next_trace_id;
        self.next_trace_id += 1;
        id
    }
}

/// A thin wrapper around `fuchsia_scenic::ScenicSession`, which currently exits
/// to properly implement flow events for `present` calls.
///
/// Scenic will maintain it's own counter so we need to ensure each an every
/// present call increments the trace_id otherwise our traces will not be
/// generated correctly.
#[cfg(not(feature = "flatland"))]
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
