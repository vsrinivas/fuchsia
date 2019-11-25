// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use byteorder::{BigEndian, WriteBytesExt};
use fidl::encoding::Decodable;
use fidl_fuchsia_media::FormatDetails;
use std::{convert::TryFrom, fs, mem, path::Path};
use stream_processor_test::*;

pub const BEAR_TEST_FILE: &str = "/pkg/data/bear.h264";
const NAL_START_CODE: u32 = 1;

/// Represents an H264 elementary stream.
pub struct H264Stream {
    data: Vec<u8>,
}

impl H264Stream {
    /// Constructs an H264 elementary stream from a file with raw elementary stream data.
    pub fn from_file(filename: impl AsRef<Path>) -> Result<Self> {
        Ok(H264Stream::from(fs::read(filename)?))
    }

    /// Returns an iterator over H264 NALs that does not copy.
    fn nal_iter(&self) -> impl Iterator<Item = H264Nal<'_>> {
        H264NalIter { data: &self.data, pos: 0 }
    }
}

impl From<Vec<u8>> for H264Stream {
    fn from(data: Vec<u8>) -> Self {
        Self { data }
    }
}

impl ElementaryStream for H264Stream {
    fn format_details(&self, version_ordinal: u64) -> FormatDetails {
        FormatDetails {
            format_details_version_ordinal: Some(version_ordinal),
            mime_type: Some(String::from("video/h264")),
            ..<FormatDetails as Decodable>::new_empty()
        }
    }

    fn is_access_units(&self) -> bool {
        true
    }

    fn stream<'a>(&'a self) -> Box<dyn Iterator<Item = ElementaryStreamChunk<'_>> + 'a> {
        Box::new(self.nal_iter().map(|nal| ElementaryStreamChunk {
            start_access_unit: true,
            known_end_access_unit: true,
            data: nal.data,
            significance: match nal.kind {
                H264NalKind::Picture => Significance::Video(VideoSignificance::Picture),
                H264NalKind::NotPicture => Significance::Video(VideoSignificance::NotPicture),
            },
            timestamp: None,
        }))
    }
}

pub struct H264SeiItuT35 {
    pub country_code: u8,
    pub country_code_extension: u8,
    pub payload: Vec<u8>,
}

impl H264SeiItuT35 {
    pub const COUNTRY_CODE_UNITED_STATES: u8 = 0xb5;

    pub fn as_bytes(&self) -> Result<Vec<u8>> {
        const ITU_T35_PAYLOAD_TYPE: u8 = 4;

        let mut bytes = vec![];
        bytes.write_u32::<BigEndian>(NAL_START_CODE)?;
        bytes.write_u8(H264NalKind::SEI_CODE)?;
        bytes.write_u8(ITU_T35_PAYLOAD_TYPE)?;
        bytes.write_u8(u8::try_from(self.payload_size())?)?;
        bytes.write_u8(self.country_code)?;
        bytes.write_u8(self.country_code_extension)?;
        bytes.append(&mut self.payload.clone());
        Ok(bytes)
    }

    fn payload_size(&self) -> usize {
        mem::size_of::<u8>() + mem::size_of::<u8>() + self.payload.len()
    }
}

pub struct H264Nal<'a> {
    pub kind: H264NalKind,
    pub data: &'a [u8],
}

pub enum H264NalKind {
    Picture,
    NotPicture,
}

impl H264NalKind {
    const NON_IDR_PICTURE_CODE: u8 = 1;
    const IDR_PICTURE_CODE: u8 = 5;
    const SEI_CODE: u8 = 6;

    fn from_header(header: u8) -> Self {
        let kind = header & 0xf;
        if kind == Self::NON_IDR_PICTURE_CODE || kind == Self::IDR_PICTURE_CODE {
            H264NalKind::Picture
        } else {
            H264NalKind::NotPicture
        }
    }
}

struct H264NalStart<'a> {
    /// Position in the h264 stream of the start.
    pos: usize,
    /// All the data from the start of the NAL onward.
    data: &'a [u8],
    kind: H264NalKind,
}

/// An iterator over NALs in an H264 stream.
struct H264NalIter<'a> {
    data: &'a [u8],
    pos: usize,
}

impl<'a> H264NalIter<'a> {
    fn next_nal(&self, pos: usize) -> Option<H264Nal<'a>> {
        // This won't need to search if pos already at a start code.
        let nal_start = self.next_nal_start(pos)?;
        // We search 3 bytes after the found nal's start, because that will
        // ensure we don't just find the same start code again.
        match self.next_nal_start(nal_start.pos + 3) {
            Some(next_start) => Some(H264Nal {
                kind: nal_start.kind,
                data: &nal_start.data[0..(next_start.pos - nal_start.pos)],
            }),
            None => Some(H264Nal { kind: nal_start.kind, data: nal_start.data }),
        }
    }

    fn next_nal_start(&self, pos: usize) -> Option<H264NalStart<'a>> {
        // This search size will find 3 and 4 byte start codes, and the
        // header value.
        const NAL_SEARCH_SIZE: usize = 5;

        let data = self.data.get(pos..)?;
        data.windows(NAL_SEARCH_SIZE).enumerate().find_map(|(i, candidate)| match candidate {
            [0, 0, 0, 1, h] | [0, 0, 1, h, _] => Some(H264NalStart {
                pos: i + pos,
                data: data.get(i..).expect("Getting slice starting where we just matched"),
                kind: H264NalKind::from_header(*h),
            }),
            _ => None,
        })
    }
}

impl<'a> Iterator for H264NalIter<'a> {
    type Item = H264Nal<'a>;
    fn next(&mut self) -> Option<Self::Item> {
        let nal = self.next_nal(self.pos);
        self.pos += nal.as_ref().map(|n| n.data.len()).unwrap_or(0);
        nal
    }
}
