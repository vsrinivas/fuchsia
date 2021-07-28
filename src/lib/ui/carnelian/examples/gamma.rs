// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    carnelian::{
        color::Color,
        drawing::path_for_rectangle,
        make_app_assistant,
        render::{BlendMode, Context as RenderContext, Fill, FillRule, Layer, Path, Style},
        scene::{
            facets::Facet,
            scene::{Scene, SceneBuilder},
            LayerGroup,
        },
        App, AppAssistant, Point, Rect, Size, ViewAssistant, ViewAssistantContext,
        ViewAssistantPtr, ViewKey,
    },
    euclid::{size2, vec2, Transform2D},
    fuchsia_trace_provider,
    fuchsia_zircon::Event,
};

const BLACK_COLOR: Color = Color { r: 0, g: 0, b: 0, a: 255 };
const TRANSLUCENT_COLOR: Color = Color { r: 0, g: 0, b: 0, a: 127 };
const GRAY_COLOR: Color = Color { r: 187, g: 187, b: 187, a: 255 };
const WHITE_COLOR: Color = Color { r: 255, g: 255, b: 255, a: 255 };

#[derive(Default)]
struct GammaAppAssistant;

impl AppAssistant for GammaAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(GammaViewAssistant::new()))
    }
}

struct GammaFacet {
    path: Path,
    size: Size,
}

impl GammaFacet {
    fn new(context: &mut RenderContext) -> Self {
        let path = path_for_rectangle(&Rect::new(Point::zero(), size2(1.0, 1.0)), context);

        Self { path, size: Size::zero() }
    }
}

impl Facet for GammaFacet {
    fn update_layers(
        &mut self,
        size: Size,
        layer_group: &mut dyn LayerGroup,
        render_context: &mut RenderContext,
        _view_context: &ViewAssistantContext,
    ) -> std::result::Result<(), anyhow::Error> {
        self.size = size;
        let transform = Transform2D::scale(size.width * 0.5, size.height * 0.5);
        let mut raster_builder = render_context.raster_builder().expect("raster_builder");
        raster_builder.add(&self.path, Some(&transform));
        let raster = raster_builder.build();
        let transform = Transform2D::scale(size.width * 0.5, 1.0);
        let mut raster_builder = render_context.raster_builder().expect("raster_builder");
        raster_builder.add(&self.path, Some(&transform));
        let line_raster = raster_builder.build();

        let layers = std::iter::once(Layer {
            raster: raster.clone(),
            clip: None,
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(GRAY_COLOR),
                blend_mode: BlendMode::Over,
            },
        })
        .chain(std::iter::once(Layer {
            raster: raster.clone().translate(vec2(0, (size.height * 0.5) as i32)),
            clip: None,
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(TRANSLUCENT_COLOR),
                blend_mode: BlendMode::Over,
            },
        }))
        .chain((0..size.height as i32).step_by(2).map(|y| Layer {
            raster: line_raster.clone().translate(vec2((size.width * 0.5) as i32, y)),
            clip: None,
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(BLACK_COLOR),
                blend_mode: BlendMode::Over,
            },
        }));
        layer_group.clear();
        for (i, layer) in layers.enumerate() {
            layer_group.insert(i as u16, layer);
        }
        Ok(())
    }

    fn calculate_size(&self, _available: Size) -> Size {
        self.size
    }
}

struct SceneDetails {
    scene: Scene,
}

struct GammaViewAssistant {
    scene_details: Option<SceneDetails>,
}

impl GammaViewAssistant {
    pub fn new() -> Self {
        Self { scene_details: None }
    }
}

impl ViewAssistant for GammaViewAssistant {
    fn resize(&mut self, _new_size: &Size) -> Result<(), Error> {
        self.scene_details = None;
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let mut scene_details = self.scene_details.take().unwrap_or_else(|| {
            let mut builder = SceneBuilder::new().background_color(WHITE_COLOR);
            let gamma_facet = GammaFacet::new(render_context);
            let _ = builder.facet(Box::new(gamma_facet));
            SceneDetails { scene: builder.build() }
        });

        scene_details.scene.render(render_context, ready_event, context)?;

        self.scene_details = Some(scene_details);

        context.request_render();

        Ok(())
    }
}

fn main() -> Result<(), Error> {
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    println!("Gamma Example");
    App::run(make_app_assistant::<GammaAppAssistant>())
}
