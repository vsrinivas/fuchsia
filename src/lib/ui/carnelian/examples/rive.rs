// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    carnelian::{
        color::Color,
        input::{self},
        make_app_assistant,
        render::{rive::load_rive, Context as RenderContext},
        scene::{
            facets::{FacetId, RiveFacet, SetSizeMessage, ToggleAnimationMessage},
            scene::{Scene, SceneBuilder},
        },
        App, AppAssistant, RenderOptions, Size, ViewAssistant, ViewAssistantContext,
        ViewAssistantPtr, ViewKey,
    },
    fuchsia_trace_provider,
    fuchsia_zircon::Event,
    rive_rs::{self as rive},
    std::path::Path,
};

/// Rive.
#[derive(Debug, FromArgs)]
#[argh(name = "rive-rs")]
struct Args {
    /// use spinel (GPU rendering back-end)
    #[argh(switch, short = 's')]
    use_spinel: bool,

    /// rive file to load (default is juice.riv)
    #[argh(option, default = "String::from(\"juice.riv\")")]
    file: String,

    /// background color (default is white)
    #[argh(option, from_str_fn(parse_color))]
    background: Option<Color>,
}

fn parse_color(value: &str) -> Result<Color, String> {
    Color::from_hash_code(value).map_err(|err| err.to_string())
}

#[derive(Default)]
struct RiveAppAssistant {
    use_spinel: bool,
    filename: String,
    background: Option<Color>,
}

impl AppAssistant for RiveAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        let args: Args = argh::from_env();
        self.use_spinel = args.use_spinel;
        self.filename = args.file;
        self.background = args.background;
        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(RiveViewAssistant::new(
            load_rive(Path::new("/pkg/data/static").join(self.filename.clone()))?,
            self.background.take().unwrap_or(Color::white()),
        )))
    }

    fn get_render_options(&self) -> RenderOptions {
        RenderOptions { use_spinel: self.use_spinel, ..RenderOptions::default() }
    }
}

struct SceneDetails {
    scene: Scene,
    rive: FacetId,
}

struct RiveViewAssistant {
    file: rive::File,
    background: Color,
    scene_details: Option<SceneDetails>,
}

impl RiveViewAssistant {
    fn new(file: rive::File, background: Color) -> RiveViewAssistant {
        let background = Color { r: background.r, g: background.g, b: background.b, a: 255 };

        RiveViewAssistant { file, background, scene_details: None }
    }

    fn set_size(&mut self, size: &Size) {
        if let Some(scene_details) = self.scene_details.as_mut() {
            scene_details
                .scene
                .send_message(&scene_details.rive, Box::new(SetSizeMessage { size: *size }));
        }
    }

    fn toggle_animation(&mut self, index: usize) {
        if let Some(scene_details) = self.scene_details.as_mut() {
            scene_details
                .scene
                .send_message(&scene_details.rive, Box::new(ToggleAnimationMessage { index }));
        }
    }
}

impl ViewAssistant for RiveViewAssistant {
    fn resize(&mut self, new_size: &Size) -> Result<(), Error> {
        self.set_size(new_size);
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let file = &self.file;
        let mut scene_details = self.scene_details.take().unwrap_or_else(|| {
            let mut builder = SceneBuilder::new().background_color(self.background);
            let artboard = file.artboard().unwrap();
            let initial_animations = vec![0];
            let rive_facet = RiveFacet::new(context.size, artboard, initial_animations);
            let rive = builder.facet(Box::new(rive_facet));
            let scene = builder.build();
            SceneDetails { scene, rive }
        });

        scene_details.scene.render(render_context, ready_event, context)?;
        self.scene_details = Some(scene_details);
        context.request_render();
        Ok(())
    }

    fn handle_keyboard_event(
        &mut self,
        _context: &mut ViewAssistantContext,
        _event: &input::Event,
        keyboard_event: &input::keyboard::Event,
    ) -> Result<(), Error> {
        const ZERO: u32 = '0' as u32;
        const NINE: u32 = '9' as u32;
        if let Some(code_point) = keyboard_event.code_point {
            if keyboard_event.phase == input::keyboard::Phase::Pressed {
                if (ZERO..=NINE).contains(&code_point) {
                    self.toggle_animation((code_point - ZERO) as usize);
                }
            }
        }
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    fuchsia_trace_provider::trace_provider_create_with_fdio();
    App::run(make_app_assistant::<RiveAppAssistant>())
}
