// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    carnelian::{
        color::Color,
        make_app_assistant,
        render::{Composition, Context, CopyRegion, Image, PreClear, PreCopy, RenderExt},
        App, AppAssistant, RenderOptions, Size, ViewAssistant, ViewAssistantContext,
        ViewAssistantPtr, ViewKey,
    },
    euclid::default::{Point2D, Rect},
    fuchsia_zircon::{AsHandleRef, Event, Signals},
    std::fs::File,
};

const WHITE_COLOR: Color = Color { r: 255, g: 255, b: 255, a: 255 };

/// Display Png.
#[derive(Debug, FromArgs)]
#[argh(name = "display_png")]
struct Args {
    /// PNG file to load
    #[argh(option)]
    file: String,

    /// background color (default is white)
    #[argh(option, from_str_fn(parse_color))]
    background: Option<Color>,

    /// an optional x,y position for the image (default is center)
    #[argh(option, from_str_fn(parse_point))]
    position: Option<Point2D<f32>>,
}

fn parse_color(value: &str) -> Result<Color, String> {
    Color::from_hash_code(value).map_err(|err| err.to_string())
}

fn parse_point(value: &str) -> Result<Point2D<f32>, String> {
    let mut coords = vec![];
    for value in value.split(",") {
        coords.push(value.parse::<f32>().map_err(|err| err.to_string())?);
    }
    if coords.len() == 2 {
        Ok(Point2D::new(coords[0], coords[1]))
    } else {
        Err("bad position".to_string())
    }
}

#[derive(Default)]
struct DisplayPngAppAssistant {
    filename: String,
    background: Option<Color>,
    position: Option<Point2D<f32>>,
}

impl AppAssistant for DisplayPngAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        let args: Args = argh::from_env();
        self.filename = args.file;
        self.background = args.background;
        self.position = args.position;
        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(DisplayPngViewAssistant::new(
            self.filename.clone(),
            self.background.take().unwrap_or(WHITE_COLOR),
            self.position.take(),
        )))
    }

    fn get_render_options(&self) -> RenderOptions {
        RenderOptions { ..RenderOptions::default() }
    }
}

struct DisplayPngViewAssistant {
    filename: String,
    background: Color,
    png: Option<(Size, Image)>,
    composition: Composition,
    position: Option<Point2D<f32>>,
}

impl DisplayPngViewAssistant {
    pub fn new(filename: String, background: Color, position: Option<Point2D<f32>>) -> Self {
        let background = Color { r: background.r, g: background.g, b: background.b, a: 255 };
        let composition = Composition::new(background);

        Self { filename, background, png: None, composition, position }
    }
}

impl ViewAssistant for DisplayPngViewAssistant {
    fn setup(&mut self, _context: &ViewAssistantContext) -> Result<(), Error> {
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut Context,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        // Create image from PNG.
        let filename = &self.filename;
        let (png_size, png_image) = self.png.take().unwrap_or_else(|| {
            let file = File::open(format!("{}", filename))
                .expect(&format!("failed to open file {}", filename));
            let decoder = png::Decoder::new(file);
            let (info, mut reader) = decoder.read_info().unwrap();
            let image = render_context
                .new_image_from_png(&mut reader)
                .expect(&format!("failed to decode file {}", filename));
            let size = Size::new(info.width as f32, info.height as f32);
            (size, image)
        });

        // Center image if position has not been specified.
        let position = self.position.take().unwrap_or_else(|| {
            let x = (context.size.width - png_size.width) / 2.0;
            let y = (context.size.height - png_size.height) / 2.0;
            Point2D::new(x, y)
        });

        // Determine visible rect.
        let dst_rect = Rect::new(position.to_i32(), png_size.to_i32());
        let output_rect = Rect::new(Point2D::zero(), context.size.to_i32());

        // Clear |image| to background color and copy |png_image| to |image|.
        let ext = RenderExt {
            pre_clear: Some(PreClear { color: self.background }),
            pre_copy: dst_rect.intersection(&output_rect).map(|visible_rect| PreCopy {
                image: png_image,
                copy_region: CopyRegion {
                    src_offset: (visible_rect.origin - dst_rect.origin).to_point().to_u32(),
                    dst_offset: visible_rect.origin.to_u32(),
                    extent: visible_rect.size.to_u32(),
                },
            }),
            ..Default::default()
        };
        let image = render_context.get_current_image(context);
        render_context.render(&self.composition, Some(Rect::zero()), image, &ext);

        self.png.replace((png_size, png_image));
        self.position.replace(position);

        ready_event.as_handle_ref().signal(Signals::NONE, Signals::EVENT_SIGNALED)?;
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    App::run(make_app_assistant::<DisplayPngAppAssistant>())
}
