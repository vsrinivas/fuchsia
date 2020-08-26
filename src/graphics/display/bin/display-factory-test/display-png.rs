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
    fuchsia_async as fasync,
    fuchsia_zircon::{AsHandleRef, Duration, Event, Signals},
    std::fs::File,
    std::process,
};

const WHITE_COLOR: Color = Color { r: 255, g: 255, b: 255, a: 255 };

/// Display Png.
#[derive(Debug, FromArgs)]
#[argh(name = "display_png")]
struct Args {
    /// PNG file to load
    #[argh(option)]
    file: Option<String>,

    /// path to file containing gamma values to apply to the red channel
    /// If this option is provided the other channels must also be provided.
    #[argh(option)]
    gamma_red_file: Option<String>,

    /// path to file containing gamma values to apply to the green channel
    /// If this option is provided the other channels must also be provided.
    #[argh(option)]
    gamma_green_file: Option<String>,

    /// path to file containing gamma values to apply to the blue channel
    /// If this option is provided the other channels must also be provided.
    #[argh(option)]
    gamma_blue_file: Option<String>,

    /// background color (default is white)
    #[argh(option, from_str_fn(parse_color))]
    background: Option<Color>,

    /// an optional x,y position for the image (default is center)
    #[argh(option, from_str_fn(parse_point))]
    position: Option<Point2D<f32>>,

    /// seconds of delay before application exits (default is 1 second)
    #[argh(option, default = "1")]
    timeout: i64,
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

struct PngSourceInfo {
    png_reader: Option<png::Reader<File>>,
    png_size: IntSize,
}

#[derive(Clone, Debug)]
struct GammaValues {
    red: Vec<f32>,
    green: Vec<f32>,
    blue: Vec<f32>,
}

#[derive(Default)]
struct DisplayPngAppAssistant {
    png_source: Option<PngSourceInfo>,
    gamma_values: Option<GammaValues>,
    background: Option<Color>,
    position: Option<Point2D<f32>>,
}

impl DisplayPngAppAssistant {
    fn read_gamma_channel_file(src: &String) -> Result<Vec<f32>, Error> {
        let file =
            File::open(format!("{}", src)).context(format!("failed to open file {}", src))?;
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
        Ok(gamma_values)
    }
}

impl AppAssistant for DisplayPngAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        let args: Args = argh::from_env();
        self.background = args.background;
        self.position = args.position;

        if let Some(filename) = args.file {
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

            self.png_source = Some(PngSourceInfo {
                png_reader: Some(png_reader),
                png_size: IntSize::new(info.width as i32, info.height as i32),
            });
        }

        if args.gamma_red_file.is_some()
            || args.gamma_green_file.is_some()
            || args.gamma_blue_file.is_some()
        {
            ensure!(
                args.gamma_red_file.is_some()
                    && args.gamma_green_file.is_some()
                    && args.gamma_blue_file.is_some(),
                "When providing a gamma channel file one must provide all three"
            );

            let red = Self::read_gamma_channel_file(args.gamma_red_file.as_ref().unwrap())?;
            let green = Self::read_gamma_channel_file(args.gamma_green_file.as_ref().unwrap())?;
            let blue = Self::read_gamma_channel_file(args.gamma_blue_file.as_ref().unwrap())?;
            self.gamma_values = Some(GammaValues { red, green, blue });
        }

        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        let png_source = self.png_source.take();
        Ok(Box::new(DisplayPngViewAssistant::new(
            png_source,
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
    png_source: Option<PngSourceInfo>,
    gamma_values: Option<GammaValues>,
    background: Color,
    png: Option<Image>,
    composition: Composition,
    position: Option<Point2D<f32>>,
}

impl DisplayPngViewAssistant {
    pub fn new(
        png_source: Option<PngSourceInfo>,
        gamma_values: Option<GammaValues>,
        background: Color,
        position: Option<Point2D<f32>>,
    ) -> Self {
        let background = Color { r: background.r, g: background.g, b: background.b, a: 255 };
        let composition = Composition::new(background);
        Self { png_source, gamma_values, background, png: None, composition, position }
    }
}

impl ViewAssistant for DisplayPngViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        let args: Args = argh::from_env();
        if let Some(gamma_values) = self.gamma_values.as_ref() {
            if let Some(frame_buffer) = context.frame_buffer.as_ref() {
                let frame_buffer = frame_buffer.borrow_mut();
                let mut r: [f32; 256] = [0.0; 256];
                r.copy_from_slice(&gamma_values.red);
                let mut g: [f32; 256] = [0.0; 256];
                g.copy_from_slice(&gamma_values.green);
                let mut b: [f32; 256] = [0.0; 256];
                b.copy_from_slice(&gamma_values.blue);
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
        let timer = fasync::Timer::new(fasync::Time::after(Duration::from_seconds(args.timeout)));
        fasync::Task::local(async move {
            timer.await;
            process::exit(1);
        })
        .detach();
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut Context,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let pre_copy = if let Some(png_source) = self.png_source.as_mut() {
            let png_size = png_source.png_size;
            // Create image from PNG.
            let png_image = self.png.take().unwrap_or_else(|| {
                let mut png_reader = png_source.png_reader.take().expect("png_reader");
                render_context
                    .new_image_from_png(&mut png_reader)
                    .expect(&format!("failed to decode file"))
            });

            // Center image if position has not been specified.
            let position = self.position.take().unwrap_or_else(|| {
                let x = (context.size.width - png_size.width as f32) / 2.0;
                let y = (context.size.height - png_size.height as f32) / 2.0;
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

            self.png.replace(png_image);
            self.position.replace(position);

            dst_rect.intersection(&output_rect).map(|visible_rect| PreCopy {
                image: png_image,
                copy_region: CopyRegion {
                    src_offset: (visible_rect.origin - dst_rect.origin).to_point().to_u32(),
                    dst_offset: visible_rect.origin.to_u32(),
                    extent: visible_rect.size.to_u32(),
                },
            })
        } else {
            None
        };

        // Clear |image| to background color and copy |png_image| to |image|.
        let ext = RenderExt {
            pre_clear: Some(PreClear { color: self.background }),
            pre_copy: pre_copy,
            ..Default::default()
        };
        let image = render_context.get_current_image(context);
        render_context.render(&self.composition, Some(Rect::zero()), image, &ext);

        ready_event.as_handle_ref().signal(Signals::NONE, Signals::EVENT_SIGNALED)?;
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    App::run(make_app_assistant::<DisplayPngAppAssistant>())
}
