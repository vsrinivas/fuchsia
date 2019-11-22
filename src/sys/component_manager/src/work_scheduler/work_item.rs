// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::work_scheduler::dispatcher::Dispatcher,
    fidl_fuchsia_sys2 as fsys,
    std::{cmp::Ordering, fmt, sync::Arc},
};

/// `WorkItem` is a single item in the ordered-by-deadline collection maintained by `WorkScheduler`.
#[derive(Clone, Eq)]
pub(super) struct WorkItem {
    /// A reference-counted pointer to the `WorkItem`'s `Dispatcher`. This is retained by the
    /// `WorkItem` so that `Dispatcher` implementations can perform cleanup work when they are no
    /// longer referenced by outstanding `WorkItem`s.
    pub(super) dispatcher: Arc<dyn Dispatcher>,
    /// Unique identifier for this unit of work **relative to others with the same
    /// `dispatcher`**.
    pub(super) id: String,
    /// Next deadline for this unit of work, in monotonic time.
    pub(super) next_deadline_monotonic: i64,
    /// Period between repeating this unit of work (if any), measure in nanoseconds.
    pub(super) period: Option<i64>,
}

impl fmt::Debug for WorkItem {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        fmt.debug_struct("WorkItem")
            .field("id", &self.id)
            .field("next_deadline_monotonic", &self.next_deadline_monotonic)
            .field("period", &self.period)
            .finish()
    }
}

/// WorkItem default equality: identical `dispatcher` and `id`.
impl PartialEq for WorkItem {
    fn eq(&self, other: &Self) -> bool {
        self.id == other.id && self.dispatcher.abs_moniker() == other.dispatcher.abs_moniker()
    }
}

impl WorkItem {
    pub(super) fn new(
        dispatcher: Arc<dyn Dispatcher>,
        id: &str,
        next_deadline_monotonic: i64,
        period: Option<i64>,
    ) -> Self {
        WorkItem { dispatcher, id: id.to_string(), next_deadline_monotonic, period }
    }

    /// Produce a canonical `WorkItem` from its identifying information: `abs_moniker` + `id`. Note
    /// that other fields are ignored in equality testing.
    pub(super) fn new_by_identity(dispatcher: Arc<dyn Dispatcher>, id: &str) -> Self {
        WorkItem { dispatcher, id: id.to_string(), next_deadline_monotonic: 0, period: None }
    }

    /// Attempt to unpack identifying info (`abs_moniker`, `id`) + `WorkRequest` into a `WorkItem`.
    /// Errors:
    /// - INVALID_ARGUMENTS: Missing or invalid `work_request.start` value.
    pub(super) fn try_new(
        dispatcher: Arc<dyn Dispatcher>,
        id: &str,
        work_request: &fsys::WorkRequest,
    ) -> Result<Self, fsys::Error> {
        let next_deadline_monotonic = match &work_request.start {
            None => Err(fsys::Error::InvalidArguments),
            Some(start) => match start {
                fsys::Start::MonotonicTime(monotonic_time) => Ok(monotonic_time),
                _ => Err(fsys::Error::InvalidArguments),
            },
        }?;
        Ok(WorkItem::new(dispatcher, id, *next_deadline_monotonic, work_request.period))
    }

    pub(super) fn deadline_order(left: &Self, right: &Self) -> Ordering {
        left.next_deadline_monotonic.cmp(&right.next_deadline_monotonic)
    }
}
