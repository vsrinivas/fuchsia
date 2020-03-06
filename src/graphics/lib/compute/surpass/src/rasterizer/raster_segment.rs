// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cmp::Ordering;

use crate::TILE_SHIFT;

const NONE: u64 = 1 << 63;

macro_rules! bitfield {
    ( @shift_or $type:ty, $val:ident, $shift:expr, [ $field:ident ], [ $size:expr $( , )* ] ) => {
        $val.0 <<= $shift;
        $val.0 |= $field as $type & (1 << $size) - 1;
    };

    (
        @shift_or
        $type:ty,
        $val:ident,
        $shift:expr,
        [ $field:ident , $( $fields_tail:ident ),* ],
        [ $size:expr , $next_size:expr , $( $sizes_tail:expr ),* $( , )* ]
    ) => {
        $val.0 <<= $shift;
        $val.0 |= $field as $type & (1 << $size) - 1;
        bitfield!(
            @shift_or
            $type,
            $val,
            $next_size,
            [$($fields_tail),*],
            [$next_size, $($sizes_tail),*,]
        );
    };

    ( @add [ $size:expr ] ) => ($size);

    ( @add [ $size:expr , $( $sizes_tail:expr ),* ] ) => {{
        $size + bitfield!(@add [$($sizes_tail),*])
    }};

    ( @getter [ $field:ident ], [ $type:ty ], [ $size:expr ] ) => {
        #[inline]
        pub fn $field(self) -> $type {
            let sign_fix_shift = ::std::mem::size_of::<$type>() * 8 - $size;
            let mask = (1 << $size) - 1;
            let val = (self.0 & mask) as $type;

            val << sign_fix_shift >> sign_fix_shift
        }
    };

    (
        @getter
        [ $field:ident , $( $fields_tail:ident ),* ],
        [ $type:ty , $( $types_tail:ty ),* ],
        [ $size:expr , $( $sizes_tail:expr ),* ]
    ) => {
        #[inline]
        pub fn $field(self) -> $type {
            let shift = bitfield!(@add [$($sizes_tail),*]);
            let sign_fix_shift = ::std::mem::size_of::<$type>() * 8 - $size;
            let mask = (1 << $size) - 1;
            let val = (self.0 >> shift & mask) as $type;

            val << sign_fix_shift >> sign_fix_shift
        }

        bitfield!(@getter [$($fields_tail),*], [$($types_tail),*], [$($sizes_tail),*]);
    };

    (
        @impl
        $name:ident,
        $type:ty,
        [ $( $fields:ident ),* ],
        [ $( $types:ty ),* ],
        [ $( $sizes:expr ),* ]
    ) => {
        impl $name {
            #[inline]
            pub fn new(
                $($fields: $types),*
            ) -> Self {
                let mut val = $name(<$type>::default());

                bitfield!(@shift_or $type, val, 0, [$($fields),*], [$($sizes),*]);

                val
            }

            #[inline]
            pub fn unwrap(self) -> $type {
                self.0
            }

            bitfield!(@getter [$($fields),*], [$($types),*], [$($sizes),*]);
        }

        impl ::std::fmt::Debug for $name {
            fn fmt(&self, f: &mut ::std::fmt::Formatter<'_>) -> ::std::fmt::Result {
                f.debug_struct(stringify!($name))
                $(
                    .field(stringify!($fields), &self.$fields())
                )*
                   .finish()
            }
        }
    };

    (
        $( #[ $( $meta:meta )* ] )?
        struct $name:ident ( $type:ty ) {
            $( $fields:ident : $field_types:ty [ $sizes:expr ] ),*
            $( , )?
        }
    ) => {
        $(#[$($meta)*])?
        struct $name($type);

        bitfield!(@impl $name, $type, [$($fields),*], [$($field_types),*], [$($sizes),*]);
    };

    (
        $( #[ $( $meta:meta )* ] )?
        pub struct $name:ident ( $type:ty ) {
            $( $fields:ident : $field_types:ty [ $sizes:expr ] ),*
            $( , )?
        }
    ) => {
        $(#[$($meta)*])?
        pub struct $name($type);

        bitfield!(@impl $name, $type, [$($fields),*], [$($field_types),*], [$($sizes),*]);
    };
}

bitfield! {
    #[derive(Clone, Copy, Eq, Ord, PartialEq, PartialOrd)]
    pub struct CompactSegment(u64) {
        is_none: u8[1],
        tile_j: i16[15 - TILE_SHIFT],
        tile_i: i16[16 - TILE_SHIFT],
        layer: u16[16],
        tile_y: u8[TILE_SHIFT],
        tile_x: u8[TILE_SHIFT],
        area: i16[10],
        cover: i8[6],
    }
}

impl Default for CompactSegment {
    fn default() -> Self {
        CompactSegment(NONE)
    }
}

impl Into<Option<RasterSegment>> for CompactSegment {
    fn into(self) -> Option<RasterSegment> {
        if self.is_none() == 1 {
            None
        } else {
            Some(RasterSegment {
                layer: self.layer(),
                tile_i: self.tile_i(),
                tile_j: self.tile_j(),
                tile_x: self.tile_x(),
                tile_y: self.tile_y(),
                area: self.area(),
                cover: self.cover(),
            })
        }
    }
}

#[inline]
pub fn search_last_by_key<F, K>(
    segments: &[CompactSegment],
    key: K,
    mut f: F,
) -> Result<usize, usize>
where
    F: FnMut(&CompactSegment) -> K,
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
pub struct RasterSegment {
    pub layer: u16,
    pub tile_i: i16,
    pub tile_j: i16,
    pub tile_x: u8,
    pub tile_y: u8,
    pub area: i16,
    pub cover: i8,
}
