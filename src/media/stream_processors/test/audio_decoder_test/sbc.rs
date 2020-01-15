// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use anyhow::{format_err, Result};
use bitfield::bitfield;
use fidl::encoding::Decodable;
use fidl_fuchsia_media::*;
use std::{convert::TryInto, fs, io, path::Path};
use stream_processor_test::*;

/// Represents an SBC elementary stream.
pub struct SbcStream {
    data: Vec<u8>,
    oob_bytes: Vec<u8>,
    chunk_frames: usize,
}

impl SbcStream {
    /// Constructs an SBC elementary stream from a file with raw elementary stream data.
    pub fn from_file(
        filename: impl AsRef<Path>,
        codec_info: &[u8],
        chunk_frames: usize,
    ) -> io::Result<Self> {
        Ok(SbcStream { data: fs::read(filename)?, oob_bytes: codec_info.to_vec(), chunk_frames })
    }

    /// Returns an iterator over SBC frames that does not copy.
    fn frame_iter(&self) -> impl Iterator<Item = SbcFrame<'_>> {
        SbcFrameIter { data: &self.data, pos: 0, chunk_frames: self.chunk_frames }
    }
}

impl ElementaryStream for SbcStream {
    fn format_details(&self, version_ordinal: u64) -> FormatDetails {
        FormatDetails {
            format_details_version_ordinal: Some(version_ordinal),
            mime_type: Some(String::from("audio/sbc")),
            oob_bytes: Some(self.oob_bytes.clone()),
            ..<FormatDetails as Decodable>::new_empty()
        }
    }

    fn is_access_units(&self) -> bool {
        false
    }

    fn stream<'a>(&'a self) -> Box<dyn Iterator<Item = ElementaryStreamChunk<'_>> + 'a> {
        Box::new(self.frame_iter().map(|frame| ElementaryStreamChunk {
            start_access_unit: false,
            known_end_access_unit: false,
            data: frame.data,
            significance: Significance::Audio(AudioSignificance::Encoded),
            timestamp: None,
        }))
    }
}

#[derive(Debug, PartialEq)]
enum ChannelMode {
    Mono,
    DualChannel,
    Stereo,
    JointStereo,
}

impl From<u8> for ChannelMode {
    fn from(bits: u8) -> Self {
        match bits {
            0 => ChannelMode::Mono,
            1 => ChannelMode::DualChannel,
            2 => ChannelMode::Stereo,
            3 => ChannelMode::JointStereo,
            _ => panic!("invalid channel mode"),
        }
    }
}

bitfield! {
    pub struct SbcFrameHeader(u32);
    impl Debug;
    u8;
    syncword, _: 7, 0;
    subbands, _: 8;
    allocation_method, _: 9;
    into ChannelMode, channel_mode, _: 11, 10;
    blocks_bits, _: 13, 12;
    sampling_frequency_bits, _: 15, 14;
    bitpool_bits, _: 23, 16;
    crc_check, _: 31, 24;
}

impl SbcFrameHeader {
    /// The number of channels, based on the channel mode in the header.
    /// From Table 12.18 in the A2DP Spec.
    fn channels(&self) -> usize {
        match self.channel_mode() {
            ChannelMode::Mono => 1,
            _ => 2,
        }
    }

    fn has_syncword(&self) -> bool {
        const SBC_SYNCWORD: u8 = 0x9c;
        self.syncword() == SBC_SYNCWORD
    }

    /// The number of blocks, based on tbe bits in the header.
    /// From Table 12.17 in the A2DP Spec.
    fn blocks(&self) -> usize {
        4 * (self.blocks_bits() + 1) as usize
    }

    fn bitpool(&self) -> usize {
        self.bitpool_bits() as usize
    }

    /// Number of subbands based on the header bit.
    /// From Table 12.20 in the A2DP Spec.
    fn num_subbands(&self) -> usize {
        if self.subbands() {
            8
        } else {
            4
        }
    }

    /// Calculates the frame length.
    /// Formula from Section 12.9 of the A2DP Spec.
    fn frame_length(&self) -> Result<usize> {
        if !self.has_syncword() {
            return Err(format_err!("syncword does not match"));
        }
        let len = 4 + (4 * self.num_subbands() * self.channels()) / 8;
        let rest = (match self.channel_mode() {
            ChannelMode::Mono | ChannelMode::DualChannel => {
                self.blocks() * self.channels() * self.bitpool()
            }
            ChannelMode::Stereo => self.blocks() * self.bitpool(),
            ChannelMode::JointStereo => self.num_subbands() + (self.blocks() * self.bitpool()),
        } as f64
            / 8.0)
            .ceil() as usize;
        Ok(len + rest)
    }

    /// Given a buffer with an SBC frame at the start, find the length of the
    /// SBC frame.
    fn find_sbc_frame_len(buf: &[u8]) -> Result<usize> {
        if buf.len() < 4 {
            return Err(format_err!("Buffer too short for header"));
        }
        let hdr = u32::from_le_bytes((&buf[0..4]).try_into()?);
        SbcFrameHeader(hdr).frame_length()
    }
}

pub struct SbcFrame<'a> {
    pub data: &'a [u8],
    pub length: usize,
}

/// An iterator over frames in an SBC stream.
struct SbcFrameIter<'a> {
    data: &'a [u8],
    pos: usize,
    chunk_frames: usize,
}

impl<'a> SbcFrameIter<'a> {
    fn next_frame(&self, pos: usize) -> Option<SbcFrame<'a>> {
        SbcFrameHeader::find_sbc_frame_len(&self.data[pos..])
            .map(|len| {
                let end_pos = std::cmp::min(pos + len * self.chunk_frames, self.data.len());
                let frame_len = end_pos - pos;
                SbcFrame { data: &self.data[pos..end_pos], length: frame_len }
            })
            .ok()
    }
}

impl<'a> Iterator for SbcFrameIter<'a> {
    type Item = SbcFrame<'a>;
    fn next(&mut self) -> Option<Self::Item> {
        if self.pos >= self.data.len() {
            return None;
        }
        let frame = self.next_frame(self.pos);
        self.pos += frame.as_ref().map(|f| f.length).unwrap_or(0);
        frame
    }
}
