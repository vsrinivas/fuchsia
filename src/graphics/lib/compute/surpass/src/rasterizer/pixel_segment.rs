// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cmp::Ordering, fmt, mem};

use crate::{MAX_HEIGHT_SHIFT, MAX_WIDTH_SHIFT, PIXEL_WIDTH};

// Tile coordinates are signed integers stored with a bias.
// The value range goes from -1 to 2^bits - 2 inclusive.
const TILE_BIAS: i16 = 1;

pub const fn bit_field_lens<const TW: usize, const TH: usize>() -> [usize; 7] {
    const fn log2_round_up(n: usize) -> usize {
        if n.count_ones() == 1 {
            n.trailing_zeros() as usize
        } else {
            mem::size_of::<usize>() * 8 - n.leading_zeros() as usize
        }
    }

    let tile_width_shift = TW.trailing_zeros() as usize;
    let tile_height_shift = TH.trailing_zeros() as usize;

    let mut bit_field_lens = [
        MAX_HEIGHT_SHIFT - tile_height_shift,
        MAX_WIDTH_SHIFT - tile_width_shift,
        0,
        tile_width_shift,
        tile_height_shift,
        log2_round_up((PIXEL_WIDTH + 1) * 2),
        log2_round_up((PIXEL_WIDTH + 1) * 2),
    ];

    let layer_id_len = mem::size_of::<PixelSegment<TW, TH>>() * 8
        - bit_field_lens[0]
        - bit_field_lens[1]
        - bit_field_lens[3]
        - bit_field_lens[4]
        - bit_field_lens[5]
        - bit_field_lens[6];

    bit_field_lens[2] = layer_id_len;

    bit_field_lens
}

const fn shift_left_for<const TW: usize, const TH: usize>(bit_field_index: usize) -> u32 {
    let mut amount = 0;
    let mut i = 0;

    while i < bit_field_index {
        amount += bit_field_lens::<TW, TH>()[i];
        i += 1;
    }

    amount as u32
}

const fn shift_right_for<const TW: usize, const TH: usize>(bit_field_index: usize) -> u32 {
    (mem::size_of::<PixelSegment<TW, TH>>() * 8 - bit_field_lens::<TW, TH>()[bit_field_index])
        as u32
}

macro_rules! extract {
    ( $tw:expr , $th:expr , $pixel_segment:expr , $bit_field_index:expr ) => {{
        $pixel_segment << shift_left_for::<$tw, $th>($bit_field_index)
            >> shift_right_for::<$tw, $th>($bit_field_index)
    }};
}

#[derive(Clone, Copy, Default, Eq, Ord, PartialEq, PartialOrd)]
pub struct PixelSegment<const TW: usize, const TH: usize>(u64);

impl<const TW: usize, const TH: usize> PixelSegment<TW, TH> {
    const BIT_FIELD_LENGTH: [usize; 7] = bit_field_lens::<TW, TH>();

    #[inline]
    pub fn new(
        layer_id: u32,
        tile_x: i16,
        tile_y: i16,
        local_x: u8,
        local_y: u8,
        double_area_multiplier: u8,
        cover: i8,
    ) -> Self {
        let mut val = 0;

        val |= ((1 << Self::BIT_FIELD_LENGTH[0]) - 1) & (tile_y + TILE_BIAS).max(0) as u64;

        val <<= Self::BIT_FIELD_LENGTH[1];
        val |= ((1 << Self::BIT_FIELD_LENGTH[1]) - 1) as u64 & (tile_x + TILE_BIAS).max(0) as u64;

        val <<= Self::BIT_FIELD_LENGTH[2];
        val |= ((1 << Self::BIT_FIELD_LENGTH[2]) - 1) as u64 & u64::from(layer_id);

        val <<= Self::BIT_FIELD_LENGTH[3];
        val |= ((1 << Self::BIT_FIELD_LENGTH[3]) - 1) as u64 & u64::from(local_x);

        val <<= Self::BIT_FIELD_LENGTH[4];
        val |= ((1 << Self::BIT_FIELD_LENGTH[4]) - 1) as u64 & u64::from(local_y);

        val <<= Self::BIT_FIELD_LENGTH[5];
        val |= ((1 << Self::BIT_FIELD_LENGTH[5]) - 1) as u64 & u64::from(double_area_multiplier);

        val <<= Self::BIT_FIELD_LENGTH[6];
        val |= ((1 << Self::BIT_FIELD_LENGTH[6]) - 1) as u64 & cover as u64;

        Self(val)
    }

    #[inline]
    pub fn layer_id(self) -> u32 {
        extract!(TW, TH, self.0, 2) as u32
    }

    #[inline]
    pub fn tile_x(self) -> i16 {
        extract!(TW, TH, self.0, 1) as i16 - TILE_BIAS
    }

    #[inline]
    pub fn tile_y(self) -> i16 {
        extract!(TW, TH, self.0, 0) as i16 - TILE_BIAS
    }

    #[inline]
    pub fn local_x(self) -> u8 {
        extract!(TW, TH, self.0, 3) as u8
    }

    #[inline]
    pub fn local_y(self) -> u8 {
        extract!(TW, TH, self.0, 4) as u8
    }

    #[inline]
    fn double_area_multiplier(self) -> u8 {
        extract!(TW, TH, self.0, 5) as u8
    }

    #[inline]
    pub fn double_area(self) -> i16 {
        i16::from(self.double_area_multiplier()) * i16::from(self.cover())
    }

    #[inline]
    pub fn cover(self) -> i8 {
        extract!(TW, TH, self.0 as i64, 6) as i8
    }
}

impl<const TW: usize, const TH: usize> fmt::Debug for PixelSegment<TW, TH> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let unpacked: PixelSegmentUnpacked = (*self).into();
        f.debug_struct("PixelSegment")
            .field("layer_id", &unpacked.layer_id)
            .field("tile_x", &unpacked.tile_x)
            .field("tile_y", &unpacked.tile_y)
            .field("local_x", &unpacked.local_x)
            .field("local_y", &unpacked.local_y)
            .field("double_area", &unpacked.double_area)
            .field("cover", &unpacked.cover)
            .finish()
    }
}

impl<const TW: usize, const TH: usize> From<&PixelSegment<TW, TH>> for u64 {
    fn from(segment: &PixelSegment<TW, TH>) -> Self {
        segment.0
    }
}

#[inline]
pub fn search_last_by_key<F, K, const TW: usize, const TH: usize>(
    segments: &[PixelSegment<TW, TH>],
    key: K,
    mut f: F,
) -> Result<usize, usize>
where
    F: FnMut(&PixelSegment<TW, TH>) -> K,
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
        (start, len) = match f(&segments[mid]).cmp(&key) {
            Ordering::Greater => (start, half),
            _ => (mid, len - half),
        };
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

impl<const TW: usize, const TH: usize> From<PixelSegment<TW, TH>> for PixelSegmentUnpacked {
    fn from(value: PixelSegment<TW, TH>) -> Self {
        PixelSegmentUnpacked {
            layer_id: value.layer_id(),
            tile_x: value.tile_x(),
            tile_y: value.tile_y(),
            local_x: value.local_x(),
            local_y: value.local_y(),
            double_area: value.double_area(),
            cover: value.cover(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::{LAYER_LIMIT, PIXEL_DOUBLE_WIDTH, PIXEL_WIDTH, TILE_HEIGHT, TILE_WIDTH};

    #[test]
    fn pixel_segment() {
        let layer_id = 3;
        let tile_x = 4;
        let tile_y = 5;
        let local_x = 6;
        let local_y = 7;
        let double_area_multiplier = 8;
        let cover = 9;

        let pixel_segment = PixelSegment::<TILE_WIDTH, TILE_HEIGHT>::new(
            layer_id,
            tile_x,
            tile_y,
            local_x,
            local_y,
            double_area_multiplier,
            cover,
        );

        assert_eq!(pixel_segment.layer_id(), layer_id);
        assert_eq!(pixel_segment.tile_x(), tile_x);
        assert_eq!(pixel_segment.tile_y(), tile_y);
        assert_eq!(pixel_segment.local_x(), local_x);
        assert_eq!(pixel_segment.local_y(), local_y);
        assert_eq!(
            pixel_segment.double_area(),
            i16::from(double_area_multiplier) * i16::from(cover)
        );
        assert_eq!(pixel_segment.cover(), cover);
    }

    #[test]
    fn pixel_segment_max() {
        let layer_id = LAYER_LIMIT as u32;
        let tile_x = (1 << (bit_field_lens::<TILE_WIDTH, TILE_HEIGHT>()[1] - 1)) - 1;
        let tile_y = (1 << (bit_field_lens::<TILE_WIDTH, TILE_HEIGHT>()[0] - 1)) - 1;
        let local_x = (1 << bit_field_lens::<TILE_WIDTH, TILE_HEIGHT>()[4]) - 1;
        let local_y = (1 << bit_field_lens::<TILE_WIDTH, TILE_HEIGHT>()[3]) - 1;
        let double_area_multiplier = PIXEL_DOUBLE_WIDTH as u8;
        let cover = PIXEL_WIDTH as i8;

        let pixel_segment = PixelSegment::<TILE_WIDTH, TILE_HEIGHT>::new(
            layer_id,
            tile_x,
            tile_y,
            local_x,
            local_y,
            double_area_multiplier,
            cover,
        );

        assert_eq!(pixel_segment.layer_id(), layer_id);
        assert_eq!(pixel_segment.tile_x(), tile_x);
        assert_eq!(pixel_segment.tile_y(), tile_y);
        assert_eq!(pixel_segment.local_x(), local_x);
        assert_eq!(pixel_segment.local_y(), local_y);
        assert_eq!(
            pixel_segment.double_area(),
            i16::from(double_area_multiplier) * i16::from(cover)
        );
        assert_eq!(pixel_segment.cover(), cover);
    }

    #[test]
    fn pixel_segment_min() {
        let layer_id = 0;
        let tile_x = -1;
        let tile_y = -1;
        let local_x = 0;
        let local_y = 0;
        let double_area_multiplier = 0;
        let cover = -(PIXEL_WIDTH as i8);

        let pixel_segment = PixelSegment::<TILE_WIDTH, TILE_HEIGHT>::new(
            layer_id,
            tile_x,
            tile_y,
            local_x,
            local_y,
            double_area_multiplier,
            cover,
        );

        assert_eq!(pixel_segment.layer_id(), layer_id);
        assert_eq!(pixel_segment.tile_x(), -1);
        assert_eq!(pixel_segment.tile_y(), -1);
        assert_eq!(pixel_segment.local_x(), local_x);
        assert_eq!(pixel_segment.local_y(), local_y);
        assert_eq!(
            pixel_segment.double_area(),
            i16::from(double_area_multiplier) * i16::from(cover)
        );
        assert_eq!(pixel_segment.cover(), cover);
    }

    #[test]
    fn pixel_segment_clipping() {
        let tile_x = -2;
        let tile_y = -2;

        let pixel_segment =
            PixelSegment::<TILE_WIDTH, TILE_HEIGHT>::new(0, tile_x, tile_y, 0, 0, 0, 0);

        assert_eq!(pixel_segment.tile_x(), -1, "negative tile coord clipped to -1");
        assert_eq!(pixel_segment.tile_y(), -1, "negative tile coord clipped to -1");

        let tile_x = i16::MIN;
        let tile_y = i16::MIN;

        let pixel_segment =
            PixelSegment::<TILE_WIDTH, TILE_HEIGHT>::new(0, tile_x, tile_y, 0, 0, 0, 0);

        assert_eq!(pixel_segment.tile_x(), -1, "negative tile coord clipped to -1");
        assert_eq!(pixel_segment.tile_y(), -1, "negative tile coord clipped to -1");
    }

    #[test]
    fn search_last_by_key_test() {
        let size = 50;
        let segments: Vec<_> = (0..(size * 2))
            .map(|i| PixelSegment::<TILE_WIDTH, TILE_HEIGHT>::new(i / 2, 0, 0, 0, 0, 0, 0))
            .collect();
        for i in 0..size {
            assert_eq!(
                Ok((i * 2 + 1) as usize),
                search_last_by_key(segments.as_slice(), i, |ps| ps.layer_id())
            );
        }
    }
}
