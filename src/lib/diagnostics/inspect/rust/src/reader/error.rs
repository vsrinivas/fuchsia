// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error;
use crate::format::block_type::BlockType;
use diagnostics_hierarchy::Error as HierarchyError;
use fuchsia_zircon as zx;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum ReaderError {
    #[error("FIDL error")]
    Fidl(#[source] anyhow::Error),

    #[error("Lazy node callback failed")]
    LazyCallback(#[source] anyhow::Error),

    #[error("expected header block on index 0")]
    MissingHeader,

    #[error("Cannot find parent block index {0}")]
    ParentIndexNotFound(u32),

    #[error("Malformed tree, no complete node with parent=0")]
    MalformedTree,

    #[error("VMO format error")]
    VmoFormat(#[source] error::Error),

    #[error("Tried to read more slots than available at block index {0}")]
    AttemptedToReadTooManyArraySlots(u32),

    #[error("unexpected array entry type format: {0:?}")]
    UnexpectedArrayEntryFormat(BlockType),

    #[error("Failed to parse name at index {0}")]
    ParseName(u32),

    #[error("Failed to get link content at index {0}")]
    GetLinkContent(u32),

    #[error("Failed to get extent at index {0}")]
    GetExtent(u32),

    #[error("Failed to get consistent snapshot")]
    InconsistentSnapshot,

    #[error("Failed to read snapshot from vmo")]
    ReadSnapshotFromVmo,

    #[error("Header missing or is locked")]
    MissingHeaderOrLocked,

    #[error("Cannot read from no-op Inspector")]
    NoOpInspector,

    #[error("Failed to call vmo")]
    Vmo(zx::Status),

    #[error("Error creating node hierarchy")]
    Hierarchy(#[source] HierarchyError),

    #[error("Failed to duplicate vmo handle")]
    DuplicateVmo,

    #[error("Failed to fetch vmo from Tree content")]
    FetchVmo,

    #[error("Failed to load tree name {0}")]
    FailedToLoadTree(String),

    #[error("Failed to lock inspector state")]
    FailedToLockState(#[source] error::Error),
}
