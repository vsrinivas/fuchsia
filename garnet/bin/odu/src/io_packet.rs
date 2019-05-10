// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! IoPacket trait encapsulates all the information needed to carry out an IO.
//! It also maintains meta-information about the IO. Each IoPacket maintain time
//! spent in various stages of of IoPacket lifetime. This will help in
//! generating detailed heatmaps (http://www.brendangregg.com/HeatMaps/latency.html)
//! of various aspects of IO

use {
    crate::operations::{OperationType, PipelineStages},
    std::{
        io::{Error, Result},
        marker::{Send, Sync},
        ops::Range,
        time::Instant,
    },
};

pub type IoPacketType = Box<IoPacket + Send + Sync>;

/// This struct maintains monotonically increasing clock time. Though these fields
/// are u128, when writing to disk we write u64 nanos which is sufficient to track
/// single uptime of a system.
#[derive(Clone, Copy)]
pub struct TimeInterval {
    start: Instant,
    end: Instant,
}

impl TimeInterval {
    /// Create a new interval
    pub fn new() -> TimeInterval {
        let start = Instant::now();
        TimeInterval { start: start, end: start }
    }

    /// If creation time and start time are different then explicitly start
    /// measuring time.
    pub fn start(&mut self) {
        self.start = Instant::now();
    }

    /// The start and end time in nanos relative to an arbitrary start_instant.
    pub fn interval_to_u64(&self, start_instant: &Instant) -> (u64, u64) {
        let ustart = self.start.duration_since(*start_instant).as_nanos();
        let uend = self.end.duration_since(*start_instant).as_nanos();
        (ustart as u64, uend as u64)
    }

    /// End measuring the time interval.
    pub fn end(&mut self) {
        self.end = Instant::now();
    }

    /// Duration between create/start and end in nano seconds. This function may
    /// panic if end time is earlier than start time.
    pub fn duration(self) -> u128 {
        self.end.duration_since(self.start).as_nanos()
    }
}

pub enum InternalCommand {
    Abort,
    Exit,
    None,
}

pub trait IoPacket: IoPacketClone {
    /// Function returns type of operation this IoPacket will perform
    fn operation_type(&self) -> OperationType;

    /// Returns IO sequence number of this packet
    fn sequence_number(&self) -> u64;

    /// Starts measuring time for the IO for /stage/
    fn timestamp_stage_start(&mut self, stage: PipelineStages);

    /// Ends measuring time for the IO for /stage/
    fn timestamp_stage_end(&mut self, stage: PipelineStages);

    /// Returns time in nanos spent by this packet in the /stage/
    fn stage_duration(&self, stage: PipelineStages) -> u128;

    /// Returns time (in nanos) internal relative to an arbitrary Instant.
    fn interval_to_u64(&self, stage: PipelineStages) -> (u64, u64);

    /// Command to internal command
    fn abort_or_exit(&self) -> InternalCommand {
        match self.operation_type() {
            OperationType::Abort => InternalCommand::Abort,
            OperationType::Exit => InternalCommand::Exit,
            _ => InternalCommand::None,
        }
    }

    /// Returns reference to offset range on which this io packet worked
    fn io_offset_range(&self) -> Range<u64>;

    /// Returns size of the io
    fn io_size(&self) -> u64 {
        let range = self.io_offset_range();
        range.end - range.start
    }

    /// Perform IO
    fn do_io(&mut self);

    /// Return true if the IO is complete. False if it is still pending (for
    /// asynchronous operations).
    fn is_complete(&self) -> bool;

    /// Returns true if verification of IO needs additional IO
    fn verify_needs_io(&self) -> bool;

    /// Generates a verification IoPacket
    fn generate_verify_io(&mut self);

    /// Returns result of the IO command
    fn get_error(&self) -> Result<()>;

    /// Sets error on a IoPacket
    fn set_error(&mut self, error: Error);

    /// Verify the completed IO packet
    fn verify(&mut self, verify_io: &IoPacket) -> bool;

    /// Returns pointer to mutable buffer
    fn buffer_mut(&mut self) -> &mut Vec<u8>;

    /// Returns pointer to buffer
    fn buffer(&mut self) -> &Vec<u8>;
}

/// Make IoPacket clone able for verifier.
pub trait IoPacketClone {
    fn clone_box(&self) -> IoPacketType;
}

impl<T> IoPacketClone for T
where
    T: IoPacket + Send + Sync + Clone + 'static,
{
    fn clone_box(&self) -> IoPacketType {
        Box::new(self.clone())
    }
}

impl Clone for IoPacketType {
    fn clone(&self) -> Self {
        self.clone_box()
    }
}
