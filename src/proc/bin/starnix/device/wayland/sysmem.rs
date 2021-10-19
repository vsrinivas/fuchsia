// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_sysmem as fsysmem;

// TODO(fxbug.dev/86097): Move to library that is shared with other clients.

/// The default image format constraints for allocating buffers.
pub const IMAGE_FORMAT_CONSTRAINTS_DEFAULT: fsysmem::ImageFormatConstraints =
    fsysmem::ImageFormatConstraints {
        pixel_format: fsysmem::PixelFormat {
            type_: fsysmem::PixelFormatType::Nv12,
            has_format_modifier: false,
            format_modifier: fsysmem::FormatModifier { value: 0 },
        },
        color_spaces_count: 0,
        color_space: [fsysmem::ColorSpace { type_: fsysmem::ColorSpaceType::Invalid }; 32],
        min_coded_width: 0,
        max_coded_width: 0,
        min_coded_height: 0,
        max_coded_height: 0,
        min_bytes_per_row: 0,
        max_bytes_per_row: 0,
        max_coded_width_times_coded_height: 0,
        layers: 0,
        coded_width_divisor: 0,
        coded_height_divisor: 0,
        bytes_per_row_divisor: 0,
        start_offset_divisor: 0,
        display_width_divisor: 0,
        display_height_divisor: 0,
        required_min_coded_width: 0,
        required_max_coded_width: 0,
        required_min_coded_height: 0,
        required_max_coded_height: 0,
        required_min_bytes_per_row: 0,
        required_max_bytes_per_row: 0,
    };

/// The default buffers usage for allocating buffers.
pub const BUFFER_USAGE_DEFAULT: fsysmem::BufferUsage =
    fsysmem::BufferUsage { none: 0, cpu: 0, vulkan: 0, display: 0, video: 0 };

/// The default buffer memory constraints for allocating buffers.
pub const BUFFER_MEMORY_CONSTRAINTS_DEFAULT: fsysmem::BufferMemoryConstraints =
    fsysmem::BufferMemoryConstraints {
        min_size_bytes: 0,
        max_size_bytes: u32::MAX,
        physically_contiguous_required: false,
        secure_required: false,
        ram_domain_supported: false,
        cpu_domain_supported: true,
        inaccessible_domain_supported: false,
        heap_permitted_count: 0,
        heap_permitted: [fsysmem::HeapType::SystemRam; 32],
    };

/// The default buffer collection constraints for allocating buffers.
pub const BUFFER_COLLECTION_CONSTRAINTS_DEFAULT: fsysmem::BufferCollectionConstraints =
    fsysmem::BufferCollectionConstraints {
        usage: BUFFER_USAGE_DEFAULT,
        min_buffer_count_for_camping: 0,
        min_buffer_count_for_dedicated_slack: 0,
        min_buffer_count_for_shared_slack: 0,
        min_buffer_count: 0,
        max_buffer_count: 0,
        has_buffer_memory_constraints: false,
        buffer_memory_constraints: BUFFER_MEMORY_CONSTRAINTS_DEFAULT,
        image_format_constraints_count: 0,
        image_format_constraints: [IMAGE_FORMAT_CONSTRAINTS_DEFAULT; 32],
    };

const DRM_FORMAT_ARGB8888: u32 = 0x34325241;
const DRM_FORMAT_ABGR8888: u32 = 0x34324241;
const DRM_FORMAT_XRGB8888: u32 = 0x34325258;
const DRM_FORMAT_XBGR8888: u32 = 0x34324258;

pub fn drm_format_to_sysmem_format(drm_format: u32) -> fsysmem::PixelFormatType {
    match drm_format {
        format if format == DRM_FORMAT_ARGB8888 => fsysmem::PixelFormatType::Bgra32,
        format if format == DRM_FORMAT_XRGB8888 => fsysmem::PixelFormatType::Bgra32,
        format if format == DRM_FORMAT_ABGR8888 => fsysmem::PixelFormatType::R8G8B8A8,
        format if format == DRM_FORMAT_XBGR8888 => fsysmem::PixelFormatType::R8G8B8A8,
        _ => fsysmem::PixelFormatType::Invalid,
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
