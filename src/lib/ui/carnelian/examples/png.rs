// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    carnelian::{
        color::Color,
        drawing::path_for_rectangle,
        input::{self},
        make_app_assistant,
        render::{
            BlendMode, Composition, Context, CopyRegion, Fill, FillRule, Image, Layer, PostCopy,
            PreClear, Raster, RenderExt, Style,
        },
        App, AppAssistant, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey,
    },
    euclid::{
        default::{Point2D, Rect},
        point2, size2,
    },
    fuchsia_trace_provider,
    fuchsia_zircon::{AsHandleRef, Event, Signals},
    std::{collections::BTreeMap, fs::File},
};

const WHITE_COLOR: Color = Color { r: 255, g: 255, b: 255, a: 255 };

/// Png.
#[derive(Debug, FromArgs)]
#[argh(name = "png-rs")]
struct Args {
    /// PNG file to load (default is lenna.png)
    #[argh(option, default = "String::from(\"lenna.png\")")]
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
        Ok(point2(coords[0], coords[1]))
    } else {
        Err("bad position".to_string())
    }
}

struct PngAppAssistant {
    filename: String,
    background: Option<Color>,
    position: Option<Point2D<f32>>,
}

impl Default for PngAppAssistant {
    fn default() -> Self {
        let args: Args = argh::from_env();
        let filename = args.file;
        let background = args.background;
        let position = args.position;

        Self { filename, background, position }
    }
}

impl AppAssistant for PngAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(PngViewAssistant::new(
            self.filename.clone(),
            self.background.take().unwrap_or(WHITE_COLOR),
            self.position.take(),
        )))
    }
}

struct Rendering {
    size: Size,
    composition: Composition,
    clear_raster: Option<Raster>,
}

impl Rendering {
    fn new(background: Color) -> Rendering {
        let composition = Composition::new(background);

        Rendering { size: Size::zero(), composition, clear_raster: None }
    }
}

struct PngViewAssistant {
    filename: String,
    background: Color,
    renderings: BTreeMap<u64, Rendering>,
    png: Option<(Size, Image, Raster)>,
    composition: Composition,
    position: Option<Point2D<f32>>,
    touch_locations: BTreeMap<input::pointer::PointerId, Point2D<f32>>,
}

impl PngViewAssistant {
    pub fn new(filename: String, background: Color, position: Option<Point2D<f32>>) -> Self {
        let background = Color { r: background.r, g: background.g, b: background.b, a: 255 };
        let composition = Composition::new(background);

        Self {
            filename,
            background,
            renderings: BTreeMap::new(),
            png: None,
            composition,
            position,
            touch_locations: BTreeMap::new(),
        }
    }
}

impl ViewAssistant for PngViewAssistant {
    fn setup(&mut self, _context: &ViewAssistantContext) -> Result<(), Error> {
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut Context,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let background = self.background;
        let image_id = context.image_id;
        let rendering =
            self.renderings.entry(image_id).or_insert_with(|| Rendering::new(background));
        let ext = if rendering.size != context.size {
            rendering.size = context.size;
            RenderExt { pre_clear: Some(PreClear { color: background }), ..Default::default() }
        } else {
            RenderExt::default()
        };

        // Create image from PNG and raster for clearing.
        let filename = &self.filename;
        let (png_size, png_image, png_raster) = self.png.take().unwrap_or_else(|| {
            let file = File::open(format!("/pkg/data/static/{}", filename))
                .expect(&format!("failed to open file /pkg/data/static/{}", filename));
            let decoder = png::Decoder::new(file);
            let (info, mut reader) = decoder.read_info().unwrap();
            let image = render_context
                .new_image_from_png(&mut reader)
                .expect(&format!("failed to decode file /pkg/data/static/{}", filename));
            let size = size2(info.width as f32, info.height as f32);
            let mut raster_builder = render_context.raster_builder().expect("raster_builder");
            raster_builder
                .add(&path_for_rectangle(&Rect::new(Point2D::zero(), size), render_context), None);
            (size, image, raster_builder.build())
        });

        // Center image if position has not been specified.
        let position = self.position.take().unwrap_or_else(|| {
            let x = (rendering.size.width - png_size.width) / 2.0;
            let y = (rendering.size.height - png_size.height) / 2.0;
            point2(x, y)
        });

        // Clear area where image was previously located.
        if let Some(clear_raster) = &rendering.clear_raster {
            rendering.composition.insert(
                0,
                Layer {
                    raster: clear_raster.clone(),
                    clip: None,
                    style: Style {
                        fill_rule: FillRule::NonZero,
                        fill: Fill::Solid(background),
                        blend_mode: BlendMode::Over,
                    },
                },
            );
        }
        let image = render_context.get_current_image(context);
        render_context.render(&mut rendering.composition, None, image, &ext);

        // Save clear raster for next frame.
        let translation = position.to_vector().to_i32();
        let clear_raster = png_raster.clone().translate(translation);
        rendering.clear_raster.replace(clear_raster);

        // Determine visible rect and copy |png_image| to |image|.
        let dst_rect = Rect::new(position.to_i32(), png_size.to_i32());
        let output_rect = Rect::new(Point2D::zero(), rendering.size.to_i32());
        let ext = RenderExt {
            post_copy: dst_rect.intersection(&output_rect).map(|visible_rect| PostCopy {
                image,
                copy_region: CopyRegion {
                    src_offset: (visible_rect.origin - dst_rect.origin).to_point().to_u32(),
                    dst_offset: visible_rect.origin.to_u32(),
                    extent: visible_rect.size.to_u32(),
                },
            }),
            ..Default::default()
        };
        // Empty clip to skip rendering and only copy image.
        render_context.render(&mut self.composition, Some(Rect::zero()), png_image, &ext);

        self.png.replace((png_size, png_image, png_raster));
        self.position.replace(position);

        ready_event.as_handle_ref().signal(Signals::NONE, Signals::EVENT_SIGNALED)?;

        context.request_render();

        Ok(())
    }

    fn handle_pointer_event(
        &mut self,
        _context: &mut ViewAssistantContext,
        _event: &input::Event,
        pointer_event: &input::pointer::Event,
    ) -> Result<(), Error> {
        match &pointer_event.phase {
            input::pointer::Phase::Down(touch_location) => {
                self.touch_locations
                    .insert(pointer_event.pointer_id.clone(), touch_location.to_f32());
            }
            input::pointer::Phase::Moved(touch_location) => {
                if let Some(location) = self.touch_locations.get_mut(&pointer_event.pointer_id) {
                    let mut position = self.position.take().unwrap_or_else(|| Point2D::zero());
                    // Pan image using the change to touch location.
                    position += touch_location.to_f32() - *location;
                    self.position.replace(position);
                    *location = touch_location.to_f32();
                }
            }
            input::pointer::Phase::Up => {
                self.touch_locations.remove(&pointer_event.pointer_id.clone());
            }
            _ => (),
        }
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    println!("Png Example");
    App::run(make_app_assistant::<PngAppAssistant>())
}
