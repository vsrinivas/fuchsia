// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_media::FormatDetails;

pub trait ElementaryStream {
    fn format_details(&self, version_ordinal: u64) -> FormatDetails;

    /// Whether _all_ chunks in the elementary stream will be on access unit boundaries. These are
    /// units for decoder input (e.g. in H264, NALs). When input is not in access units, the server
    /// must parse and/or buffer the bitstream.
    fn is_access_units(&self) -> bool;

    fn stream<'a>(&'a self) -> Box<dyn Iterator<Item = ElementaryStreamChunk> + 'a>;

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
}

#[derive(Copy, Clone, Debug)]
pub enum VideoSignificance {
    Picture,
    NotPicture,
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

    fn stream<'a>(&'a self) -> Box<dyn Iterator<Item = ElementaryStreamChunk> + 'a> {
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
