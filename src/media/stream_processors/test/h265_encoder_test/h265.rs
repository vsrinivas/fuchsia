// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::encoding::Decodable;
use fidl_fuchsia_media::FormatDetails;
use stream_processor_test::*;

/// Represents an H265 elementary stream.
pub struct H265Stream {
    data: Vec<u8>,
}

impl H265Stream {
    /// Adds data to raw H265 elementary stream
    pub fn append(&mut self, data: &mut Vec<u8>) {
        self.data.append(data);
    }

    /// Returns an iterator over H265 NALs that does not copy.
    pub fn nal_iter(&self) -> impl Iterator<Item = H265Nal<'_>> {
        H265NalIter { data: &self.data, pos: 0 }
    }
}

impl From<Vec<u8>> for H265Stream {
    fn from(data: Vec<u8>) -> Self {
        Self { data }
    }
}

impl ElementaryStream for H265Stream {
    fn format_details(&self, version_ordinal: u64) -> FormatDetails {
        FormatDetails {
            format_details_version_ordinal: Some(version_ordinal),
            mime_type: Some(String::from("video/h265")),
            ..<FormatDetails as Decodable>::new_empty()
        }
    }

    fn is_access_units(&self) -> bool {
        true
    }

    fn stream<'a>(&'a self) -> Box<dyn Iterator<Item = ElementaryStreamChunk> + 'a> {
        Box::new(self.nal_iter().map(|nal| ElementaryStreamChunk {
            start_access_unit: true,
            known_end_access_unit: true,
            data: nal.data.to_vec(),
            significance: match nal.kind {
                H265NalKind::IRAP | H265NalKind::NonIRAP => {
                    Significance::Video(VideoSignificance::Picture)
                }
                _ => Significance::Video(VideoSignificance::NotPicture),
            },
            timestamp: None,
        }))
    }
}

#[derive(Debug)]
pub struct H265Nal<'a> {
    pub kind: H265NalKind,
    pub data: &'a [u8],
}

#[derive(Debug, Clone, PartialEq)]
pub enum H265NalKind {
    IRAP,
    NonIRAP,
    VPS,
    SPS,
    PPS,
    Unknown,
}

const IDR_W_RADL_CODE: u8 = 19;
const CRA_NUT_CODE: u8 = 21;
const VPS_CODE: u8 = 32;
const SPS_CODE: u8 = 33;
const PPS_CODE: u8 = 34;
const TRAIL_R_CODE: u8 = 1;

impl H265NalKind {
    fn from_header(header: u8) -> Self {
        let kind = (header >> 1) & 0x3f;
        match kind {
            kind if kind == IDR_W_RADL_CODE => H265NalKind::IRAP,
            kind if kind == CRA_NUT_CODE => H265NalKind::IRAP,
            kind if kind == VPS_CODE => H265NalKind::VPS,
            kind if kind == SPS_CODE => H265NalKind::SPS,
            kind if kind == PPS_CODE => H265NalKind::PPS,
            kind if kind == TRAIL_R_CODE => H265NalKind::NonIRAP,
            _ => H265NalKind::Unknown,
        }
    }
}

struct H265NalStart<'a> {
    /// Position in the h265 stream of the start.
    pos: usize,
    /// All the data from the start of the NAL onward.
    data: &'a [u8],
    kind: H265NalKind,
}

/// An iterator over NALs in an H265 stream.
struct H265NalIter<'a> {
    data: &'a [u8],
    pos: usize,
}

impl<'a> H265NalIter<'a> {
    fn next_nal(&self, pos: usize) -> Option<H265Nal<'a>> {
        // This won't need to search if pos already at a start code.
        let nal_start = self.next_nal_start(pos)?;
        // We search 3 bytes after the found nal's start, because that will
        // ensure we don't just find the same start code again.
        match self.next_nal_start(nal_start.pos + 3) {
            Some(next_start) => Some(H265Nal {
                kind: nal_start.kind,
                data: &nal_start.data[0..(next_start.pos - nal_start.pos)],
            }),
            None => Some(H265Nal { kind: nal_start.kind, data: nal_start.data }),
        }
    }

    fn next_nal_start(&self, pos: usize) -> Option<H265NalStart<'a>> {
        // This search size will find 3 and 4 byte start codes, and the
        // header value.
        const NAL_SEARCH_SIZE: usize = 5;

        let data = self.data.get(pos..)?;
        data.windows(NAL_SEARCH_SIZE).enumerate().find_map(|(i, candidate)| match candidate {
            [0, 0, 0, 1, h] | [0, 0, 1, h, _] => Some(H265NalStart {
                pos: i + pos,
                data: data.get(i..).expect("Getting slice starting where we just matched"),
                kind: H265NalKind::from_header(*h),
            }),
            _ => None,
        })
    }
}

impl<'a> Iterator for H265NalIter<'a> {
    type Item = H265Nal<'a>;
    fn next(&mut self) -> Option<Self::Item> {
        let nal = self.next_nal(self.pos);
        self.pos += nal.as_ref().map(|n| n.data.len()).unwrap_or(0);
        nal
    }
}
