// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    carnelian::{
        color::Color,
        drawing::path_for_rectangle,
        make_app_assistant,
        render::{
            BlendMode, Composition, Context, Fill, FillRule, Layer, Path, PreClear, RenderExt,
            Style,
        },
        App, AppAssistant, Point, Rect, RenderOptions, Size, ViewAssistant, ViewAssistantContext,
        ViewAssistantPtr, ViewKey,
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

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(GammaViewAssistant::new()))
    }

    fn get_render_options(&self) -> RenderOptions {
        RenderOptions { use_spinel: self.use_spinel, ..RenderOptions::default() }
    }
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
    fn render(
        &mut self,
        render_context: &mut Context,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let path = self.path.take().unwrap_or_else(|| {
            path_for_rectangle(&Rect::new(Point::zero(), Size::new(1.0, 1.0)), render_context)
        });
        let transform =
            Transform2D::create_scale(context.size.width * 0.5, context.size.height * 0.5);
        let mut raster_builder = render_context.raster_builder().expect("raster_builder");
        raster_builder.add(&path, Some(&transform));
        let raster = raster_builder.build();
        let transform = Transform2D::create_scale(context.size.width * 0.5, 1.0);
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
            raster: raster.clone().translate(Vector2D::new(0, (context.size.height * 0.5) as i32)),
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(TRANSLUCENT_COLOR),
                blend_mode: BlendMode::Over,
            },
        }))
        .chain((0..context.size.height as i32).step_by(2).map(|y| Layer {
            raster:
                line_raster.clone().translate(Vector2D::new((context.size.width * 0.5) as i32, y)),
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(BLACK_COLOR),
                blend_mode: BlendMode::Over,
            },
        }));

        self.composition.replace(.., layers);

        let image = render_context.get_current_image(context);
        let ext =
            RenderExt { pre_clear: Some(PreClear { color: WHITE_COLOR }), ..Default::default() };
        render_context.render(&self.composition, None, image, &ext);
        ready_event.as_handle_ref().signal(Signals::NONE, Signals::EVENT_SIGNALED)?;
        context.request_render();
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    println!("Gamma Example");
    App::run(make_app_assistant::<GammaAppAssistant>())
}
