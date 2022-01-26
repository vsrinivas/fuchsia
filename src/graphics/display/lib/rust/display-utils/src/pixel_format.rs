// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_upper_case_globals)]

use {fidl_fuchsia_sysmem as sysmem, std::fmt};

// TODO(fxbug.dev/85320): This module is intended to provide some amount of compatibility between
// the sysmem pixel format (the canonical Fuchsia image format) and zx_pixel_format_t (which is
// used by the display driver stack). The `PixelFormat` type defined here suffers from the same
// component-endianness confusion as ZX_PIXEL_FORMAT_* values. Most of this module can be removed
// when the display API is changed to be in terms of sysmem image formats instead.

/// Pixel format definitions that are compatible with the display and GPU drivers' internal image
/// type representation. These are distinct from sysmem image formats and are intended to be
/// compatible with the ZX_PIXEL_FORMAT_* C definitions that are declared in
/// //zircon/system/public/zircon/pixelformat.h.
///
/// NOTE: The color-components are interpreted in memory in little-endian byte-order.
/// E.g., PixelFormat::Argb8888 as [u8; 4] would have components [blue, green, red, alpha].
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum PixelFormat {
    /// Default invalid image format value compatible with `ZX_PIXEL_FORMAT_NONE`.
    Unknown,
    /// 8-bit luminance-only. Identical to Gray8
    Mono8,
    /// 8-bit luminance-only. Identical to Mono8
    Gray8,
    /// 8-bit RGB 3/3/2
    Rgb332,
    /// 8-bit RGB 2/2/2
    Rgb2220,
    /// 16-bit RGB 5/6/5
    Rgb565,
    /// 24-bit RGB
    Rgb888,
    /// 32-bit BGR with ignored Alpha
    Bgr888X,
    /// 32-bit RGB with ignored Alpha
    RgbX888,
    /// 32-bit ARGB 8/8/8/8
    Argb8888,
    /// 32-bit ABGR
    Abgr8888,
    /// Bi-planar YUV (YCbCr) with 8-bit Y plane followed by an interleaved U/V plane with 2x2
    /// subsampling
    Nv12,
}

/// Alias for zx_pixel_format_t
#[allow(non_camel_case_types)]
pub type zx_pixel_format_t = u32;

/// See //zircon/system/public/zircon/pixelformat.h.
const ZX_PIXEL_FORMAT_NONE: zx_pixel_format_t = 0;
const ZX_PIXEL_FORMAT_RGB_565: zx_pixel_format_t = 0x00020001;
const ZX_PIXEL_FORMAT_RGB_332: zx_pixel_format_t = 0x00010002;
const ZX_PIXEL_FORMAT_RGB_2220: zx_pixel_format_t = 0x00010003;
const ZX_PIXEL_FORMAT_ARGB_8888: zx_pixel_format_t = 0x00040004;
const ZX_PIXEL_FORMAT_RGB_x888: zx_pixel_format_t = 0x00040005;
const ZX_PIXEL_FORMAT_MONO_8: zx_pixel_format_t = 0x00010007;
const ZX_PIXEL_FORMAT_GRAY_8: zx_pixel_format_t = 0x00010007;
const ZX_PIXEL_FORMAT_NV12: zx_pixel_format_t = 0x00010008;
const ZX_PIXEL_FORMAT_RGB_888: zx_pixel_format_t = 0x00030009;
const ZX_PIXEL_FORMAT_ABGR_8888: zx_pixel_format_t = 0x0004000a;
const ZX_PIXEL_FORMAT_BGR_888x: zx_pixel_format_t = 0x0004000b;

impl Default for PixelFormat {
    fn default() -> PixelFormat {
        PixelFormat::Unknown
    }
}

impl From<u32> for PixelFormat {
    fn from(src: u32) -> Self {
        match src {
            ZX_PIXEL_FORMAT_NONE => PixelFormat::Unknown,
            ZX_PIXEL_FORMAT_RGB_565 => PixelFormat::Rgb565,
            ZX_PIXEL_FORMAT_RGB_332 => PixelFormat::Rgb332,
            ZX_PIXEL_FORMAT_RGB_2220 => PixelFormat::Rgb2220,
            ZX_PIXEL_FORMAT_ARGB_8888 => PixelFormat::Argb8888,
            ZX_PIXEL_FORMAT_RGB_x888 => PixelFormat::RgbX888,
            ZX_PIXEL_FORMAT_MONO_8 => PixelFormat::Mono8,
            // ZX_PIXEL_FORMAT_GRAY_8 is an alias for ZX_PIXEL_FORMAT_MONO_8
            ZX_PIXEL_FORMAT_NV12 => PixelFormat::Nv12,
            ZX_PIXEL_FORMAT_RGB_888 => PixelFormat::Rgb888,
            ZX_PIXEL_FORMAT_ABGR_8888 => PixelFormat::Abgr8888,
            ZX_PIXEL_FORMAT_BGR_888x => PixelFormat::Bgr888X,
            _ => PixelFormat::Unknown,
        }
    }
}

impl From<&zx_pixel_format_t> for PixelFormat {
    fn from(src: &zx_pixel_format_t) -> Self {
        Self::from(*src)
    }
}

impl From<PixelFormat> for zx_pixel_format_t {
    fn from(src: PixelFormat) -> Self {
        match src {
            PixelFormat::Unknown => ZX_PIXEL_FORMAT_NONE,
            PixelFormat::Mono8 => ZX_PIXEL_FORMAT_MONO_8,
            PixelFormat::Gray8 => ZX_PIXEL_FORMAT_GRAY_8,
            PixelFormat::Rgb332 => ZX_PIXEL_FORMAT_RGB_332,
            PixelFormat::Rgb2220 => ZX_PIXEL_FORMAT_RGB_2220,
            PixelFormat::Rgb565 => ZX_PIXEL_FORMAT_RGB_565,
            PixelFormat::Rgb888 => ZX_PIXEL_FORMAT_RGB_888,
            PixelFormat::Bgr888X => ZX_PIXEL_FORMAT_BGR_888x,
            PixelFormat::RgbX888 => ZX_PIXEL_FORMAT_RGB_x888,
            PixelFormat::Abgr8888 => ZX_PIXEL_FORMAT_ABGR_8888,
            PixelFormat::Argb8888 => ZX_PIXEL_FORMAT_ARGB_8888,
            PixelFormat::Nv12 => ZX_PIXEL_FORMAT_NV12,
        }
    }
}

impl From<&PixelFormat> for zx_pixel_format_t {
    fn from(src: &PixelFormat) -> Self {
        Self::from(*src)
    }
}

impl From<PixelFormat> for sysmem::PixelFormatType {
    fn from(src: PixelFormat) -> Self {
        match src {
            PixelFormat::Unknown => Self::Invalid,
            PixelFormat::Mono8 | PixelFormat::Gray8 => Self::L8,
            PixelFormat::Rgb332 => Self::Rgb332,
            PixelFormat::Rgb2220 => Self::Rgb2220,
            PixelFormat::Rgb565 => Self::Rgb565,
            PixelFormat::Rgb888 => Self::Bgr24,
            PixelFormat::Abgr8888 | PixelFormat::Bgr888X => Self::R8G8B8A8,
            PixelFormat::Argb8888 | PixelFormat::RgbX888 => Self::Bgra32,
            PixelFormat::Nv12 => Self::Nv12,
        }
    }
}

impl fmt::Display for PixelFormat {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let string: &str = match *self {
            PixelFormat::Unknown => "(unknown)",
            PixelFormat::Mono8 => "Mono 8-bit",
            PixelFormat::Gray8 => "Gray 8-bit",
            PixelFormat::Rgb332 => "RGB 332",
            PixelFormat::Rgb2220 => "RGB 2220",
            PixelFormat::Rgb565 => "RGB 565",
            PixelFormat::Rgb888 => "RGB 888",
            PixelFormat::Bgr888X => "BGR 888x",
            PixelFormat::RgbX888 => "RGB x888",
            PixelFormat::Abgr8888 => "ABGR 8888",
            PixelFormat::Argb8888 => "ARGB 8888",
            PixelFormat::Nv12 => "NV12",
        };
        write!(f, "{}", string)
    }
}
