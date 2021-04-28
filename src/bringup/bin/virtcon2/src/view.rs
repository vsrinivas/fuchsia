// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    carnelian::{
        color::Color,
        drawing::{load_font, FontFace},
        render::{rive::load_rive, Context as RenderContext},
        scene::{
            facets::{RiveFacet, TextFacetOptions, TextHorizontalAlignment, TextVerticalAlignment},
            layout::{CrossAxisAlignment, Flex, FlexOptions, MainAxisAlignment, MainAxisSize},
            scene::{Scene, SceneBuilder},
        },
        Point, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr,
    },
    euclid::size2,
    fuchsia_zircon::{Event, Time},
    rive_rs::{self as rive},
    std::path::PathBuf,
};

pub struct VirtualConsoleViewAssistant {
    background_color: Color,
    foreground_color: Color,
    face: FontFace,
    // Artboard has weak references to data owned by file.
    _logo: rive::File,
    artboard: rive::Object<rive::Artboard>,
    animation: rive::animation::LinearAnimationInstance,
    last_presentation_time: Option<Time>,
    scene: Option<Scene>,
}

// TODO(reveman): Read from boot arguments and configuration file.
const BACKGROUND_COLOR: &'static str = "#000000";
const FOREGROUND_COLOR: &'static str = "#FFFFFF";
const FONT: &'static str = "/pkg/data/fonts/RobotoSlab-Regular.ttf";
const LOGO: &'static str = "/pkg/data/logo.riv";
const LABEL_TEXT: &'static str = "welcome to fuchsia";

impl VirtualConsoleViewAssistant {
    pub fn new() -> Result<ViewAssistantPtr, Error> {
        let background_color = Color::from_hash_code(BACKGROUND_COLOR)?;
        let foreground_color = Color::from_hash_code(FOREGROUND_COLOR)?;
        let face = load_font(PathBuf::from(FONT))?;
        let logo = load_rive(PathBuf::from(LOGO))?;
        let artboard = logo.artboard().ok_or_else(|| anyhow!("missing artboard"))?;
        let first_animation =
            artboard.as_ref().animations().next().ok_or_else(|| anyhow!("missing animation"))?;
        let animation = rive::animation::LinearAnimationInstance::new(first_animation);
        let last_presentation_time = None;
        let scene = None;

        Ok(Box::new(VirtualConsoleViewAssistant {
            background_color,
            foreground_color,
            face,
            _logo: logo,
            artboard,
            animation,
            last_presentation_time,
            scene,
        }))
    }
}

impl ViewAssistant for VirtualConsoleViewAssistant {
    fn resize(&mut self, _new_size: &Size) -> Result<(), Error> {
        self.scene = None;
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let mut scene = self.scene.take().unwrap_or_else(|| {
            let target_size = context.size;
            let min_dimension = target_size.width.min(target_size.height);
            let logo_size = (min_dimension / 2.0).ceil();
            let font_size = (min_dimension / 20.0).ceil().min(32.0);

            let mut builder = SceneBuilder::new().background_color(self.background_color);
            builder.group().column().max_size().main_align(MainAxisAlignment::Center).contents(
                |builder| {
                    builder.start_group(
                        "splash",
                        Flex::with_options_ptr(FlexOptions::row(
                            MainAxisSize::Max,
                            MainAxisAlignment::Center,
                            CrossAxisAlignment::Center,
                        )),
                    );
                    builder.facet(Box::new(RiveFacet::new(
                        size2(logo_size, logo_size),
                        self.artboard.clone(),
                    )));
                    builder.text(
                        self.face.clone(),
                        LABEL_TEXT,
                        font_size,
                        Point::zero(),
                        TextFacetOptions {
                            color: self.foreground_color,
                            horizontal_alignment: TextHorizontalAlignment::Left,
                            vertical_alignment: TextVerticalAlignment::Top,
                            ..TextFacetOptions::default()
                        },
                    );
                    builder.end_group();
                },
            );

            let mut scene = builder.build();
            scene.layout(target_size);
            scene
        });

        let presentation_time = Time::get_monotonic();
        let elapsed = if let Some(last_presentation_time) = self.last_presentation_time {
            const NANOS_PER_SECOND: f32 = 1_000_000_000.0;
            (presentation_time - last_presentation_time).into_nanos() as f32 / NANOS_PER_SECOND
        } else {
            0.0
        };
        self.last_presentation_time = Some(presentation_time);

        let artboard_ref = self.artboard.as_ref();
        self.animation.advance(elapsed);
        self.animation.apply(self.artboard.clone(), 1.0);
        artboard_ref.advance(elapsed);

        scene.render(render_context, ready_event, context)?;
        self.scene = Some(scene);

        if !self.animation.is_done() {
            context.request_render();
        }

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
