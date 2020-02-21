// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod test_suite;
mod video_frame;

use crate::test_suite::*;
use fidl_fuchsia_media::*;
use fidl_fuchsia_sysmem as sysmem;
use fuchsia_async as fasync;
use std::rc::Rc;
use stream_processor_test::*;

#[test]
fn h264_stream_init() -> Result<()> {
    let test_case = H264EncoderTestCase {
        input_format: sysmem::ImageFormat2 {
            pixel_format: sysmem::PixelFormat {
                type_: sysmem::PixelFormatType::Nv12,
                has_format_modifier: false,
                format_modifier: sysmem::FormatModifier { value: 0 },
            },
            coded_width: 16,
            coded_height: 16,
            bytes_per_row: 16,
            display_width: 16,
            display_height: 12,
            layers: 0,
            color_space: sysmem::ColorSpace { type_: sysmem::ColorSpaceType::Rec709 },
            has_pixel_aspect_ratio: false,
            pixel_aspect_ratio_width: 0,
            pixel_aspect_ratio_height: 0,
        },
        num_frames: 0,
        settings: Rc::new(move || -> EncoderSettings {
            EncoderSettings::H264(H264EncoderSettings {})
        }),
    };

    fasync::Executor::new().unwrap().run_singlethreaded(test_case.run())
}
