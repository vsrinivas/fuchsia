// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io_packet::{IoPacket, IoPacketType},
    serde_derive::{Deserialize, Serialize},
    std::{io::Result, ops::Range, slice::Iter, sync::Arc, time::Instant},
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
        OPERATIONS.into_iter()
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
        STAGES.into_iter()
    }
}

pub type TargetType = Arc<Box<Target + Send + Sync>>;

/// Targets is the object on which IO operations can be performed. Target traits
/// help to create IoPackets, which operate on the. For example files,
/// directories, block devices and blobs can be targets that implement
/// their own way of doing IO.
/// Currently only File blocking IO call are implemented. Some of these
/// functions are no-ops as the work is still in progress.
pub trait Target {
    fn setup(&mut self, file_name: &String, range: Range<u64>) -> Result<()>;

    // TODO(auradkar): This function prototype is weird - it takes self (of type
    // Target) and it also takes TargetType (of type Arc<Box<Target>>). We don't
    // have use for self. All we want is IoPacket to hold a reference over
    // Target throughout IoPackets life span. I am unable to make the code
    // compile without passing both self and TargetType. I need to figure out
    // a way to do this.
    fn create_io_packet(
        &self,
        operation_type: OperationType,
        seq: u64,
        seed: u64,
        io_offset_range: Range<u64>,
        target: TargetType, // &Arc<Box<Target + Send + Sync>>,
    ) -> IoPacketType;

    /// Returns target unique identifier.
    fn id(&self) -> u64;

    /// Returns a reference to a struct which contains all the valid operations
    /// for the instance of the target. See allowed_ops.
    fn supported_ops() -> &'static TargetOps
    where
        Self: Sized;

    /// allowed_ops() is a set of operations that the user is allowed to
    /// request to be measured against this particular kind of target,
    /// while supported_ops() is a set of operations that the generator is
    /// allowed to generate for this target.
    ///
    /// allowed_ops() must be a non-strict subset of supported_ops().
    ///
    /// The reason a generator is allowed to generate operations that are not
    /// allowed for explicit user selection, is because for certain targets
    /// generators may need to follow particular patterns when generating
    /// operations. And we still want to see individual times for those
    /// operations.
    ///
    /// Currently one example is the "truncate" operation on blobfs. "truncate"
    /// can and must only be used on a newly created blobs before any data is
    /// written in the blob. The generator will need to issue "open",
    /// followed by "truncate", when creating a new blob and we want to see
    /// individual times for both operations. At the same time, allowing the
    /// users to request "truncate" based loads is meaningless.
    fn allowed_ops() -> &'static TargetOps
    where
        Self: Sized;

    /// issues an IO
    fn do_io(&self, io_packet: &mut IoPacket);

    /// Returns true if the issued IO is complete.
    fn is_complete(&self, io_packet: &IoPacket) -> bool;

    /// Returns true if verify needs an IO
    fn verify_needs_io(&self, io_packet: &IoPacket) -> bool;

    /// Generates parameters for verify IO packet.
    fn generate_verify_io(&self, io_packet: &mut IoPacket);

    /// Verifies "success" of an IO. Returns true if IO was successful.
    fn verify(&self, io_packet: &mut IoPacket, verify_packet: &IoPacket) -> bool;

    fn start_instant(&self) -> Instant;
}

/// Not all targets implement all operations. For example truncate is meaningless
/// for block device where as readdir is meaningless for posix files. When a
/// structure implements a Target trait, this structure helps to programmatically
/// know what are valid operations for a given Target.
#[derive(Clone, Default, Debug, Serialize, Deserialize)]
pub struct TargetOps {
    pub write: bool,
    pub open: bool,
    //    pub read: bool,
    //    pub lseek: bool,
    //    pub close: bool,
    //    pub fsync: bool,
    //
    //
    //    pub create: bool,
    //    pub unlink: bool,
    //    pub createdir: bool,
    //    pub deletedir: bool,
    //    pub readdir: bool,
    //    pub opendir: bool,
    //    pub link: bool,
    //
    //    pub mount: bool,
    //    pub unmount: bool,
}

impl TargetOps {
    pub fn friendly_names() -> Vec<&'static str> {
        vec!["write", "open"]
    }

    pub fn enabled(&self, name: &str) -> bool {
        match name {
            "write" => return self.write,
            "open" => return self.open,
            _ => false,
        }
    }

    pub fn enabled_operation_names(&self) -> Vec<&'static str> {
        TargetOps::friendly_names().iter().filter(|name| self.enabled(name)).map(|&x| x).collect()
    }

    pub fn enable(&mut self, name: &str, enabled: bool) -> std::result::Result<(), &'static str> {
        match name {
            "write" => self.write = enabled,
            "open" => self.open = enabled,
            _ => return Err("Invalid input"),
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use crate::operations::{OperationType, TargetOps};

    #[test]
    fn enabled_all_enabled() {
        let x: TargetOps = TargetOps { write: true, open: true };
        assert!(x.enabled("write"));
        assert!(x.enabled("open"));
    }

    #[test]
    fn enabled_none_enabled() {
        let x: TargetOps = TargetOps { write: false, open: false };
        assert!(!x.enabled("write"));
        assert!(!x.enabled("open"));
    }

    #[test]
    fn enable_toggle_one() {
        let mut x: TargetOps = TargetOps { write: false, open: false };
        assert!(!x.enabled("write"));
        assert!(!x.enabled("open"));

        x.enable("open", true).unwrap();
        assert!(!x.enabled("write"));
        assert!(x.enabled("open"));

        x.enable("open", false).unwrap();
        assert!(!x.enabled("write"));
        assert!(!x.enabled("open"));
    }

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
