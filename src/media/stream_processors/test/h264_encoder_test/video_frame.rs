// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::encoding::Decodable;
use fidl_fuchsia_media::*;
use fidl_fuchsia_sysmem as sysmem;
use fuchsia_zircon as zx;
use std::rc::Rc;
use stream_processor_test::*;

#[derive(Debug)]
pub struct VideoFrame {
    format: sysmem::ImageFormat2,
    data: Vec<u8>,
}

impl VideoFrame {
    pub fn create(format: sysmem::ImageFormat2) -> Self {
        // For 4:2:0 YUV, the UV data is 1/2 the size of the Y data,
        // so the size of the frame is 3/2 the size of the Y plane.
        let frame_size = (format.bytes_per_row * format.coded_height * 3 / 2) as usize;
        Self { format, data: vec![0; frame_size] }
    }
}

/// Generates timestamps according to a timebase and rate of playback of uncompressed video.
///
/// Since the rate is constant, this can also be used to extrapolate timestamps.
pub struct TimestampGenerator {
    timebase: u64,
    frames_per_second: usize,
}

impl TimestampGenerator {
    pub fn timestamp_at(&self, input_index: usize) -> u64 {
        let fps = self.frames_per_second as u64;
        (input_index as u64) * self.timebase / fps
    }
}

pub struct VideoFrameStream {
    pub frames: Vec<VideoFrame>,
    pub format: sysmem::ImageFormat2,
    pub encoder_settings: Rc<dyn Fn() -> EncoderSettings>,
    pub frames_per_second: usize,
    pub timebase: Option<u64>,
}

impl VideoFrameStream {
    pub fn create(
        format: sysmem::ImageFormat2,
        num_frames: usize,
        encoder_settings: Rc<dyn Fn() -> EncoderSettings>,
        frames_per_second: usize,
        timebase: Option<u64>,
    ) -> Result<Self> {
        let frames = (0..num_frames).map(|_| VideoFrame::create(format.clone())).collect();
        Ok(Self { frames, format, encoder_settings, frames_per_second, timebase })
    }

    pub fn timestamp_generator(&self) -> Option<TimestampGenerator> {
        self.timebase.map(|timebase| TimestampGenerator {
            frames_per_second: self.frames_per_second,
            timebase,
        })
    }
}

impl ElementaryStream for VideoFrameStream {
    fn format_details(&self, format_details_version_ordinal: u64) -> FormatDetails {
        FormatDetails {
            domain: Some(DomainFormat::Video(VideoFormat::Uncompressed(VideoUncompressedFormat {
                image_format: self.format.clone(),
                ..<VideoUncompressedFormat as Decodable>::new_empty()
            }))),
            encoder_settings: Some((self.encoder_settings)()),
            format_details_version_ordinal: Some(format_details_version_ordinal),
            mime_type: Some("video/h264".to_string()),
            oob_bytes: None,
            pass_through_parameters: None,
            timebase: self.timebase,
        }
    }

    fn is_access_units(&self) -> bool {
        false
    }

    fn stream<'a>(&'a self) -> Box<dyn Iterator<Item = ElementaryStreamChunk<'_>> + 'a> {
        Box::new((0..self.frames.len()).map(move |input_index| {
            ElementaryStreamChunk {
                start_access_unit: false,
                known_end_access_unit: false,
                data: &self.frames[input_index].data,
                significance: Significance::Video(VideoSignificance::Picture),
                timestamp: self
                    .timestamp_generator()
                    .as_ref()
                    .map(|timestamp_generator| timestamp_generator.timestamp_at(input_index)),
            }
        }))
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[derive(Debug, Copy, Clone)]
    struct TestSpec {
        pixel_format: sysmem::PixelFormatType,
        coded_width: usize,
        coded_height: usize,
        display_width: usize,
        display_height: usize,
        bytes_per_row: usize,
    }

    impl Into<VideoUncompressedFormat> for TestSpec {
        fn into(self) -> VideoUncompressedFormat {
            VideoUncompressedFormat {
                image_format: sysmem::ImageFormat2 {
                    pixel_format: sysmem::PixelFormat {
                        type_: self.pixel_format,
                        has_format_modifier: false,
                        format_modifier: sysmem::FormatModifier { value: 0 },
                    },
                    coded_width: self.coded_width as u32,
                    coded_height: self.coded_height as u32,
                    bytes_per_row: self.bytes_per_row as u32,
                    display_width: self.display_width as u32,
                    display_height: self.display_height as u32,
                    layers: 0,
                    color_space: sysmem::ColorSpace { type_: sysmem::ColorSpaceType::Rec709 },
                    has_pixel_aspect_ratio: false,
                    pixel_aspect_ratio_width: 0,
                    pixel_aspect_ratio_height: 0,
                },
                ..<VideoUncompressedFormat as Decodable>::new_empty()
            }
        }
    }

    #[test]
    fn stream_timestamps() {
        let test_spec = TestSpec {
            pixel_format: sysmem::PixelFormatType::Nv12,
            coded_width: 16,
            coded_height: 16,
            display_height: 12,
            display_width: 16,
            bytes_per_row: 16,
        };

        let format: VideoUncompressedFormat = test_spec.into();
        let stream = VideoFrameStream::create(
            format.image_format,
            /*num_frames=*/ 2,
            Rc::new(move || -> EncoderSettings { EncoderSettings::H264(H264EncoderSettings {}) }),
            /*frames_per_second=*/ 60,
            /*timebase=*/ Some(zx::Duration::from_seconds(1).into_nanos() as u64),
        )
        .expect("stream");
        let mut chunks = stream.stream();

        assert_eq!(chunks.next().and_then(|chunk| chunk.timestamp), Some(0));
        assert_eq!(
            chunks.next().and_then(|chunk| chunk.timestamp),
            Some(zx::Duration::from_seconds(1).into_nanos() as u64 / 60)
        );
    }
}
