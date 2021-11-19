// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO: Remove this once the magma file is added.
#![allow(dead_code)]

use fidl_fuchsia_sysmem as fsysmem;
use vk_sys as vk;

pub fn drm_modifier_to_sysmem_modifier(modifier: u64) -> u64 {
    match modifier {
        DRM_FORMAT_MOD_LINEAR => fsysmem::FORMAT_MODIFIER_LINEAR,
        I915_FORMAT_MOD_X_TILED => fsysmem::FORMAT_MODIFIER_INTEL_I915_X_TILED,
        I915_FORMAT_MOD_Y_TILED => fsysmem::FORMAT_MODIFIER_INTEL_I915_Y_TILED,
        I915_FORMAT_MOD_YF_TILED => fsysmem::FORMAT_MODIFIER_INTEL_I915_YF_TILED,
        _ => fsysmem::FORMAT_MODIFIER_INVALID,
    }
}

pub fn min_bytes_per_row(drm_format: u32, width: u32) -> u32 {
    match drm_format {
        format
            if format == DRM_FORMAT_ARGB8888
                || format == DRM_FORMAT_XRGB8888
                || format == DRM_FORMAT_ABGR8888
                || format == DRM_FORMAT_XBGR8888 =>
        {
            width * 4
        }
        _ => 0,
    }
}

pub fn drm_format_to_vulkan_format(drm_format: u32) -> u32 {
    match drm_format {
        DRM_FORMAT_ARGB8888 | DRM_FORMAT_XRGB8888 => vk::FORMAT_B8G8R8A8_UNORM,
        DRM_FORMAT_ABGR8888 | DRM_FORMAT_XBGR8888 => vk::FORMAT_R8G8B8A8_UNORM,
        _ => vk::FORMAT_UNDEFINED,
    }
}

pub fn drm_format_to_sysmem_format(drm_format: u32) -> fsysmem::PixelFormatType {
    match drm_format {
        DRM_FORMAT_ARGB8888 | DRM_FORMAT_XRGB8888 => fsysmem::PixelFormatType::Bgra32,
        DRM_FORMAT_ABGR8888 | DRM_FORMAT_XBGR8888 => fsysmem::PixelFormatType::R8G8B8A8,
        _ => fsysmem::PixelFormatType::Invalid,
    }
}

pub fn sysmem_modifier_to_drm_modifier(modifier: u64) -> u64 {
    match modifier {
        DRM_FORMAT_MOD_LINEAR
        | I915_FORMAT_MOD_X_TILED
        | I915_FORMAT_MOD_Y_TILED
        | I915_FORMAT_MOD_YF_TILED => modifier,
        _ => DRM_FORMAT_MOD_INVALID,
    }
}

const fn fourcc_code(a: u8, b: u8, c: u8, d: u8) -> u32 {
    let a32 = a as u32;
    let b32 = (b as u32) << 8;
    let c32 = (c as u32) << 16;
    let d32 = (d as u32) << 24;
    a32 | b32 | c32 | d32
}

const fn fourcc_mod_code(vendor: u64, value: u64) -> u64 {
    vendor << 56 | (value & 0x00ffffffffffffff)
}

const DRM_FORMAT_MOD_VENDOR_NONE: u64 = 0;
const DRM_FORMAT_MOD_VENDOR_INTEL: u64 = 0x01;

pub const DRM_FORMAT_MOD_INVALID: u64 =
    fourcc_mod_code(DRM_FORMAT_MOD_VENDOR_NONE, DRM_FORMAT_MOD_RESERVED);

const DRM_FORMAT_MOD_RESERVED: u64 = (1 << 56) - 1;
const DRM_FORMAT_MOD_LINEAR: u64 = fourcc_mod_code(DRM_FORMAT_MOD_VENDOR_NONE, 0);
const I915_FORMAT_MOD_X_TILED: u64 = fourcc_mod_code(DRM_FORMAT_MOD_VENDOR_INTEL, 1);
const I915_FORMAT_MOD_Y_TILED: u64 = fourcc_mod_code(DRM_FORMAT_MOD_VENDOR_INTEL, 2);
const I915_FORMAT_MOD_YF_TILED: u64 = fourcc_mod_code(DRM_FORMAT_MOD_VENDOR_INTEL, 3);

const DRM_FORMAT_XRGB8888: u32 = fourcc_code(b'X', b'R', b'2', b'4');
const DRM_FORMAT_XBGR8888: u32 = fourcc_code(b'X', b'B', b'2', b'4');
const DRM_FORMAT_ARGB8888: u32 = fourcc_code(b'A', b'R', b'2', b'4');
const DRM_FORMAT_ABGR8888: u32 = fourcc_code(b'A', b'B', b'2', b'4');
