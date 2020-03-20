// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    carnelian::{
        make_app_assistant,
        render::{Context as RenderContext, *},
        AnimationMode, App, AppAssistant, Color, Point, Rect, RenderOptions, Size, ViewAssistant,
        ViewAssistantContext, ViewAssistantPtr, ViewKey, ViewMode,
    },
    euclid::{Transform2D, Vector2D},
    fuchsia_trace_provider,
    fuchsia_zircon::{AsHandleRef, Event, Signals},
};

const BLACK_COLOR: Color = Color { r: 0, g: 0, b: 0, a: 255 };
const TRANSLUCENT_COLOR: Color = Color { r: 0, g: 0, b: 0, a: 127 };
const GRAY_COLOR: Color = Color { r: 187, g: 187, b: 187, a: 255 };
const WHITE_COLOR: Color = Color { r: 255, g: 255, b: 255, a: 255 };

/// Gamma.
#[derive(Debug, FromArgs)]
#[argh(name = "gamma_rs")]
struct Args {
    /// use spinel (GPU rendering back-end)
    #[argh(switch, short = 's')]
    use_spinel: bool,
}

#[derive(Default)]
struct GammaAppAssistant {
    use_spinel: bool,
}

impl AppAssistant for GammaAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        let args: Args = argh::from_env();
        self.use_spinel = args.use_spinel;
        Ok(())
    }

    fn create_view_assistant_render(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(GammaViewAssistant::new()))
    }

    fn get_mode(&self) -> ViewMode {
        ViewMode::Render(RenderOptions { use_spinel: self.use_spinel })
    }
}

fn path_for_rectangle(bounds: &Rect, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    path_builder.move_to(bounds.origin);
    path_builder.line_to(bounds.top_right());
    path_builder.line_to(bounds.bottom_right());
    path_builder.line_to(bounds.bottom_left());
    path_builder.line_to(bounds.origin);
    path_builder.build()
}

struct GammaViewAssistant {
    path: Option<Path>,
    composition: Composition,
}

impl GammaViewAssistant {
    pub fn new() -> Self {
        let composition = Composition::new(WHITE_COLOR);

        Self { path: None, composition }
    }
}

impl ViewAssistant for GammaViewAssistant {
    fn setup(&mut self, _context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        Ok(())
    }

    fn update(&mut self, _: &ViewAssistantContext<'_>) -> Result<(), Error> {
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut Context,
        ready_event: Event,
        context: &ViewAssistantContext<'_>,
    ) -> Result<(), Error> {
        let path = self.path.take().unwrap_or_else(|| {
            path_for_rectangle(&Rect::new(Point::zero(), Size::new(1.0, 1.0)), render_context)
        });
        let transform = Transform2D::create_scale(
            context.logical_size.width * 0.5,
            context.logical_size.height * 0.5,
        );
        let mut raster_builder = render_context.raster_builder().expect("raster_builder");
        raster_builder.add(&path, Some(&transform));
        let raster = raster_builder.build();
        let transform = Transform2D::create_scale(context.logical_size.width * 0.5, 1.0);
        let mut raster_builder = render_context.raster_builder().expect("raster_builder");
        raster_builder.add(&path, Some(&transform));
        let line_raster = raster_builder.build();
        self.path.replace(path);

        let layers = std::iter::once(Layer {
            raster: raster.clone(),
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(GRAY_COLOR),
                blend_mode: BlendMode::Over,
            },
        })
        .chain(std::iter::once(Layer {
            raster: raster
                .clone()
                .translate(Vector2D::new(0, (context.logical_size.height * 0.5) as i32)),
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(TRANSLUCENT_COLOR),
                blend_mode: BlendMode::Over,
            },
        }))
        .chain((0..context.logical_size.height as i32).step_by(2).map(|y| {
            Layer {
                raster: line_raster
                    .clone()
                    .translate(Vector2D::new((context.logical_size.width * 0.5) as i32, y)),
                style: Style {
                    fill_rule: FillRule::NonZero,
                    fill: Fill::Solid(BLACK_COLOR),
                    blend_mode: BlendMode::Over,
                },
            }
        }));

        self.composition.replace(.., layers);

        let image = render_context.get_current_image(context);
        let ext =
            RenderExt { pre_clear: Some(PreClear { color: WHITE_COLOR }), ..Default::default() };
        render_context.render(&self.composition, None, image, &ext);
        ready_event.as_handle_ref().signal(Signals::NONE, Signals::EVENT_SIGNALED)?;
        Ok(())
    }

    fn initial_animation_mode(&mut self) -> AnimationMode {
        return AnimationMode::EveryFrame;
    }
}

fn main() -> Result<(), Error> {
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    println!("Gamma Example");
    App::run(make_app_assistant::<GammaAppAssistant>())
}
