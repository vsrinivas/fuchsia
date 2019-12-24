// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    canvas::{measure_text, Canvas, FontDescription, FontFace, MappingPixelSink, Paint},
    geometry::{Coord, IntSize, Point, Rect, Size},
};
use anyhow::{Context as _, Error};
use fidl_fuchsia_images as images;
use fuchsia_scenic::{EntityNode, HostImageCycler, SessionPtr};
use lazy_static::lazy_static;

// This font creation method isn't ideal. The correct method would be to ask the Fuchsia
// font service for the font data.
static FONT_DATA: &'static [u8] =
    include_bytes!("../../../../../prebuilt/third_party/fonts/robotoslab/RobotoSlab-Regular.ttf");

lazy_static! {
    pub static ref FONT_FACE: FontFace<'static> =
        FontFace::new(&FONT_DATA).expect("Failed to create font");
}

const BASELINE: i32 = 0;

/// Make a font description for the one included font
pub fn make_font_description<'a, 'b>(size: u32, baseline: i32) -> FontDescription<'a, 'b> {
    FontDescription { face: &FONT_FACE, size: size, baseline: baseline }
}

/// Struct to render a static string as as Scenic node
pub struct Label {
    text: String,
    image_cycler: HostImageCycler,
}

impl Label {
    /// Create a new label for a string in a Scenic session
    pub fn new(session: &SessionPtr, text: &str) -> Result<Label, Error> {
        Ok(Label { text: text.to_string(), image_cycler: HostImageCycler::new(session.clone()) })
    }

    /// Call to update the contents of the Scenic node
    pub fn update(&mut self, font_size: u32, paint: &Paint) -> Result<(), Error> {
        // Figure out the pixel dimension of the label
        let size = self.dimensions(font_size).ceil().to_i32();
        let w = size.width;
        let h = size.height;
        if w > 0 && h > 0 {
            // Fuchsia is uniformly 4 bytes per pixel but ideally this would
            // come from the graphics environment.
            let stride = (w * 4) as u32;

            // Create a description of this pixel buffer that
            // Scenic can understand.
            let info = images::ImageInfo {
                transform: images::Transform::Normal,
                width: w as u32,
                height: h as u32,
                stride: stride,
                pixel_format: images::PixelFormat::Bgra8,
                color_space: images::ColorSpace::Srgb,
                tiling: images::Tiling::Linear,
                alpha_format: images::AlphaFormat::Opaque,
            };

            // Grab an image buffer from the cycler
            let guard = self.image_cycler.acquire(info).context("failed to allocate buffer")?;

            // Create a canvas to render into the buffer
            let mut canvas = Canvas::new(
                IntSize::new(w, h),
                MappingPixelSink::new(&guard.image().mapping()),
                stride,
                4,
                0,
                0,
            );

            // since the label buffer is sized to fit the text, always draw at 0,0
            let location = Point::zero();

            // fill the buffer with the bg color, since fill_text ignore pixels that
            // aren't covered by glyphs
            let bounds = Rect::from_size(Size::new(w as Coord, h as Coord));
            canvas.fill_rect(&bounds, paint.bg);

            // Fill the text
            canvas.fill_text(
                &self.text,
                location,
                &mut make_font_description(font_size, BASELINE),
                &paint,
            );
        }

        Ok(())
    }

    /// Use canvas's measure_text() to find the pixel size of the label
    pub fn dimensions(&self, font_size: u32) -> Size {
        let mut font_description = make_font_description(font_size, BASELINE);
        measure_text(&self.text, &mut font_description)
    }

    /// Expose the image cycler's node so that it can be added to the
    /// button's container
    pub fn node(&mut self) -> &EntityNode {
        self.image_cycler.node()
    }
}
