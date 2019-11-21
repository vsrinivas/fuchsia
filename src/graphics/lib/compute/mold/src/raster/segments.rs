// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Raster compact representation.
//!
//! Raster segments are stored in a compact representation that can only be iterated on and has no
//! random access. However, iterations can start from arbitrary points in the collection with
//! [RasterSegment::from`, as long as the previous point can be provided.
//!
//! The segments are generated from chains of commands stored in a byte buffer in `RasterSegment`.
//! These chains always start with a **move** command and continue with one or more **segment**
//! commands.
//!
//! **move** commands are stored in 9 bytes:
//!
//! | 1 bit        | 7 bits | 4 bytes                | 4 bytes                |
//! |--------------|--------|------------------------|------------------------|
//! | command type | unused | `segment.p0.x` (`i32`) | `segment.p0.y` (`i32`) |
//!
//! **segment** commands are  stored in 2 bytes: (as `CompactDiff`)
//!
//! | 1 bit        | 3 bits | 6 bits                        | 6 bits                         |
//! |--------------|--------|-------------------------------|--------------------------------|
//! | command type | unused | `segment.p1.x - segment.p0.x` | `segment.p1.y - segment.p0.y`  |

use std::{fmt, iter::FromIterator, ops::Range};

use crate::{point::Point, segment::Segment};

const COMPACT_DIFF_MASK: i32 = 0b111111;
const COMPACT_DIFF_DX_SHIFT: u16 = 32 - COMPACT_DIFF_MASK.leading_zeros() as u16;
const COMPACT_DIFF_SHIFT_TO_I32: i32 = 32 - COMPACT_DIFF_DX_SHIFT as i32;

pub struct CompactDiff {
    value: u16,
}

impl CompactDiff {
    pub fn new(dx: i32, dy: i32) -> Self {
        let mut value = (RASTER_COMMAND_SEGMENT as u16) << 8;

        value |= ((dx & COMPACT_DIFF_MASK) as u16) << COMPACT_DIFF_DX_SHIFT;
        value |= (dy & COMPACT_DIFF_MASK) as u16;

        Self { value }
    }

    pub fn dx(&self) -> i32 {
        let shifted = self.value >> COMPACT_DIFF_DX_SHIFT;
        let dx = (shifted as i32 & COMPACT_DIFF_MASK) << COMPACT_DIFF_SHIFT_TO_I32;

        dx >> COMPACT_DIFF_SHIFT_TO_I32
    }

    pub fn dy(&self) -> i32 {
        let dy = (self.value as i32 & COMPACT_DIFF_MASK) << COMPACT_DIFF_SHIFT_TO_I32;

        dy >> COMPACT_DIFF_SHIFT_TO_I32
    }
}

const RASTER_COMMAND_MASK: u8 = 0b1000000;
const RASTER_COMMAND_MOVE: u8 = 0b0000000;
const RASTER_COMMAND_SEGMENT: u8 = 0b1000000;

pub struct RasterSegments {
    commands: Vec<u8>,
}

impl RasterSegments {
    pub const fn new() -> Self {
        Self { commands: Vec::new() }
    }

    pub fn iter(&self) -> RasterSegmentsIter {
        RasterSegmentsIter { commands: &self.commands, index: 0, previous_point: None }
    }

    pub fn from(&self, previous_point: Point<i32>, range: Range<usize>) -> RasterSegmentsIter {
        RasterSegmentsIter {
            commands: &self.commands[range],
            index: 0,
            previous_point: Some(previous_point),
        }
    }
}

impl FromIterator<Segment<i32>> for RasterSegments {
    fn from_iter<T: IntoIterator<Item = Segment<i32>>>(iter: T) -> Self {
        let mut commands = vec![];
        let mut end_point = None;

        for segment in iter {
            if end_point != Some(segment.p0) {
                commands.push(RASTER_COMMAND_MOVE);

                commands.extend(&segment.p0.x.to_be_bytes());
                commands.extend(&segment.p0.y.to_be_bytes());
            }

            let diff = CompactDiff::new(segment.p1.x - segment.p0.x, segment.p1.y - segment.p0.y);
            commands.extend(&diff.value.to_be_bytes());

            end_point = Some(segment.p1);
        }

        Self { commands }
    }
}

impl fmt::Debug for RasterSegments {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.debug_list().entries(self.iter()).finish()
    }
}

#[derive(Clone, Debug)]
pub struct RasterSegmentsIter<'c> {
    commands: &'c [u8],
    index: usize,
    previous_point: Option<Point<i32>>,
}

impl RasterSegmentsIter<'_> {
    pub fn index(&self) -> usize {
        self.index
    }
}

impl Iterator for RasterSegmentsIter<'_> {
    type Item = Segment<i32>;

    fn next(&mut self) -> Option<Self::Item> {
        if let Some(command) = self.commands.get(self.index) {
            match command & RASTER_COMMAND_MASK {
                RASTER_COMMAND_MOVE => {
                    self.index += 1;

                    let mut bytes = [0u8; 4];

                    bytes.copy_from_slice(&self.commands[self.index..self.index + 4]);
                    let x = i32::from_be_bytes(bytes);
                    self.index += 4;

                    bytes.copy_from_slice(&self.commands[self.index..self.index + 4]);
                    let y = i32::from_be_bytes(bytes);
                    self.index += 4;

                    self.previous_point = Some(Point::new(x, y));

                    self.next()
                }
                RASTER_COMMAND_SEGMENT => {
                    let mut bytes = [0u8; 2];

                    bytes.copy_from_slice(&self.commands[self.index..self.index + 2]);
                    let value = u16::from_be_bytes(bytes);
                    self.index += 2;

                    let diff = CompactDiff { value };

                    let start_point =
                        self.previous_point.expect("RASTER_COMMAND_MOVE expected as first command");
                    self.previous_point =
                        Some(Point::new(start_point.x + diff.dx(), start_point.y + diff.dy()));

                    Some(Segment::new(start_point, self.previous_point.unwrap()))
                }
                _ => unreachable!(),
            }
        } else {
            None
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::PIXEL_WIDTH;

    #[test]
    fn compact_diff_conversion() {
        let size = PIXEL_WIDTH as i32;
        for dx in -size..=size {
            for dy in -size..=size {
                let diff = CompactDiff::new(dx, dy);
                assert_eq!((diff.dx(), diff.dy()), (dx, dy));
            }
        }
    }
}
