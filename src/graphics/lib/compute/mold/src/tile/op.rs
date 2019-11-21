// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

macro_rules! ops {
    ( @filter $slf:expr, [], [ $( $v1:ident )* ],  [ $( $v2:ident )* ] ) => {
        match $slf {
            $( TileOp::$v1 => stringify!($v1), )*
            $( TileOp::$v2(..) => stringify!($v2), )*
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
        #[derive(Clone, Copy, Debug, Eq, PartialEq)]
        pub enum TileOp {
            $( $tokens )*
        }

        impl TileOp {
            #[cfg(feature = "tracing")]
            #[inline]
            pub(crate) fn name(&self) -> &'static str {
                ops!(@filter self, [$( $tokens )*], [], [])
            }
        }
    };
}

ops! {
    CoverWipZero,
    CoverWipNonZero,
    CoverWipEvenOdd,
    CoverWipMask,
    CoverAccZero,
    CoverAccAccumulate,
    CoverMaskZero,
    CoverMaskOne,
    CoverMaskCopyFromWip,
    CoverMaskCopyFromAcc,
    CoverMaskInvert,
    ColorWipZero,
    ColorWipFillSolid(u32),
    ColorAccZero,
    ColorAccBlendOver,
    ColorAccBlendAdd,
    ColorAccBlendMultiply,
    ColorAccBackground(u32),
}
