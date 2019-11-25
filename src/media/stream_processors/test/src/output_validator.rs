// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::FatalError;
use failure::Error;
use fidl_fuchsia_media::{FormatDetails, StreamOutputFormat};
use fidl_table_validation::*;
use fuchsia_stream_processors::*;
use hex::{decode, encode};
use mundane::hash::{Digest, Hasher, Sha256};
use std::{convert::TryInto, fmt, rc::Rc};

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

/// Returns all the packets in the output with preserved order.
pub fn output_packets(output: &[Output]) -> impl Iterator<Item = &OutputPacket> {
    output.iter().filter_map(|output| match output {
        Output::Packet(packet) => Some(packet),
        _ => None,
    })
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

#[derive(Copy, Clone)]
pub struct ExpectedDigest {
    pub label: &'static str,
    pub bytes: <<Sha256 as Hasher>::Digest as Digest>::Bytes,
}

impl ExpectedDigest {
    pub fn new(label: &'static str, hex: impl AsRef<[u8]>) -> Self {
        Self {
            label,
            bytes: decode(hex)
                .expect("Decoding static compile-time test hash as valid hex")
                .as_slice()
                .try_into()
                .expect("Taking 32 bytes from compile-time test hash"),
        }
    }
}

impl fmt::Display for ExpectedDigest {
    fn fmt(&self, w: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(w, "ExpectedDigest {{\n")?;
        write!(w, "\tlabel: {}", self.label)?;
        write!(w, "\tbytes: {}", encode(self.bytes))?;
        write!(w, "}}")
    }
}
