// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use byteorder::{BigEndian, WriteBytesExt};
use fidl::encoding::Decodable;
use fidl_fuchsia_media::FormatDetails;
use std::{convert::TryFrom, fs, mem, path::Path};
use stream_processor_test::*;

const SEPARATE_SPS_PPS: bool = true;
// written to vector as big-endian
const NAL_START_CODE: u32 = 1;

/// Represents an H264 elementary stream.
pub struct H264Stream {
    data: Vec<u8>,
}

impl H264Stream {
    /// Adds data to raw H264 elementary stream
    pub fn append(&mut self, data: &mut Vec<u8>) {
        self.data.append(data);
    }

    /// Constructs an H264 elementary stream from a file with raw elementary stream data.
    pub fn from_file(filename: impl AsRef<Path>) -> Result<Self> {
        Ok(H264Stream::from(fs::read(filename)?))
    }

    /// Returns an iterator over H264 NALs that does not copy.
    pub fn nal_iter(&self) -> impl Iterator<Item = H264Nal<'_>> {
        H264NalIter { data: &self.data, pos: 0 }
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
                Some(H264NalKind::IDR) | Some(H264NalKind::NonIDR) => {
                    Significance::Video(VideoSignificance::Picture)
                }
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
        bytes.write_u8(SEI_CODE)?;
        bytes.write_u8(ITU_T35_PAYLOAD_TYPE)?;
        bytes.write_u8(u8::try_from(self.payload_size())?)?;
        bytes.write_u8(self.country_code)?;
        bytes.write_u8(self.country_code_extension)?;
        bytes.append(&mut self.payload.clone());
        Ok(bytes)
    }

    fn payload_size(&self) -> usize {
        // At the time this is called, the country_code and country_code_extension bytes haven't
        // yet been written.
        mem::size_of::<u8>() + mem::size_of::<u8>() + self.payload.len()
    }
}

#[derive(Debug)]
pub struct H264Nal<'a> {
    pub kind: H264NalKind,
    pub data: &'a [u8],
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum H264NalKind {
    NonIDR,
    IDR,
    SEI,
    SPS,
    PPS,
    Unknown,
}

const NON_IDR_PICTURE_CODE: u8 = 1;
const IDR_PICTURE_CODE: u8 = 5;
const SEI_CODE: u8 = 6;
const SPS_CODE: u8 = 7;
const PPS_CODE: u8 = 8;

impl H264NalKind {
    fn from_header(header: u8) -> Self {
        let kind = header & 0x1f;
        match kind {
            kind if kind == IDR_PICTURE_CODE => H264NalKind::IDR,
            kind if kind == NON_IDR_PICTURE_CODE => H264NalKind::NonIDR,
            kind if kind == SPS_CODE => H264NalKind::SPS,
            kind if kind == PPS_CODE => H264NalKind::PPS,
            kind if kind == SEI_CODE => H264NalKind::SEI,
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

#[derive(Debug)]
pub struct H264Chunk<'a> {
    /// Only NonIDR, IDR are used, or not set if last data in file needs to be emitted but IDR or
    /// NonIDR wasn't last in the file.
    pub nal_kind: Option<H264NalKind>,
    pub data: &'a [u8],
}

/// An iterator over groupings of NALs in an H264 stream.
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
                        // picture
                        H264NalKind::IDR | H264NalKind::NonIDR => true,
                        // not picture
                        _ => SEPARATE_SPS_PPS,
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

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_nal_iter() {
        let sps_pps_idr = [
            0x00, 0x00, 0x00, 0x01, 0x67, 0x4d, 0x00, 0x28, 0xf4, 0x03, 0xc0, 0x11, 0x3f, 0x2a,
            0x00, 0x00, 0x00, 0x01, 0x68, 0xee, 0x38, 0x80, 0x00, 0x00, 0x00, 0x01, 0x65, 0x88,
            0x84, 0x01,
        ];

        let iter = H264NalIter { data: &sps_pps_idr, pos: 0 };

        let result: Vec<H264Nal<'_>> = iter.collect();

        assert_eq!(result[0].kind, H264NalKind::SPS);
        assert_eq!(result[1].kind, H264NalKind::PPS);
        assert_eq!(result[2].kind, H264NalKind::IDR);
    }

    #[test]
    fn test_chunk_iter() {
        let sps_pps_idr = [
            0x00, 0x00, 0x00, 0x01, 0x67, 0x4d, 0x00, 0x28, 0xf4, 0x03, 0xc0, 0x11, 0x3f, 0x2a,
            0x00, 0x00, 0x00, 0x01, 0x68, 0xee, 0x38, 0x80, 0x00, 0x00, 0x00, 0x01, 0x65, 0x88,
            0x84, 0x01,
        ];

        let iter = H264ChunkIter::create(&sps_pps_idr);

        let result: Vec<H264Chunk<'_>> = iter.collect();

        if SEPARATE_SPS_PPS {
            assert_eq!(result[0].nal_kind.expect("nal"), H264NalKind::SPS);
            assert_eq!(result[1].nal_kind.expect("nal"), H264NalKind::PPS);
            assert_eq!(result[2].nal_kind.expect("nal"), H264NalKind::IDR);
        } else {
            assert_eq!(result[0].nal_kind.expect("nal"), H264NalKind::IDR);
        }
    }
}
