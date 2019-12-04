// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    serde_derive::{Deserialize, Serialize},
    std::slice::Iter,
};

/// The type of operations that can be performed using odu. Not all targets may
/// implement all the operations.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum OperationType {
    // Persist a buffer onto target that can be read at a later point in time.
    Write,

    // "Open" target for other IO operations. This is not implement at the
    // moment but is used to unit test some of the generate functionality.
    Open,

    //    Meta operations
    // Finish all outstanding operations and forward the command to next stage
    // of the pipeline before exiting gracefully.
    Exit,

    // Abort all outstanding operations and exit as soon as possible.
    Abort,
    //    Read,
    //    LSeek,
    //    Truncate,
    //    Close,
    //    FSync,
    //
    //    /// DirOps
    //    Create,
    //    Unlink,
    //    CreateDir,
    //    DeleteDir,
    //    ReadDir,
    //    OpenDir,
    //    Link, /// This is for hard links only. Symlinks are small files.
    //
    //    /// FsOps
    //    Mount,
    //    Unmount,
    //
}

/// These functions makes better indexing and walking stages a bit better.
impl OperationType {
    pub const fn operations_count() -> usize {
        4 // number of entries in OperationType.
    }

    pub fn operation_number(self) -> usize {
        self as usize
    }

    pub fn iterator() -> Iter<'static, OperationType> {
        static OPERATIONS: [OperationType; OperationType::operations_count()] =
            [OperationType::Write, OperationType::Open, OperationType::Exit, OperationType::Abort];
        OPERATIONS.iter()
    }
}

/// IoPackets go through different stages in pipeline. These stages help track
// the IO and also are indicative of how loaded different parts of the app is.
#[derive(Serialize, Deserialize, Debug, Clone, Copy)]
pub enum PipelineStages {
    /// IoPacket is in generator stage
    Generate = 0,

    /// IoPacket is in issuer stage
    Issue = 1,

    /// IoPacket is in verifier stage
    Verify = 2,
}

/// These functions makes better indexing and walking stages a bit better.
impl PipelineStages {
    pub const fn stage_count() -> usize {
        3 // number of entries in PipelineStages.
    }

    pub fn stage_number(self) -> usize {
        self as usize
    }

    pub fn iterator() -> Iter<'static, PipelineStages> {
        static STAGES: [PipelineStages; PipelineStages::stage_count()] =
            [PipelineStages::Generate, PipelineStages::Issue, PipelineStages::Verify];
        STAGES.iter()
    }
}

#[cfg(test)]
mod tests {
    use crate::operations::OperationType;

    #[test]
    fn operation_count_test() {
        assert_eq!(OperationType::operations_count(), 4);
    }

    #[test]
    fn operation_iterator_count_test() {
        assert_eq!(OperationType::operations_count(), OperationType::iterator().count());
    }

    #[test]
    fn operation_iterator_uniqueness() {
        for (i, operation) in OperationType::iterator().enumerate() {
            assert!(i == operation.operation_number());
        }
    }
}
