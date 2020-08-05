// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use byteorder::{BigEndian, WriteBytesExt};
use fidl::encoding::Decodable;
use fidl_fuchsia_media::FormatDetails;
use std::{convert::TryFrom, fs, mem, path::Path};
use stream_processor_test::*;

const SEPARATE_SPS_PPS: bool = true;
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

    /// Returns an iterator over H264 chunks (pictures and anything preceding each picture) that
    /// does not copy.
    //
    // TODO(fxb/13483): Make H264ChunkIter capable of iterating NALs or iterating pictures with any
    // non-picture NALs in front of each picture prepended (the current behavior).
    fn chunk_iter(&self) -> impl Iterator<Item = H264Chunk<'_>> {
        H264ChunkIter::create(&self.data)
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

    fn stream<'a>(&'a self) -> Box<dyn Iterator<Item = ElementaryStreamChunk> + 'a> {
        Box::new(self.chunk_iter().map(|chunk| ElementaryStreamChunk {
            start_access_unit: true,
            known_end_access_unit: true,
            data: chunk.data.to_vec(),
            significance: match chunk.nal_kind {
                Some(H264NalKind::Picture) => Significance::Video(VideoSignificance::Picture),
                _ => Significance::Video(VideoSignificance::NotPicture),
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
    const SPS_CODE: u8 = 7;
    const PPS_CODE: u8 = 8;
    const SEI_CODE: u8 = 6;

    fn from_header(header: u8) -> Self {
        let kind = header & 0x1f;
        if kind == Self::NON_IDR_PICTURE_CODE || kind == Self::IDR_PICTURE_CODE {
            H264NalKind::Picture
        } else if kind == Self::SPS_CODE || kind == Self::PPS_CODE || kind == Self::SEI_CODE {
            H264NalKind::NotPicture
        } else {
            // When assigning a nal_unit_type to "NotPicture" or "Picture", note that NotPicture
            // NALs are buffered up until a Picture NAL is seen, at which point the buffered
            // NotPicture(s) + 1 Picture are put in a packet.
            panic!("Unrecognized nal_unit_type - please add as needed - type: {}", kind);
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
                pos: pos + i,
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

pub struct H264Chunk<'a> {
    pub nal_kind: Option<H264NalKind>,
    pub data: &'a [u8],
}

struct H264ChunkIter<'a> {
    nal_iter: H264NalIter<'a>,
    chunk_start_pos: Option<usize>,
}

impl<'a> H264ChunkIter<'a> {
    fn create(data: &'a [u8]) -> H264ChunkIter<'a> {
        H264ChunkIter { nal_iter: H264NalIter { data: data, pos: 0 }, chunk_start_pos: None }
    }
}

impl<'a> Iterator for H264ChunkIter<'a> {
    type Item = H264Chunk<'a>;
    fn next(&mut self) -> Option<Self::Item> {
        let chunk = loop {
            let maybe_nal = self.nal_iter.next();
            match maybe_nal {
                Some(nal) => {
                    if let None = self.chunk_start_pos {
                        self.chunk_start_pos = Some(self.nal_iter.pos - nal.data.len());
                    }
                    let queue_chunk = match nal.kind {
                        H264NalKind::NotPicture => SEPARATE_SPS_PPS,
                        H264NalKind::Picture => true,
                    };
                    if queue_chunk {
                        break Some(H264Chunk {
                            nal_kind: Some(nal.kind),
                            data: &self.nal_iter.data
                                [self.chunk_start_pos.unwrap()..self.nal_iter.pos],
                        });
                    } else {
                        continue;
                    }
                }
                None => {
                    if self.chunk_start_pos.expect("Zero NALs?") == self.nal_iter.data.len() {
                        // done
                        break None;
                    } else {
                        break Some(H264Chunk {
                            nal_kind: None,
                            data: &self.nal_iter.data[self.chunk_start_pos.unwrap()..],
                        });
                    }
                }
            }
        };
        *self.chunk_start_pos.as_mut().unwrap() +=
            chunk.as_ref().map(|c| c.data.len()).unwrap_or(0);
        chunk
    }
}
