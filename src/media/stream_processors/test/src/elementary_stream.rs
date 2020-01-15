// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_media::FormatDetails;

pub trait ElementaryStream {
    fn format_details(&self, version_ordinal: u64) -> FormatDetails;

    /// Whether _all_ chunks in the elementary stream will be marked with access unit boundaries.
    /// These are units for stream processors (e.g. for H264 decoder, NALs). When input is not in
    /// access units, the server must parse and/or buffer the bitstream.
    fn is_access_units(&self) -> bool;

    fn stream<'a>(&'a self) -> Box<dyn Iterator<Item = ElementaryStreamChunk<'_>> + 'a>;

    /// Returns the elementary stream with chunks capped at a given size. Chunks bigger than the cap
    /// will be divided into multiple chunks. Order is retained. Timestamps are not extrapolated.
    /// Access unit boundaries are corrected.
    fn capped_chunks<'a>(
        &'a self,
        max_size: usize,
    ) -> Box<dyn Iterator<Item = ElementaryStreamChunk<'_>> + 'a> {
        Box::new(self.stream().flat_map(move |chunk| CappedSizeChunks {
            src: chunk,
            offset: 0,
            max_size,
        }))
    }

    fn video_frame_count(&self) -> usize {
        self.stream()
            .filter(|chunk| match chunk.significance {
                Significance::Video(VideoSignificance::Picture) => true,
                _ => false,
            })
            .count()
    }
}

#[derive(Copy, Clone, Debug)]
pub struct ElementaryStreamChunk<'a> {
    pub start_access_unit: bool,
    pub known_end_access_unit: bool,
    pub data: &'a [u8],
    pub significance: Significance,
    pub timestamp: Option<u64>,
}

#[derive(Copy, Clone, Debug)]
pub enum Significance {
    Video(VideoSignificance),
    Audio(AudioSignificance),
}

#[derive(Copy, Clone, Debug)]
pub enum VideoSignificance {
    Picture,
    NotPicture,
}

#[derive(Copy, Clone, Debug)]
pub enum AudioSignificance {
    PcmFrames,
    Encoded,
}

struct CappedSizeChunks<'a> {
    src: ElementaryStreamChunk<'a>,
    offset: usize,
    max_size: usize,
}

impl<'a> Iterator for CappedSizeChunks<'a> {
    type Item = ElementaryStreamChunk<'a>;
    fn next(&mut self) -> Option<Self::Item> {
        if self.offset >= self.src.data.len() {
            return None;
        }

        let len = std::cmp::min(self.src.data.len() - self.offset, self.max_size);
        let next_offset = self.offset + len;
        let is_first_subchunk = self.offset == 0;
        let is_last_subchunk = next_offset == self.src.data.len();
        let chunk = ElementaryStreamChunk {
            start_access_unit: self.src.start_access_unit && is_first_subchunk,
            known_end_access_unit: self.src.known_end_access_unit && is_last_subchunk,
            data: &self.src.data[self.offset..next_offset],
            timestamp: if is_first_subchunk { self.src.timestamp } else { None },
            significance: self.src.significance,
        };
        self.offset = next_offset;

        Some(chunk)
    }
}

/// Wraps an elementary stream and adds sequential dummy timestamps to its chunks.
pub struct TimestampedStream<S, I> {
    pub source: S,
    pub timestamps: I,
}

impl<S, I> ElementaryStream for TimestampedStream<S, I>
where
    S: ElementaryStream,
    I: Iterator<Item = u64> + Clone,
{
    fn format_details(&self, version_ordinal: u64) -> FormatDetails {
        self.source.format_details(version_ordinal)
    }

    fn is_access_units(&self) -> bool {
        self.source.is_access_units()
    }

    fn stream<'a>(&'a self) -> Box<dyn Iterator<Item = ElementaryStreamChunk<'_>> + 'a> {
        let mut timestamps = self.timestamps.clone();
        Box::new(self.source.stream().map(move |mut chunk| {
            match chunk.significance {
                Significance::Video(VideoSignificance::Picture) => {
                    chunk.timestamp = timestamps.next();
                }
                _ => {}
            };
            chunk
        }))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl::encoding::Decodable;

    struct FakeStream {
        data: Vec<u8>,
    }

    impl ElementaryStream for FakeStream {
        fn format_details(&self, _version_ordinal: u64) -> FormatDetails {
            Decodable::new_empty()
        }

        fn is_access_units(&self) -> bool {
            true
        }

        fn stream<'a>(&'a self) -> Box<dyn Iterator<Item = ElementaryStreamChunk<'_>> + 'a> {
            Box::new(self.data.chunks(20).map(|data| ElementaryStreamChunk {
                start_access_unit: true,
                known_end_access_unit: true,
                data,
                significance: Significance::Video(VideoSignificance::Picture),
                timestamp: None,
            }))
        }
    }

    #[test]
    fn chunks_are_capped() {
        let stream = TimestampedStream {
            source: FakeStream { data: (0..).take(100).collect() },
            timestamps: 0..,
        };
        let chunk_size = 10;
        for (i, capped_chunk) in stream.capped_chunks(chunk_size).enumerate() {
            if i % 2 == 0 {
                // This is the first half of a capped chunk.
                assert!(capped_chunk.start_access_unit);
                assert_eq!(capped_chunk.timestamp, Some(i as u64 / 2));
            } else {
                // This is the second half of a capped chunk.
                assert!(capped_chunk.known_end_access_unit);

                // No subchunks other than the first should have a timestamp; if there is a
                // timestamp, we spend it on the first subchunk to keep it aligned.
                assert!(capped_chunk.timestamp.is_none());
            }

            for j in 0..chunk_size {
                assert_eq!(capped_chunk.data[j], (i * chunk_size + j) as u8);
            }
        }
    }
}
