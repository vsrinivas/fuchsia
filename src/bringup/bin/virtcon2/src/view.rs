// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::colors::ColorScheme,
    anyhow::{anyhow, Error},
    carnelian::{
        render::{rive::load_rive, Context as RenderContext},
        scene::{
            facets::RiveFacet,
            scene::{Scene, SceneBuilder},
        },
        Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr,
    },
    fidl_fuchsia_hardware_display::VirtconMode,
    fuchsia_zircon::{Event, Time},
    rive_rs::{self as rive},
    std::path::PathBuf,
};

pub struct VirtualConsoleViewAssistant {
    color_scheme: ColorScheme,
    round_scene_corners: bool,
    // Artboard has weak references to data owned by file.
    _logo: rive::File,
    artboard: rive::Object<rive::Artboard>,
    animation: rive::animation::LinearAnimationInstance,
    last_presentation_time: Option<Time>,
    scene: Option<Scene>,
}

const LOGO: &'static str = "/pkg/data/logo.riv";

impl VirtualConsoleViewAssistant {
    pub fn new(
        color_scheme: ColorScheme,
        round_scene_corners: bool,
    ) -> Result<ViewAssistantPtr, Error> {
        let logo = load_rive(PathBuf::from(LOGO))?;
        let artboard = logo.artboard().ok_or_else(|| anyhow!("missing artboard"))?;
        let first_animation =
            artboard.as_ref().animations().next().ok_or_else(|| anyhow!("missing animation"))?;
        let animation = rive::animation::LinearAnimationInstance::new(first_animation);
        let last_presentation_time = None;
        let scene = None;

        Ok(Box::new(VirtualConsoleViewAssistant {
            color_scheme,
            round_scene_corners,
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
            let mut builder = SceneBuilder::new()
                .background_color(self.color_scheme.back)
                .round_scene_corners(self.round_scene_corners);
            builder.facet(Box::new(RiveFacet::new(context.size, self.artboard.clone())));
            builder.build()
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

        // Switch to fallback mode when animation ends so primary client can
        // take over. Otherwise, request another frame.
        if self.animation.is_done() {
            if let Some(fb) = context.frame_buffer.as_ref() {
                fb.borrow_mut().set_virtcon_mode(VirtconMode::Fallback)?;
            }
        } else {
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
        let _ = VirtualConsoleViewAssistant::new(ColorScheme::default(), false)?;
        Ok(())
    }
}
