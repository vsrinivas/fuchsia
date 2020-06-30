// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{ensure, Context as AnyhowContext, Error},
    argh::FromArgs,
    carnelian::{
        color::Color,
        make_app_assistant,
        render::{Composition, Context, CopyRegion, Image, PreClear, PreCopy, RenderExt},
        App, AppAssistant, IntSize, RenderOptions, ViewAssistant, ViewAssistantContext,
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

    /// gamma values to apply
    #[argh(option)]
    gamma_file: Option<String>,

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
    png_reader: Option<png::Reader<File>>,
    png_size: IntSize,
    gamma_values: Option<Vec<f32>>,
    background: Option<Color>,
    position: Option<Point2D<f32>>,
}

impl AppAssistant for DisplayPngAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        let args: Args = argh::from_env();
        self.background = args.background;
        self.position = args.position;

        let filename = args.file;

        let file = File::open(format!("{}", filename))
            .context(format!("failed to open file {}", filename))?;
        let decoder = png::Decoder::new(file);
        let (info, png_reader) =
            decoder.read_info().context(format!("failed to open image file {}", filename))?;
        ensure!(
            info.width > 0 && info.height > 0,
            "Invalid image size for png file {}x{}",
            info.width,
            info.height
        );

        self.png_reader = Some(png_reader);
        self.png_size = IntSize::new(info.width as i32, info.height as i32);

        if let Some(gamma_file) = args.gamma_file {
            let file = File::open(format!("{}", gamma_file))
                .context(format!("failed to open file {}", gamma_file))?;
            let mut rdr = csv::ReaderBuilder::new().has_headers(false).from_reader(file);
            let gamma_values: Vec<f32> = rdr
                .records()
                // map records, dropping any that don't parse correctly.
                .filter_map(|record| {
                    // Get the first field, and if it is there, try to parse it as
                    // a f32 (inferred from the type of the field). Use .ok() to turn
                    // a parse failure into a None value which will get filtered out by
                    // filter_map. Finally scale the value.
                    // TODO(55351): Remove hard-coded 1023 scaling value.
                    record.ok().and_then(|record| {
                        record
                            .get(0)
                            .and_then(|value_str| value_str.parse().ok())
                            .and_then(|v: f32| Some(v / 1023.0))
                    })
                })
                .collect();

            ensure!(gamma_values.len() == 256, "The gamma values file must have exactly 256 lines");
            self.gamma_values = Some(gamma_values);
        }
        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        let png_reader = self.png_reader.take().expect("png_reader");
        Ok(Box::new(DisplayPngViewAssistant::new(
            self.png_size,
            png_reader,
            self.gamma_values.clone(),
            self.background.take().unwrap_or(WHITE_COLOR),
            self.position.take(),
        )))
    }

    fn get_render_options(&self) -> RenderOptions {
        RenderOptions { ..RenderOptions::default() }
    }
}

const GAMMA_TABLE_ID: u64 = 100;

struct DisplayPngViewAssistant {
    png_reader: Option<png::Reader<File>>,
    png_size: IntSize,
    gamma_values: Option<Vec<f32>>,
    background: Color,
    png: Option<Image>,
    composition: Composition,
    position: Option<Point2D<f32>>,
}

impl DisplayPngViewAssistant {
    pub fn new(
        png_size: IntSize,
        png_reader: png::Reader<File>,
        gamma_values: Option<Vec<f32>>,
        background: Color,
        position: Option<Point2D<f32>>,
    ) -> Self {
        let background = Color { r: background.r, g: background.g, b: background.b, a: 255 };
        let composition = Composition::new(background);
        Self {
            png_size,
            png_reader: Some(png_reader),
            gamma_values,
            background,
            png: None,
            composition,
            position,
        }
    }
}

impl ViewAssistant for DisplayPngViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        if let Some(gamma_values) = self.gamma_values.as_ref() {
            if let Some(frame_buffer) = context.frame_buffer.as_ref() {
                let frame_buffer = frame_buffer.borrow_mut();
                let mut r: [f32; 256] = [0.0; 256];
                r.copy_from_slice(&gamma_values);
                let mut g: [f32; 256] = [0.0; 256];
                g.copy_from_slice(&gamma_values);
                let mut b: [f32; 256] = [0.0; 256];
                b.copy_from_slice(&gamma_values);
                frame_buffer.controller.import_gamma_table(
                    GAMMA_TABLE_ID,
                    &mut r,
                    &mut g,
                    &mut b,
                )?;
                let config = frame_buffer.get_config();
                frame_buffer
                    .controller
                    .set_display_gamma_table(config.display_id, GAMMA_TABLE_ID)?;
            }
        }
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut Context,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        // Create image from PNG.
        let png_image = self.png.take().unwrap_or_else(|| {
            let mut png_reader = self.png_reader.take().expect("png_reader");
            render_context
                .new_image_from_png(&mut png_reader)
                .expect(&format!("failed to decode file"))
        });

        // Center image if position has not been specified.
        let position = self.position.take().unwrap_or_else(|| {
            let x = (context.size.width - self.png_size.width as f32) / 2.0;
            let y = (context.size.height - self.png_size.height as f32) / 2.0;
            Point2D::new(x, y)
        });

        // Determine visible rect.
        let dst_rect = Rect::new(position.to_i32(), self.png_size.to_i32());
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

        self.png.replace(png_image);
        self.position.replace(position);

        ready_event.as_handle_ref().signal(Signals::NONE, Signals::EVENT_SIGNALED)?;
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    App::run(make_app_assistant::<DisplayPngAppAssistant>())
}
