// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::FatalError;
use anyhow::Error;
use async_trait::async_trait;
use fidl_fuchsia_media::{FormatDetails, StreamOutputFormat};
use fidl_table_validation::*;
use fuchsia_stream_processors::*;
use hex::{decode, encode};
use mundane::hash::{Digest, Hasher, Sha256};
use std::io::Write;
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
#[async_trait(?Send)]
pub trait OutputValidator {
    async fn validate(&self, output: &[Output]) -> Result<(), Error>;
}

/// Validates that the output contains the expected number of packets.
pub struct OutputPacketCountValidator {
    pub expected_output_packet_count: usize,
}

#[async_trait(?Send)]
impl OutputValidator for OutputPacketCountValidator {
    async fn validate(&self, output: &[Output]) -> Result<(), Error> {
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

#[async_trait(?Send)]
impl OutputValidator for TerminatesWithValidator {
    async fn validate(&self, output: &[Output]) -> Result<(), Error> {
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

/// Validates that an output's format matches expected
pub struct FormatValidator {
    pub expected_format: FormatDetails,
}

#[async_trait(?Send)]
impl OutputValidator for FormatValidator {
    async fn validate(&self, output: &[Output]) -> Result<(), Error> {
        let packets: Vec<&OutputPacket> = output_packets(output).collect();
        let format = &packets
            .first()
            .ok_or(FatalError(String::from("No packets in output")))?
            .format
            .format_details;

        if self.expected_format != *format {
            return Err(FatalError(format!(
                "Expected {:?}; got {:?}",
                self.expected_format, format
            ))
            .into());
        }

        Ok(())
    }
}

/// Validates that an output's data exactly matches an expected hash, including oob_bytes
pub struct BytesValidator {
    pub output_file: Option<&'static str>,
    pub expected_digests: Vec<ExpectedDigest>,
}

impl BytesValidator {
    fn write_and_hash(
        &self,
        mut file: impl Write,
        oob: &[u8],
        packets: &[&OutputPacket],
    ) -> Result<(), Error> {
        let mut hasher = Sha256::default();

        hasher.update(oob);

        for packet in packets {
            file.write_all(&packet.data)?;
            hasher.update(&packet.data);
        }

        let digest = hasher.finish().bytes();

        if let None = self.expected_digests.iter().find(|e| e.bytes == digest) {
            return Err(FatalError(format!(
                "Expected one of {:?}; got {}",
                self.expected_digests,
                encode(digest)
            ))
            .into());
        }

        Ok(())
    }

    fn output_file(&self) -> Result<impl Write, Error> {
        Ok(if let Some(file) = self.output_file {
            Box::new(std::fs::File::create(file)?) as Box<dyn Write>
        } else {
            Box::new(std::io::sink()) as Box<dyn Write>
        })
    }
}

#[async_trait(?Send)]
impl OutputValidator for BytesValidator {
    async fn validate(&self, output: &[Output]) -> Result<(), Error> {
        let packets: Vec<&OutputPacket> = output_packets(output).collect();
        let oob = packets
            .first()
            .ok_or(FatalError(String::from("No packets in output")))?
            .format
            .format_details
            .oob_bytes
            .clone()
            .unwrap_or(vec![]);

        self.write_and_hash(self.output_file()?, oob.as_slice(), &packets)
    }
}

#[derive(Clone)]
pub struct ExpectedDigest {
    pub label: &'static str,
    pub bytes: <<Sha256 as Hasher>::Digest as Digest>::Bytes,
    pub per_frame_bytes: Option<Vec<<<Sha256 as Hasher>::Digest as Digest>::Bytes>>,
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
            per_frame_bytes: None,
        }
    }
    pub fn new_with_per_frame_digest(
        label: &'static str,
        hex: impl AsRef<[u8]>,
        per_frame_hexen: Vec<impl AsRef<[u8]>>,
    ) -> Self {
        Self {
            per_frame_bytes: Some(
                per_frame_hexen
                    .into_iter()
                    .map(|per_frame_hex| {
                        decode(per_frame_hex)
                            .expect("Decoding static compile-time test hash as valid hex")
                            .as_slice()
                            .try_into()
                            .expect("Taking 32 bytes from compile-time test hash")
                    })
                    .collect(),
            ),
            ..Self::new(label, hex)
        }
    }
}

impl fmt::Display for ExpectedDigest {
    fn fmt(&self, w: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(w, "{:?}", self)
    }
}

impl fmt::Debug for ExpectedDigest {
    fn fmt(&self, w: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(w, "ExpectedDigest {{\n")?;
        write!(w, "\tlabel: {}", self.label)?;
        write!(w, "\tbytes: {}", encode(self.bytes))?;
        write!(w, "}}")
    }
}
