// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::model::AbsoluteMoniker, fidl_fuchsia_sys2 as fsys, std::cmp::Ordering};

/// `WorkItem` is a single item in the ordered-by-deadline collection maintained by `WorkScheduler`.
#[derive(Clone, Debug, Eq)]
pub(super) struct WorkItem {
    // TODO(markdittmer): Document.
    pub(super) abs_moniker: AbsoluteMoniker,
    /// Unique identifier for this unit of work **relative to others with the same
    /// `abs_moniker`**.
    pub(super) id: String,
    /// Next deadline for this unit of work, in monotonic time.
    pub(super) next_deadline_monotonic: i64,
    /// Period between repeating this unit of work (if any), measure in nanoseconds.
    pub(super) period: Option<i64>,
}

/// WorkItem default equality: identical `abs_moniker` and `id`.
impl PartialEq for WorkItem {
    fn eq(&self, other: &Self) -> bool {
        self.id == other.id && self.abs_moniker == other.abs_moniker
    }
}

impl WorkItem {
    pub(super) fn new(
        abs_moniker: &AbsoluteMoniker,
        id: &str,
        next_deadline_monotonic: i64,
        period: Option<i64>,
    ) -> Self {
        WorkItem {
            abs_moniker: abs_moniker.clone(),
            id: id.to_string(),
            next_deadline_monotonic,
            period,
        }
    }

    /// Produce a canonical `WorkItem` from its identifying information: `abs_moniker` + `id`. Note
    /// that other fields are ignored in equality testing.
    pub(super) fn new_by_identity(abs_moniker: &AbsoluteMoniker, id: &str) -> Self {
        WorkItem {
            abs_moniker: abs_moniker.clone(),
            id: id.to_string(),
            next_deadline_monotonic: 0,
            period: None,
        }
    }

    /// Attempt to unpack identifying info (`abs_moniker`, `id`) + `WorkRequest` into a `WorkItem`.
    /// Errors:
    /// - INVALID_ARGUMENTS: Missing or invalid `work_request.start` value.
    pub(super) fn try_new(
        abs_moniker: &AbsoluteMoniker,
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
        Ok(WorkItem::new(abs_moniker, id, *next_deadline_monotonic, work_request.period))
    }

    pub(super) fn deadline_order(left: &Self, right: &Self) -> Ordering {
        left.next_deadline_monotonic.cmp(&right.next_deadline_monotonic)
    }
}
