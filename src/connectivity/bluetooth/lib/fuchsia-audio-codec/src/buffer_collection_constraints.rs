// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_sysmem::*;

pub const IMAGE_FORMAT_CONSTRAINTS_DEFAULT: ImageFormatConstraints = ImageFormatConstraints {
    pixel_format: PixelFormat {
        type_: PixelFormatType::Nv12,
        has_format_modifier: false,
        format_modifier: FormatModifier { value: 0 },
    },
    color_spaces_count: 0,
    color_space: [ColorSpace { type_: ColorSpaceType::Invalid }; 32],
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

pub const BUFFER_MEMORY_CONSTRAINTS_DEFAULT: BufferMemoryConstraints = BufferMemoryConstraints {
    min_size_bytes: 0,
    max_size_bytes: std::u32::MAX,
    physically_contiguous_required: false,
    secure_required: false,
    ram_domain_supported: false,
    cpu_domain_supported: true,
    inaccessible_domain_supported: false,
    heap_permitted_count: 0,
    heap_permitted: [HeapType::SystemRam; 32],
};

pub const BUFFER_COLLECTION_CONSTRAINTS_DEFAULT: BufferCollectionConstraints =
    BufferCollectionConstraints {
        // Set `video` usage even though we aren't doing video, as otherwise sysmem complains about not having it set.
        usage: BufferUsage { none: 0, cpu: 1, vulkan: 0, display: 0, video: 1 },
        // Indicate we want at least one buffer available on our end at all times.
        min_buffer_count_for_camping: 1,
        min_buffer_count_for_dedicated_slack: 0,
        min_buffer_count_for_shared_slack: 0,
        min_buffer_count: 0,
        max_buffer_count: 0,
        has_buffer_memory_constraints: false,
        buffer_memory_constraints: BUFFER_MEMORY_CONSTRAINTS_DEFAULT,
        image_format_constraints_count: 0,
        image_format_constraints: [IMAGE_FORMAT_CONSTRAINTS_DEFAULT; 32],
    };
