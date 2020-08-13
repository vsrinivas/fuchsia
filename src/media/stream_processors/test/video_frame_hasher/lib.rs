// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Hashes video frames.

use async_trait::async_trait;
use fidl_fuchsia_media::*;
use fidl_fuchsia_sysmem as sysmem;
use hex::encode;
use mundane::hash::{Digest, Hasher, Sha256};
use std::{convert::*, fmt};
use stream_processor_test::{ExpectedDigest, FatalError, Output, OutputPacket, OutputValidator};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum Error {
    DataTooSmallToBeFrame { expected_size: usize, actual_size: usize },
    FormatDetailsNotUncompressedVideo,
    UnsupportedPixelFormat(sysmem::PixelFormatType),
    FormatHasNoPlane { plane_requested: usize, planes_in_format: usize },
}

impl fmt::Display for Error {
    fn fmt(&self, w: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Debug::fmt(&self, w)
    }
}

struct Frame<'a> {
    data: &'a [u8],
    format: sysmem::ImageFormat2,
}

trait Subsample420 {
    const CHROMA_SUBSAMPLE_RATIO: usize = 2;

    fn frame_size(format: &sysmem::ImageFormat2) -> usize {
        // For 4:2:0 YUV, the UV data is 1/2 the size of the Y data,
        // so the size of the frame is 3/2 the size of the Y plane.
        (format.bytes_per_row * format.coded_height * 3 / 2) as usize
    }
}

struct Yv12Frame<'a> {
    frame: Frame<'a>,
}

impl<'a> Subsample420 for Yv12Frame<'a> {}

impl<'a> Yv12Frame<'a> {
    /// Returns an iterator over the display data in Yv12 format.
    fn yv12_iter(&self) -> impl Iterator<Item = u8> + 'a {
        let luminance_plane_stride = self.frame.format.bytes_per_row as usize;
        let luminance_plane_display_height = self.frame.format.display_height as usize;
        let luminance_plane_display_width = self.frame.format.display_width as usize;
        let luminance = self
            .frame
            .data
            .chunks(luminance_plane_stride)
            .take(luminance_plane_display_height)
            .flat_map(move |row| row.iter().take(luminance_plane_display_width));

        let luminance_plane_coded_size =
            luminance_plane_stride * (self.frame.format.coded_height as usize);
        let chroma_plane_display_height =
            luminance_plane_display_height / Self::CHROMA_SUBSAMPLE_RATIO;
        let chroma_plane_display_width =
            luminance_plane_display_width / Self::CHROMA_SUBSAMPLE_RATIO;

        // V rows followed by U rows.
        let chroma_rows = self.frame.data[luminance_plane_coded_size..]
            .chunks(luminance_plane_stride / Self::CHROMA_SUBSAMPLE_RATIO);

        let chroma_v = chroma_rows
            .clone()
            .take(chroma_plane_display_height)
            .flat_map(move |row| row.iter().take(chroma_plane_display_width));

        let chroma_plane_coded_height =
            self.frame.format.coded_height as usize / Self::CHROMA_SUBSAMPLE_RATIO;
        let chroma_u = chroma_rows
            .clone()
            .skip(chroma_plane_coded_height)
            .take(chroma_plane_display_height)
            .flat_map(move |row| row.iter().take(chroma_plane_display_width));

        luminance.chain(chroma_v).chain(chroma_u).cloned()
    }
}

impl<'a> TryFrom<Frame<'a>> for Yv12Frame<'a> {
    type Error = Error;
    fn try_from(frame: Frame<'a>) -> Result<Self, Self::Error> {
        let expected_size = Self::frame_size(&frame.format);
        if frame.data.len() < expected_size {
            return Err(Error::DataTooSmallToBeFrame {
                actual_size: frame.data.len(),
                expected_size,
            });
        }

        Ok(Yv12Frame { frame })
    }
}

struct Nv12Frame<'a> {
    frame: Frame<'a>,
}

impl<'a> Subsample420 for Nv12Frame<'a> {}

impl<'a> Nv12Frame<'a> {
    /// Returns an iterator over the display data in Yv12 format.
    fn yv12_iter(&self) -> impl Iterator<Item = u8> + 'a {
        let rows = self.frame.data.chunks(self.frame.format.bytes_per_row as usize);
        let luminance_row_count = self.frame.format.display_height as usize;
        let chroma_row_count = luminance_row_count / Self::CHROMA_SUBSAMPLE_RATIO;
        let row_length = self.frame.format.display_width as usize;

        let luminance =
            rows.clone().take(luminance_row_count).flat_map(move |row| row.iter().take(row_length));
        let chroma_rows = rows.skip(self.frame.format.coded_height as usize).take(chroma_row_count);
        let chroma_u =
            chroma_rows.clone().flat_map(move |row| row.iter().take(row_length).step_by(2));
        let chroma_v =
            chroma_rows.flat_map(move |row| row.iter().take(row_length).skip(1).step_by(2));

        luminance.chain(chroma_v).chain(chroma_u).cloned()
    }
}

impl<'a> TryFrom<Frame<'a>> for Nv12Frame<'a> {
    type Error = Error;
    fn try_from(frame: Frame<'a>) -> Result<Self, Self::Error> {
        let expected_size = Self::frame_size(&frame.format);
        if frame.data.len() < expected_size {
            return Err(Error::DataTooSmallToBeFrame {
                actual_size: frame.data.len(),
                expected_size,
            });
        }

        Ok(Nv12Frame { frame })
    }
}

fn packet_display_data<'a>(
    src: &'a OutputPacket,
) -> Result<Box<dyn Iterator<Item = u8> + 'a>, Error> {
    let format = src
        .format
        .format_details
        .domain
        .as_ref()
        .and_then(|domain| match domain {
            DomainFormat::Video(VideoFormat::Uncompressed(uncompressed_format)) => {
                Some(uncompressed_format.image_format)
            }
            _ => None,
        })
        .ok_or(Error::FormatDetailsNotUncompressedVideo)?;

    Ok(match format.pixel_format.type_ {
        sysmem::PixelFormatType::Yv12 => {
            Box::new(Yv12Frame::try_from(Frame { data: src.data.as_slice(), format })?.yv12_iter())
        }
        sysmem::PixelFormatType::Nv12 => {
            Box::new(Nv12Frame::try_from(Frame { data: src.data.as_slice(), format })?.yv12_iter())
        }
        _ => Err(Error::UnsupportedPixelFormat(format.pixel_format.type_))?,
    })
}

pub struct VideoFrameHasher {
    pub expected_digest: ExpectedDigest,
}

#[async_trait(?Send)]
impl OutputValidator for VideoFrameHasher {
    async fn validate(&self, output: &[Output]) -> Result<(), anyhow::Error> {
        let mut hasher = Sha256::default();

        output
            .iter()
            .map(|output| {
                if let Output::Packet(ref packet) = output {
                    packet_display_data(packet)?.for_each(|b| hasher.update(&[b]));
                }
                Ok(())
            })
            .collect::<Result<(), Error>>()?;

        let digest = hasher.finish().bytes();
        if self.expected_digest.bytes != digest {
            return Err(FatalError(format!(
                "Expected {}; got {}",
                self.expected_digest,
                encode(digest)
            ))
            .into());
        }

        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::{Error, *};
    use fidl::encoding::Decodable;
    use fidl_fuchsia_sysmem::{ColorSpace, PixelFormat, *};
    use fuchsia_stream_processors::{ValidPacket, ValidPacketHeader};
    use rand::prelude::*;
    use std::rc::Rc;
    use stream_processor_test::ValidStreamOutputFormat;

    #[derive(Debug, Copy, Clone)]
    struct TestSpec {
        pixel_format: PixelFormatType,
        coded_width: usize, // coded_height() is in the impl
        display_width: usize,
        display_height: usize,
        bytes_per_row: usize,
    }

    impl TestSpec {
        fn coded_height(&self) -> usize {
            // This just makes sure that coded_height() is > height.
            self.display_height * 2
        }
    }

    impl Into<OutputPacket> for TestSpec {
        fn into(self) -> OutputPacket {
            let mut format_details = <FormatDetails as Decodable>::new_empty();
            format_details.domain =
                Some(DomainFormat::Video(VideoFormat::Uncompressed(VideoUncompressedFormat {
                    image_format: ImageFormat2 {
                        pixel_format: PixelFormat {
                            type_: self.pixel_format,
                            has_format_modifier: false,
                            format_modifier: FormatModifier { value: 0 },
                        },
                        coded_width: self.coded_width as u32,
                        coded_height: self.coded_height() as u32,
                        bytes_per_row: self.bytes_per_row as u32,
                        display_width: self.display_width as u32,
                        display_height: self.display_height as u32,
                        layers: 0,
                        color_space: ColorSpace { type_: ColorSpaceType::Rec709 },
                        has_pixel_aspect_ratio: false,
                        pixel_aspect_ratio_width: 0,
                        pixel_aspect_ratio_height: 0,
                    },
                    ..blank_uncompressed_format()
                })));
            OutputPacket {
                data: vec![0; self.bytes_per_row * self.coded_height() * 3 / 2],
                format: Rc::new(ValidStreamOutputFormat {
                    stream_lifetime_ordinal: 0,
                    format_details,
                }),
                packet: ValidPacket {
                    header: ValidPacketHeader { buffer_lifetime_ordinal: 0, packet_index: 0 },
                    buffer_index: 0,
                    stream_lifetime_ordinal: 0,
                    start_offset: 0,
                    valid_length_bytes: 0,
                    timestamp_ish: None,
                    start_access_unit: false,
                    known_end_access_unit: false,
                },
            }
        }
    }

    fn blank_uncompressed_format() -> VideoUncompressedFormat {
        <VideoUncompressedFormat as Decodable>::new_empty()
    }

    fn specs(pixel_format: PixelFormatType) -> impl Iterator<Item = TestSpec> {
        vec![
            TestSpec {
                pixel_format,
                coded_width: 16,
                display_width: 10,
                display_height: 16,
                bytes_per_row: 20,
            },
            TestSpec {
                pixel_format,
                coded_width: 16,
                display_width: 14,
                display_height: 8,
                bytes_per_row: 16,
            },
            TestSpec {
                pixel_format,
                coded_width: 32,
                display_width: 12,
                display_height: 4,
                bytes_per_row: 54,
            },
            TestSpec {
                pixel_format,
                coded_width: 16,
                display_width: 2,
                display_height: 100,
                bytes_per_row: 1200,
            },
        ]
        .into_iter()
    }

    #[test]
    fn packets_of_different_formats_hash_same_with_matching_data() -> Result<(), Error> {
        let mut rng = StdRng::seed_from_u64(45);

        for (nv12_spec, yv12_spec) in specs(PixelFormatType::Nv12).zip(specs(PixelFormatType::Yv12))
        {
            // Initialize two packets with random data. Then set every display byte to
            // `global_index % 256`, where `global_index` is a count of every display byte so far
            // in the frame.

            let mut nv12_packet: OutputPacket = nv12_spec.into();
            rng.fill(nv12_packet.data.as_mut_slice());

            let mut nv12_global_index: usize = 0;
            let mut assign_index = |b: &mut u8| {
                *b = (nv12_global_index % 256) as u8;
                nv12_global_index += 1;
            };

            // NV12 Luminance plane.
            for row in
                nv12_packet.data.chunks_mut(nv12_spec.bytes_per_row).take(nv12_spec.display_height)
            {
                row.iter_mut().take(nv12_spec.display_width).for_each(&mut assign_index);
            }

            // NV12 Chroma V plane.
            for row in nv12_packet
                .data
                .chunks_mut(nv12_spec.bytes_per_row)
                .skip(nv12_spec.coded_height())
                .take(nv12_spec.display_height / Nv12Frame::CHROMA_SUBSAMPLE_RATIO)
            {
                row.iter_mut()
                    .take(nv12_spec.display_width)
                    .skip(1)
                    .step_by(2)
                    .for_each(&mut assign_index)
            }

            // NV12 Chroma U plane.
            for row in nv12_packet
                .data
                .chunks_mut(nv12_spec.bytes_per_row)
                .skip(nv12_spec.coded_height())
                .take(nv12_spec.display_height / Nv12Frame::CHROMA_SUBSAMPLE_RATIO)
            {
                row.iter_mut().take(nv12_spec.display_width).step_by(2).for_each(&mut assign_index)
            }

            let mut yv12_packet: OutputPacket = yv12_spec.into();
            rng.fill(yv12_packet.data.as_mut_slice());

            let mut yv12_global_index: usize = 0;
            let mut assign_index = |b: &mut u8| {
                *b = (yv12_global_index % 256) as u8;
                yv12_global_index += 1;
            };

            // YV12 Luminance plane.
            for row in
                yv12_packet.data.chunks_mut(yv12_spec.bytes_per_row).take(yv12_spec.display_height)
            {
                row.iter_mut().take(yv12_spec.display_width).for_each(&mut assign_index);
            }

            // YV12 Chroma V plane.
            let luminance_plane_size = yv12_spec.bytes_per_row * yv12_spec.coded_height();
            for row in yv12_packet.data[luminance_plane_size..]
                .chunks_mut(yv12_spec.bytes_per_row / Yv12Frame::CHROMA_SUBSAMPLE_RATIO)
                .take(yv12_spec.display_height / Yv12Frame::CHROMA_SUBSAMPLE_RATIO)
            {
                row.iter_mut()
                    .take(yv12_spec.display_width / Yv12Frame::CHROMA_SUBSAMPLE_RATIO)
                    .for_each(&mut assign_index)
            }

            // YV12 Chroma U plane.
            let chrominance_plane_size =
                luminance_plane_size / Yv12Frame::CHROMA_SUBSAMPLE_RATIO.pow(2);
            for row in yv12_packet.data[(luminance_plane_size + chrominance_plane_size)..]
                .chunks_mut(yv12_spec.bytes_per_row / Yv12Frame::CHROMA_SUBSAMPLE_RATIO)
                .take(yv12_spec.display_height / Yv12Frame::CHROMA_SUBSAMPLE_RATIO)
            {
                row.iter_mut()
                    .take(yv12_spec.display_width / Yv12Frame::CHROMA_SUBSAMPLE_RATIO)
                    .for_each(&mut assign_index)
            }

            let from_yv12 = packet_display_data(&yv12_packet)?;
            let from_nv12 = packet_display_data(&nv12_packet)?;

            let yv12_hash = Sha256::hash(&from_yv12.collect::<Vec<u8>>().as_slice());
            let nv12_hash = Sha256::hash(&from_nv12.collect::<Vec<u8>>().as_slice());

            assert_eq!(yv12_hash, nv12_hash);
        }

        Ok(())
    }

    #[test]
    fn sanity_test_that_different_packets_hash_differently() -> Result<(), Error> {
        let mut rng = StdRng::seed_from_u64(45);

        for (nv12_spec, yv12_spec) in specs(PixelFormatType::Nv12).zip(specs(PixelFormatType::Yv12))
        {
            let mut nv12_packet: OutputPacket = nv12_spec.into();
            rng.fill(nv12_packet.data.as_mut_slice());

            let mut yv12_packet: OutputPacket = yv12_spec.into();
            yv12_packet.data.iter_mut().enumerate().for_each(|(i, b)| *b = nv12_packet.data[i]);

            // Change a random display byte.
            let x = rng.gen_range(0, nv12_spec.display_width);
            let y = {
                let y_range = [
                    // Luminance plane rows.
                    (0, nv12_spec.display_height),
                    // Chrominance plane rows.
                    (nv12_spec.coded_height(), nv12_spec.display_height / 2),
                ]
                .choose(&mut rng)
                .expect("Sampling from nonempty slice")
                .clone();
                rng.gen_range(y_range.0, y_range.0 + y_range.1)
            };
            let idx = y * (nv12_spec.bytes_per_row as usize) + x;
            nv12_packet.data[idx] = nv12_packet.data[idx].overflowing_add(1).0;

            let from_yv12 = packet_display_data(&yv12_packet)?;
            let from_nv12 = packet_display_data(&nv12_packet)?;

            let yv12_hash = Sha256::hash(&from_yv12.collect::<Vec<u8>>().as_slice());
            let nv12_hash = Sha256::hash(&from_nv12.collect::<Vec<u8>>().as_slice());

            assert_ne!(yv12_hash, nv12_hash);
        }

        Ok(())
    }
}
