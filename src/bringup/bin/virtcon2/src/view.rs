// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    carnelian::{
        color::Color,
        drawing::{load_font, path_for_rounded_rectangle, FontFace},
        render::{BlendMode, Context as RenderContext, Fill, FillRule, Layer, Path, Style},
        scene::{
            facets::{Facet, TextFacetOptions, TextHorizontalAlignment, TextVerticalAlignment},
            scene::{Scene, SceneBuilder},
            LayerGroup,
        },
        Coord, Rect, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr,
    },
    euclid::{point2, size2, Angle, Transform2D},
    fuchsia_zircon::{Event, Time},
    std::{f32::consts::PI, path::PathBuf},
};

// TODO(fxb/74636): Replace this with a rive facet.
struct SquareFacet {
    color: Color,
    start: Time,
    size: f32,
    path: Option<Path>,
}

impl SquareFacet {
    fn new(color: Color, start: Time, size: f32) -> Self {
        Self { color, start, size, path: None }
    }

    fn clone_path(&self) -> Path {
        self.path.as_ref().expect("path").clone()
    }
}

impl Facet for SquareFacet {
    fn update_layers(
        &mut self,
        _size: Size,
        layer_group: &mut LayerGroup,
        render_context: &mut RenderContext,
    ) -> Result<(), Error> {
        const SPEED: f32 = 0.25;
        const SECONDS_PER_NANOSECOND: f32 = 1e-9;
        const SQUARE_PATH_SIZE: Coord = 1.0;
        const SQUARE_PATH_SIZE_2: Coord = SQUARE_PATH_SIZE / 2.0;
        const CORNER_RADIUS: Coord = SQUARE_PATH_SIZE / 4.0;

        let presentation_time = Time::get_monotonic();
        let t = ((presentation_time.into_nanos() - self.start.into_nanos()) as f32
            * SECONDS_PER_NANOSECOND
            * SPEED)
            % 1.0;
        let angle = t * PI * 2.0;

        if self.path.is_none() {
            let top_left = point2(-SQUARE_PATH_SIZE_2, -SQUARE_PATH_SIZE_2);
            let rect = Rect::new(top_left, size2(SQUARE_PATH_SIZE, SQUARE_PATH_SIZE));
            let path = path_for_rounded_rectangle(&rect, CORNER_RADIUS, render_context);
            self.path.replace(path);
        }

        let transformation =
            Transform2D::rotation(Angle::radians(angle)).then_scale(self.size, self.size);
        let mut raster_builder = render_context.raster_builder().expect("raster_builder");
        raster_builder.add(&self.clone_path(), Some(&transformation));
        let raster = raster_builder.build();
        layer_group.replace_all(std::iter::once(Layer {
            raster: raster,
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(self.color),
                blend_mode: BlendMode::Over,
            },
        }));
        Ok(())
    }

    fn get_size(&self) -> Size {
        size2(self.size, self.size)
    }
}

struct SceneDetails {
    scene: Scene,
}

pub struct VirtualConsoleViewAssistant {
    background_color: Color,
    foreground_color: Color,
    face: FontFace,
    start: Time,
    scene_details: Option<SceneDetails>,
}

// TODO(reveman): Read from boot arguments and configuration file.
const BACKGROUND_COLOR: &'static str = "#000000";
const FOREGROUND_COLOR: &'static str = "#FFFFFF";
const FONT: &'static str = "/pkg/data/fonts/RobotoSlab-Regular.ttf";
const LABEL_TEXT: &'static str = "welcome to fuchsia";

impl VirtualConsoleViewAssistant {
    pub fn new() -> Result<ViewAssistantPtr, Error> {
        let background_color = Color::from_hash_code(BACKGROUND_COLOR)?;
        let foreground_color = Color::from_hash_code(FOREGROUND_COLOR)?;
        let face = load_font(PathBuf::from(FONT))?;
        let start = Time::get_monotonic();
        let scene_details = None;

        Ok(Box::new(VirtualConsoleViewAssistant {
            background_color,
            foreground_color,
            face,
            start,
            scene_details,
        }))
    }
}

impl ViewAssistant for VirtualConsoleViewAssistant {
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
            let target_size = context.size;
            let min_dimension = target_size.width.min(target_size.height);
            let square_size = (min_dimension / 5.0).ceil().max(64.0);
            let font_size = (min_dimension / 20.0).ceil().min(32.0);
            let padding = (min_dimension / 40.0).ceil().max(8.0);
            let center_x = target_size.width * 0.5;
            let square_y = target_size.height * 0.5 - (square_size / 2.0 + padding);
            let square_position = point2(center_x, square_y);
            let label_y = target_size.height * 0.5 + (font_size + padding);
            let label_position = point2(center_x, label_y);
            let mut builder = SceneBuilder::new().background_color(self.background_color);
            let square_facet = SquareFacet::new(self.foreground_color, self.start, square_size);
            let square_facet_id = builder.facet(Box::new(square_facet));
            builder.text(
                self.face.clone(),
                LABEL_TEXT,
                font_size,
                label_position,
                TextFacetOptions {
                    color: self.foreground_color,
                    horizontal_alignment: TextHorizontalAlignment::Center,
                    vertical_alignment: TextVerticalAlignment::Center,
                    ..TextFacetOptions::default()
                },
            );
            let mut scene = builder.build();
            scene.set_facet_location(&square_facet_id, square_position);
            SceneDetails { scene }
        });

        scene_details.scene.render(render_context, ready_event, context)?;
        self.scene_details = Some(scene_details);
        context.request_render();
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn can_create_view() -> Result<(), Error> {
        let _ = VirtualConsoleViewAssistant::new()?;
        Ok(())
    }
}
