// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cmp::Ordering, convert::TryFrom, mem};

use crate::TILE_SHIFT;

const NONE: u64 = 1 << 63;

pub const BIT_FIELD_LENS: [usize; 8] =
    [1, 15 - TILE_SHIFT, 16 - TILE_SHIFT, 20, TILE_SHIFT, TILE_SHIFT, 6, 6];

const fn mask_for(bit_field_index: usize) -> u64 {
    ((1 << BIT_FIELD_LENS[bit_field_index]) - 1) as u64
}

const fn shift_left_for(bit_field_index: usize) -> u32 {
    let mut amount = 0;
    let mut i = 0;

    while i < bit_field_index {
        amount += BIT_FIELD_LENS[i];
        i += 1;
    }

    amount as u32
}

const fn shift_right_for(bit_field_index: usize) -> u32 {
    (mem::size_of::<PixelSegment>() * 8 - BIT_FIELD_LENS[bit_field_index]) as u32
}

macro_rules! extract {
    ( $pixel_segment:expr , $bit_field_index:expr ) => {{
        $pixel_segment << shift_left_for($bit_field_index) >> shift_right_for($bit_field_index)
    }};
}

#[derive(Clone, Copy, Eq, Ord, PartialEq, PartialOrd, Debug)]
pub struct PixelSegment(u64);

impl PixelSegment {
    pub fn new(
        is_none: bool,
        tile_y: i16,
        tile_x: i16,
        layer_id: u32,
        local_y: u8,
        local_x: u8,
        double_area_multiplier: u8,
        cover: i8,
    ) -> Self {
        let mut val = 0;

        val |= mask_for(0) & is_none as u64;

        val <<= BIT_FIELD_LENS[1];
        val |= mask_for(1) & tile_y as u64;

        val <<= BIT_FIELD_LENS[2];
        val |= mask_for(2) & tile_x as u64;

        val <<= BIT_FIELD_LENS[3];
        val |= mask_for(3) & layer_id as u64;

        val <<= BIT_FIELD_LENS[4];
        val |= mask_for(4) & local_y as u64;

        val <<= BIT_FIELD_LENS[5];
        val |= mask_for(5) & local_x as u64;

        val <<= BIT_FIELD_LENS[6];
        val |= mask_for(6) & double_area_multiplier as u64;

        val <<= BIT_FIELD_LENS[7];
        val |= mask_for(7) & cover as u64;

        Self(val)
    }

    pub fn double_area(self) -> i16 {
        self.double_area_multiplier() as i16 * self.cover() as i16
    }

    pub fn is_none(self) -> bool {
        extract!(self.0, 0) == 0b1
    }

    pub fn tile_y(self) -> i16 {
        extract!(self.0 as i64, 1) as i16
    }

    pub fn tile_x(self) -> i16 {
        extract!(self.0 as i64, 2) as i16
    }

    pub fn layer_id(self) -> u32 {
        extract!(self.0, 3) as u32
    }

    pub fn local_y(self) -> u8 {
        extract!(self.0, 4) as u8
    }

    pub fn local_x(self) -> u8 {
        extract!(self.0, 5) as u8
    }

    fn double_area_multiplier(self) -> u8 {
        extract!(self.0, 6) as u8
    }

    pub fn cover(self) -> i8 {
        extract!(self.0 as i64, 7) as i8
    }
}

impl Default for PixelSegment {
    fn default() -> Self {
        PixelSegment(NONE)
    }
}

impl Into<u64> for &PixelSegment {
    fn into(self) -> u64 {
        self.0
    }
}

#[inline]
pub fn search_last_by_key<F, K>(segments: &[PixelSegment], key: K, mut f: F) -> Result<usize, usize>
where
    F: FnMut(&PixelSegment) -> K,
    K: Ord,
{
    let mut len = segments.len();
    if len == 0 {
        return Err(0);
    }

    let mut start = 0;
    while len > 1 {
        let half = len / 2;
        let mid = start + half;

        start = match f(&segments[mid]).cmp(&key) {
            Ordering::Greater => start,
            _ => mid,
        };
        len -= half;
    }

    match f(&segments[start]).cmp(&key) {
        Ordering::Less => Err(start + 1),
        Ordering::Equal => Ok(start),
        Ordering::Greater => Err(start),
    }
}

#[derive(Debug)]
pub struct PixelSegmentUnpacked {
    pub layer_id: u32,
    pub tile_x: i16,
    pub tile_y: i16,
    pub local_x: u8,
    pub local_y: u8,
    pub double_area: i16,
    pub cover: i8,
}

impl TryFrom<PixelSegment> for PixelSegmentUnpacked {
    type Error = ();

    fn try_from(value: PixelSegment) -> Result<Self, Self::Error> {
        if value.is_none() {
            Err(())
        } else {
            Ok(PixelSegmentUnpacked {
                layer_id: value.layer_id(),
                tile_x: value.tile_x(),
                tile_y: value.tile_y(),
                local_x: value.local_x(),
                local_y: value.local_y(),
                double_area: value.double_area(),
                cover: value.cover(),
            })
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{PIXEL_DOUBLE_WIDTH, PIXEL_WIDTH};

    #[test]
    fn pixel_segment_max() {
        let is_none = true;
        let tile_y = (1 << (BIT_FIELD_LENS[1] - 1)) - 1;
        let tile_x = (1 << (BIT_FIELD_LENS[2] - 1)) - 1;
        let layer_id = (1 << 20) - 1;
        let local_y = (1 << BIT_FIELD_LENS[4]) - 1;
        let local_x = (1 << BIT_FIELD_LENS[5]) - 1;
        let double_area_multiplier = PIXEL_DOUBLE_WIDTH as u8;
        let cover = PIXEL_WIDTH as i8;

        let pixel_segment = PixelSegment::new(
            is_none,
            tile_y,
            tile_x,
            layer_id,
            local_y,
            local_x,
            double_area_multiplier,
            cover,
        );

        assert_eq!(pixel_segment.is_none(), is_none);
        assert_eq!(pixel_segment.tile_y(), tile_y);
        assert_eq!(pixel_segment.tile_x(), tile_x);
        assert_eq!(pixel_segment.layer_id(), layer_id);
        assert_eq!(pixel_segment.local_y(), local_y);
        assert_eq!(pixel_segment.local_x(), local_x);
        assert_eq!(pixel_segment.double_area(), double_area_multiplier as i16 * cover as i16);
        assert_eq!(pixel_segment.cover(), cover);
    }

    #[test]
    fn pixel_segment_min() {
        let is_none = false;
        let tile_y = -(1 << (BIT_FIELD_LENS[1] - 1));
        let tile_x = -(1 << (BIT_FIELD_LENS[2] - 1));
        let layer_id = 0;
        let local_y = 0;
        let local_x = 0;
        let double_area_multiplier = 0;
        let cover = -(PIXEL_WIDTH as i8);

        let pixel_segment = PixelSegment::new(
            is_none,
            tile_y,
            tile_x,
            layer_id,
            local_y,
            local_x,
            double_area_multiplier,
            cover,
        );

        assert_eq!(pixel_segment.is_none(), is_none);
        assert_eq!(pixel_segment.tile_y(), tile_y);
        assert_eq!(pixel_segment.tile_x(), tile_x);
        assert_eq!(pixel_segment.layer_id(), layer_id);
        assert_eq!(pixel_segment.local_y(), local_y);
        assert_eq!(pixel_segment.local_x(), local_x);
        assert_eq!(pixel_segment.double_area(), double_area_multiplier as i16 * cover as i16);
        assert_eq!(pixel_segment.cover(), cover);
    }
}
