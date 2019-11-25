// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]
#![allow(unused_imports)]

use carnelian::{
    make_font_description, AnimationMode, App, AppAssistant, Canvas, MappingPixelSink, PixelSink,
    Point, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey, ViewMode,
};
use failure::Error;
use fuchsia_zircon::{ClockId, Time};
use mold::{
    tile::{Map, Op},
    ColorBuffer, Layer, Path, PixelFormat, Raster,
};
use rusttype::{Contour, Font, Scale, Segment};
use std::{
    collections::BTreeMap,
    env, f32,
    io::{self, Read},
    slice, thread,
};

static FONT_DATA: &'static [u8] =
    include_bytes!("../../../../../prebuilt/third_party/fonts/robotoslab/RobotoSlab-Regular.ttf");

/// Convenience function that can be called from main and causes the Fuchsia process being
/// run over ssh to be terminated when the user hits control-C.
pub fn wait_for_close() {
    if let Some(argument) = env::args().next() {
        if !argument.starts_with("/tmp") {
            return;
        }
    }

    thread::spawn(move || loop {
        let mut input = [0; 1];
        match io::stdin().read_exact(&mut input) {
            Ok(()) => {}
            Err(_) => {
                std::process::exit(0);
            }
        }
    });
}

struct RoundedRect {
    path: Path,
}

impl RoundedRect {
    fn new(pos: Point, size: Point, radius: f32) -> Self {
        let dist = 4.0 / 3.0 * (f32::consts::PI / 8.0).tan();
        let control_dist = dist * radius;

        let tl = pos.to_vector();
        let tr = pos.to_vector() + Point::new(size.x, 0.0).to_vector();
        let br = pos.to_vector() + size.to_vector();
        let bl = pos.to_vector() + Point::new(0.0, size.y).to_vector();

        let rt = Point::new(0.0, radius).to_vector();
        let rr = Point::new(-radius, 0.0).to_vector();
        let rb = Point::new(0.0, -radius).to_vector();
        let rl = Point::new(radius, 0.0).to_vector();

        let ct = Point::new(0.0, -control_dist).to_vector();
        let cr = Point::new(control_dist, 0.0).to_vector();
        let cb = Point::new(0.0, control_dist).to_vector();
        let cl = Point::new(-control_dist, 0.0).to_vector();

        macro_rules! c {
            ( $v:expr ) => {
                mold::Point::new($v.x, $v.y)
            };
        }

        let mut path = Path::new();

        path.line(c!(tl + rl), c!(tr + rr));
        path.cubic(c!(tr + rr), c!(tr + rr + cr), c!(tr + rt + ct), c!(tr + rt));
        path.line(c!(tr + rt), c!(br + rb));
        path.cubic(c!(br + rb), c!(br + rb + cb), c!(br + rr + cr), c!(br + rr));
        path.line(c!(br + rr), c!(bl + rl));
        path.cubic(c!(bl + rl), c!(bl + rl + cl), c!(bl + rb + cb), c!(bl + rb));
        path.line(c!(bl + rb), c!(tl + rt));
        path.cubic(c!(tl + rt), c!(tl + rt + ct), c!(tl + rl + cl), c!(tl + rl));

        Self { path }
    }
}

struct Glyph {
    path: Path,
}

impl Glyph {
    fn new(contours: &[Contour], scale: f32) -> Self {
        let mut path = Path::new();

        let flip_scale = |x: f32, y: f32| {
            let x = x * scale;
            let y = (-y + 1.0) * scale;
            mold::Point::new(x, y)
        };

        for contour in contours {
            for segment in &contour.segments {
                match segment {
                    Segment::Line(line) => {
                        path.line(
                            flip_scale(line.p[1].x, line.p[1].y),
                            flip_scale(line.p[0].x, line.p[0].y),
                        );
                    }
                    Segment::Curve(curve) => {
                        path.quad(
                            flip_scale(curve.p[2].x, curve.p[2].y),
                            flip_scale(curve.p[1].x, curve.p[1].y),
                            flip_scale(curve.p[0].x, curve.p[0].y),
                        );
                    }
                }
            }
        }

        Self { path }
    }
}

struct Contents {
    rect: Raster,
    glyph: Vec<Contour>,
    map: Map,
    size: Size,
}

impl Contents {
    fn make_background(size: &Size) -> Map {
        let mut map = Map::new(size.width.floor() as usize, size.height.floor() as usize);
        map.global(0, vec![Op::ColorAccZero]);

        map.global(3, vec![Op::ColorAccBackground(0xEBD5_B3FF)]);
        map
    }

    fn new(size: Size) -> Contents {
        let map = Self::make_background(&size);

        let rounded_rect = RoundedRect::new(Point::new(200.0, 100.0), Point::new(90.0, 50.0), 20.0);

        let rect = Raster::new(&rounded_rect.path);

        let font_description = make_font_description(0, 1);
        let glyph =
            font_description.face.font.glyph('a').scaled(Scale::uniform(1.0)).shape().unwrap();

        Self { rect, glyph, map: map, size: Size::zero() }
    }

    fn update(&mut self, size: &Size, start: &Time, canvas: &Canvas<MappingPixelSink>) {
        if self.size != *size {
            self.size = *size;
            self.map = Self::make_background(size);
        }

        const SPEED: f32 = 1.2;
        const SECONDS_PER_NANOSECOND: f32 = 1e-9;
        const DELTA: f32 = 100.0;
        const GLYPH_SIZE: f32 = 200.0;

        let time_now = Time::get(ClockId::Monotonic);
        let t =
            (time_now.into_nanos() - start.into_nanos()) as f32 * SECONDS_PER_NANOSECOND * SPEED;

        self.rect.set_translation(mold::Point::new((DELTA * t.sin()).round() as i32, 0));

        let mut raster = Raster::new(&Glyph::new(&self.glyph, GLYPH_SIZE * t.sin().abs()).path);
        raster.translate(mold::Point::new(100, 100));

        self.map.print(
            1,
            Layer::new(
                self.rect.clone(),
                vec![
                    Op::CoverWipZero,
                    Op::CoverWipNonZero,
                    Op::ColorWipZero,
                    Op::ColorWipFillSolid(0x7B68_EEFF),
                    Op::ColorAccBlendOver,
                ],
            ),
        );

        self.map.print(
            2,
            Layer::new(
                raster,
                vec![
                    Op::CoverWipZero,
                    Op::CoverWipNonZero,
                    Op::ColorWipZero,
                    Op::ColorWipFillSolid(0x0000_00FF),
                    Op::ColorAccBlendOver,
                ],
            ),
        );

        self.map.render(PixelSinkWrapper::new(
            canvas.pixel_sink.clone(),
            canvas.col_stride,
            canvas.row_stride,
        ));
    }
}

struct DrawingAppAssistant;

impl AppAssistant for DrawingAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant_canvas(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(DrawingViewAssistant::new()))
    }

    fn get_mode(&self) -> ViewMode {
        ViewMode::Canvas
    }
}

struct DrawingViewAssistant {
    contents: BTreeMap<u64, Contents>,
    start: Time,
}

impl DrawingViewAssistant {
    pub fn new() -> Self {
        Self { contents: BTreeMap::new(), start: Time::get(ClockId::Monotonic) }
    }
}

#[derive(Clone)]
struct PixelSinkWrapper<P: PixelSink> {
    pixel_sink: P,
    format: PixelFormat,
    stride: usize,
}

impl<P: PixelSink> PixelSinkWrapper<P> {
    pub fn new(pixel_sink: P, col_stride: u32, row_stride: u32) -> Self {
        Self {
            pixel_sink,
            format: if col_stride == 2 { PixelFormat::RGB565 } else { PixelFormat::BGRA8888 },
            stride: (row_stride / col_stride) as usize,
        }
    }
}

impl<P: PixelSink> ColorBuffer for PixelSinkWrapper<P> {
    fn pixel_format(&self) -> PixelFormat {
        self.format
    }

    fn stride(&self) -> usize {
        self.stride
    }

    unsafe fn write_at(&mut self, offset: usize, src: *const u8, len: usize) {
        self.pixel_sink.write_pixel_at_offset(offset, slice::from_raw_parts(src, len));
    }
}

impl ViewAssistant for DrawingViewAssistant {
    fn setup(&mut self, _: &ViewAssistantContext<'_>) -> Result<(), Error> {
        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        let canvas = context.canvas.as_ref().unwrap().borrow();

        // Temporary hack to deal with the fact that carnelian
        // allocates a new buffer for each frame with the same
        // image ID of zero.
        let mut temp_content;
        let content;

        if canvas.id == 0 {
            temp_content = Contents::new(context.size);
            content = &mut temp_content;
        } else {
            content = self.contents.entry(canvas.id).or_insert_with(|| Contents::new(context.size));
        }
        content.update(&context.size, &self.start, &canvas);
        Ok(())
    }

    fn initial_animation_mode(&mut self) -> AnimationMode {
        return AnimationMode::EveryFrame;
    }
}

fn main() -> Result<(), Error> {
    println!("drawing: started");
    wait_for_close();
    let assistant = DrawingAppAssistant {};
    App::run(Box::new(assistant))
}
