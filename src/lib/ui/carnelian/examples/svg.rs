// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    carnelian::{
        color::Color,
        input, make_app_assistant,
        render::{
            BlendMode, Composition, Context, Fill, FillRule, Layer, PreClear, Raster, RenderExt,
            Style,
        },
        App, AppAssistant, RenderOptions, Size, ViewAssistant, ViewAssistantContext,
        ViewAssistantPtr, ViewKey,
    },
    euclid::default::{Point2D, Transform2D, Vector2D},
    fuchsia_trace_provider,
    fuchsia_zircon::{AsHandleRef, Event, Signals},
    std::collections::BTreeMap,
};

const BACKGROUND_COLOR: Color = Color { r: 255, g: 255, b: 255, a: 255 };

/// Svg.
#[derive(Debug, FromArgs)]
#[argh(name = "svg-rs")]
struct Args {
    /// use spinel (GPU rendering back-end)
    #[argh(switch, short = 's')]
    use_spinel: bool,
}

#[derive(Default)]
struct SvgAppAssistant {
    use_spinel: bool,
}

impl AppAssistant for SvgAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        let args: Args = argh::from_env();
        self.use_spinel = args.use_spinel;
        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(SvgViewAssistant::new()))
    }

    fn get_render_options(&self) -> RenderOptions {
        RenderOptions { use_spinel: self.use_spinel }
    }
}

struct Rendering {
    size: Size,
    composition: Composition,
    last_position: Option<Point2D<f32>>,
}

impl Rendering {
    fn new() -> Rendering {
        let composition = Composition::new(BACKGROUND_COLOR);

        Rendering { size: Size::zero(), composition, last_position: None }
    }
}

struct SvgViewAssistant {
    rasters: Option<Vec<(Raster, Style)>>,
    renderings: BTreeMap<u64, Rendering>,
    position: Point2D<f32>,
    touch_locations: BTreeMap<input::pointer::PointerId, Point2D<f32>>,
}

impl SvgViewAssistant {
    pub fn new() -> Self {
        Self {
            rasters: None,
            renderings: BTreeMap::new(),
            position: Point2D::zero(),
            touch_locations: BTreeMap::new(),
        }
    }
}

impl ViewAssistant for SvgViewAssistant {
    fn setup(&mut self, _context: &ViewAssistantContext) -> Result<(), Error> {
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut Context,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let image_id = context.image_id;
        let rendering = self.renderings.entry(image_id).or_insert_with(|| Rendering::new());

        let position = self.position;
        let last_position = rendering.last_position;
        let rasters = self.rasters.get_or_insert_with(|| {
            let shed = carnelian::render::Shed::open("/pkg/data/static/fuchsia.shed").unwrap();
            let size = shed.size();
            let min_side = size.width.min(size.height);
            let min_screen_side = context.size.width.min(context.size.height);
            let scale_factor = min_screen_side / min_side * 0.75;
            let transform = Transform2D::create_translation(-size.width / 2.0, -size.height / 2.0)
                .post_scale(scale_factor, scale_factor)
                .post_translate(Vector2D::new(context.size.width / 2.0, context.size.height / 2.0));

            shed.rasters(render_context, Some(&transform))
        });

        let layers = rasters.iter().map(|(raster, style)| Layer {
            raster: raster.clone().translate(position.to_vector().to_i32()),
            style: *style,
        });

        if let Some(last_position) = last_position {
            rendering.composition.replace(
                ..,
                layers.chain(rasters.iter().map(|(raster, _)| Layer {
                    raster: raster.clone().translate(last_position.to_vector().to_i32()),
                    style: Style {
                        fill_rule: FillRule::WholeTile,
                        fill: Fill::Solid(BACKGROUND_COLOR),
                        blend_mode: BlendMode::Over,
                    },
                })),
            );
        } else {
            rendering.composition.replace(.., layers);
        }

        let ext = if rendering.size != context.size {
            rendering.size = context.size;
            RenderExt {
                pre_clear: Some(PreClear { color: BACKGROUND_COLOR }),
                ..Default::default()
            }
        } else {
            RenderExt::default()
        };

        rendering.last_position = Some(position);

        let image = render_context.get_current_image(context);
        render_context.render(&rendering.composition, None, image, &ext);
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
                    // Pan image using the change to touch location.
                    self.position += touch_location.to_f32() - *location;
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

    println!("Svg Example");
    App::run(make_app_assistant::<SvgAppAssistant>())
}
