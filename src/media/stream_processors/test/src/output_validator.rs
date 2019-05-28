// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{buffer_set::*, FatalError};
use failure::Error;
use fidl_fuchsia_media::{FormatDetails, StreamOutputFormat};
use fidl_table_validation::*;
use std::rc::Rc;

#[derive(ValidFidlTable, Debug, PartialEq)]
#[fidl_table_src(StreamOutputFormat)]
pub struct ValidStreamOutputFormat {
    pub stream_lifetime_ordinal: u64,
    pub format_details: FormatDetails,
}

/// An output packet from the stream.
#[derive(Debug, PartialEq)]
pub struct OutputPacket {
    pub data: Vec<u8>,
    pub format: Rc<ValidStreamOutputFormat>,
    pub packet: ValidPacket,
}

/// Output represents any output from a stream we might want to validate programmatically.
///
/// This may extend to contain not just explicit events but certain stream control behaviors or
/// even errors.
#[derive(Debug, PartialEq)]
pub enum Output {
    Packet(OutputPacket),
    Eos { stream_lifetime_ordinal: u64 },
    CodecChannelClose,
}

/// Checks all output packets, which are provided to the validator in the order in which they
/// were received from the stream processor.
///
/// Failure should be indicated by returning an error, not by panic, so that the full context of
/// the error will be available in the failure output.
pub trait OutputValidator {
    fn validate(&self, output: &[Output]) -> Result<(), Error>;
}

/// Validates that the output contains the expected number of packets.
pub struct OutputPacketCountValidator {
    pub expected_output_packet_count: usize,
}

impl OutputValidator for OutputPacketCountValidator {
    fn validate(&self, output: &[Output]) -> Result<(), Error> {
        let actual_output_packet_count: usize = output
            .iter()
            .filter(|output| match output {
                Output::Packet(_) => true,
                _ => false,
            })
            .count();

        if actual_output_packet_count != self.expected_output_packet_count {
            return Err(FatalError(format!(
                "actual output packet count: {}; expected output packet count: {}",
                actual_output_packet_count, self.expected_output_packet_count
            ))
            .into());
        }

        Ok(())
    }
}

/// Validates that a stream terminates with Eos.
pub struct TerminatesWithValidator {
    pub expected_terminal_output: Output,
}

impl OutputValidator for TerminatesWithValidator {
    fn validate(&self, output: &[Output]) -> Result<(), Error> {
        let actual_terminal_output = output.last().ok_or(FatalError(format!(
            "In terminal output: expected {:?}; found: None",
            Some(&self.expected_terminal_output)
        )))?;

        if *actual_terminal_output == self.expected_terminal_output {
            Ok(())
        } else {
            Err(FatalError(format!(
                "In terminal output: expected {:?}; found: {:?}",
                Some(&self.expected_terminal_output),
                actual_terminal_output
            ))
            .into())
        }
    }
}
