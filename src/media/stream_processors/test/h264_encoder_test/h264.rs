// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::encoding::Decodable;
use fidl_fuchsia_media::FormatDetails;
use stream_processor_test::*;

/// Represents an H264 elementary stream.
pub struct H264Stream {
    data: Vec<u8>,
}

impl H264Stream {
    /// Adds data to raw H264 elementary stream
    pub fn append(&mut self, data: &mut Vec<u8>) {
        self.data.append(data);
    }

    /// Returns an iterator over H264 NALs that does not copy.
    pub fn nal_iter(&self) -> impl Iterator<Item = H264Nal<'_>> {
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
                H264NalKind::IDR | H264NalKind::NonIDR => {
                    Significance::Video(VideoSignificance::Picture)
                }
                _ => Significance::Video(VideoSignificance::NotPicture),
            },
            timestamp: None,
        }))
    }
}

#[derive(Debug)]
pub struct H264Nal<'a> {
    pub kind: H264NalKind,
    pub data: &'a [u8],
}

#[derive(Debug, Clone, PartialEq)]
pub enum H264NalKind {
    IDR,
    NonIDR,
    SPS,
    PPS,
    Unknown,
}

const IDR_PICTURE_CODE: u8 = 5;
const NON_IDR_PICTURE_CODE: u8 = 1;
const SPS_CODE: u8 = 7;
const PPS_CODE: u8 = 8;

impl H264NalKind {
    fn from_header(header: u8) -> Self {
        let kind = header & 0xf;
        match kind {
            kind if kind == IDR_PICTURE_CODE => H264NalKind::IDR,
            kind if kind == NON_IDR_PICTURE_CODE => H264NalKind::NonIDR,
            kind if kind == SPS_CODE => H264NalKind::SPS,
            kind if kind == PPS_CODE => H264NalKind::PPS,
            _ => H264NalKind::Unknown,
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
