// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

macro_rules! ops {
    ( @filter $slf:expr, [], [ $( $v1:ident )* ],  [ $( $v2:ident )* ] ) => {
        match $slf {
            $( Op::$v1 => stringify!($v1), )*
            $( Op::$v2(..) => stringify!($v2), )*
        }
    };

    (
        @filter $slf:expr,
        [ $var:ident ( $( $_:tt )* )  $( $tail:tt )* ],
        $v1:tt,
        [ $( $v2:tt )* ]
    ) => {
        ops!(@filter $slf, [$($tail)*], $v1, [$var $($v2)*])
    };

    ( @filter $slf:expr, [ $var:ident $( $tail:tt )* ], [ $( $v1:tt )* ], $v2:tt ) => {
        ops!(@filter $slf, [$($tail)*], [$var $($v1)*], $v2)
    };

    ( @filter $slf:expr, [ $_:tt $( $tail:tt )* ], $v1:tt, $v2:tt ) => {
        ops!(@filter $slf, [$($tail)*], $v1, $v2)
    };

    ( $( $tokens:tt )* ) => {
        /// Operations to be executed on the [tile's] registers.
        ///
        /// `CoverWipNonZero` and `CoverWipEvenOdd` use the raster provided in the [`Map::print`]
        /// call.
        ///
        /// [tile's]: crate::tile
        /// [`Map::print`]: crate::tile::Map::print
        #[derive(Clone, Copy, Debug, Eq, PartialEq)]
        pub enum Op {
            $( $tokens )*
        }

        impl Op {
            #[cfg(feature = "tracing")]
            #[inline]
            pub(crate) fn name(&self) -> &'static str {
                ops!(@filter self, [$( $tokens )*], [], [])
            }
        }
    };
}

ops! {
    /// Clears `CoverWip` with value `0`
    CoverWipZero,
    /// Fills `CoverWip` with content from [`Layer`]'s raster according to the [nonzero rule]
    ///
    /// [`Layer`]: crate::Layer
    /// [nonzero-rule]: https://en.wikipedia.org/wiki/Nonzero-rule
    CoverWipNonZero,
    /// Fills `CoverWip` with content from [`Layer`]'s raster according to the [even–odd rule]
    ///
    /// [`Layer`]: crate::Layer
    /// [even–odd rule]: https://en.wikipedia.org/wiki/Even%E2%80%93odd_rule
    CoverWipEvenOdd,
    /// Masks `CoverWip` with coverage from `CoverMask`
    CoverWipMask,
    /// Clears `CoverAcc` with value `0`
    CoverAccZero,
    /// Accumulates coverage from `CoverWip` into `CoverAcc`
    CoverAccAccumulate,
    /// Clears `CoverMask` with value `0`
    CoverMaskZero,
    /// Clears `CoverMask` with value `1`
    CoverMaskOne,
    /// Copies coverage from `CoverWip` into `CoverMask`
    CoverMaskCopyFromWip,
    /// Copies coverage from `CoverAcc` into `CoverMask`
    CoverMaskCopyFromAcc,
    /// Inverts coverage in `CoverMask`
    CoverMaskInvert,
    /// Clears `ColorWip` with value `0` (red: `0`, green: `0`, blue: `0`, alpha: `0`)
    ColorWipZero,
    /// Fills `ColorWip` with provided color according to coverage in `CoverWip`
    ColorWipFillSolid(u32),
    /// Clears `ColorAcc` with value `0` (red: `0`, green: `0`, blue: `0`, alpha: `0`)
    ColorAccZero,
    /// Blends `ColorWip` into `ColorAcc` with rule "srcOver"
    ColorAccBlendOver,
    /// Blends `ColorWip` into `ColorAcc` with rule "add"
    ColorAccBlendAdd,
    /// Blends `ColorWip` into `ColorAcc` with rule "multiply"
    ColorAccBlendMultiply,
    /// Fills `ColorAcc` with provided background color
    ColorAccBackground(u32),
}
