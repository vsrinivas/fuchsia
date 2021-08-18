// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    carnelian::{
        app::Config,
        color::Color,
        drawing::DisplayRotation,
        input::{self},
        render::{rive::load_rive, Context as RenderContext},
        scene::{
            facets::{FacetId, RiveFacet, SetSizeMessage},
            scene::{Scene, SceneBuilder},
        },
        App, AppAssistant, AppAssistantPtr, AppContext, AssistantCreatorFunc, LocalBoxFuture, Size,
        ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey,
    },
    fuchsia_trace::{duration, duration_begin, duration_end},
    fuchsia_trace_provider,
    fuchsia_zircon::{Event, Time},
    rive_rs::{self as rive},
    std::path::Path,
};

/// Rive.
#[derive(Debug, FromArgs)]
#[argh(name = "rive-rs")]
struct Args {
    /// display rotatation
    #[argh(option)]
    rotation: Option<DisplayRotation>,

    /// rive file to load (default is juice.riv)
    #[argh(option, default = "String::from(\"juice.riv\")")]
    file: String,

    /// playback speed (default is 1.0)
    #[argh(option, default = "1.0")]
    playback_speed: f32,

    /// background color (default is white)
    #[argh(option, from_str_fn(color_from_str))]
    background: Option<Color>,

    /// artboard name (default is first artboard found)
    #[argh(option)]
    artboard: Option<String>,
}

fn color_from_str(value: &str) -> Result<Color, String> {
    Color::from_hash_code(value).map_err(|err| err.to_string())
}

struct RiveAppAssistant {
    app_context: AppContext,
    display_rotation: DisplayRotation,
    filename: String,
    playback_speed: f32,
    background: Color,
    artboard: Option<String>,
}

impl RiveAppAssistant {
    fn new(app_context: &AppContext) -> Self {
        let args: Args = argh::from_env();
        let display_rotation = args.rotation.unwrap_or(DisplayRotation::Deg0);
        let filename = args.file;
        let playback_speed = args.playback_speed;
        let background = args.background.unwrap_or(Color::white());
        let artboard = args.artboard;

        Self {
            app_context: app_context.clone(),
            display_rotation,
            filename,
            playback_speed,
            background,
            artboard,
        }
    }
}

impl AppAssistant for RiveAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, view_key: ViewKey) -> Result<ViewAssistantPtr, Error> {
        let file = load_rive(&Path::new("/pkg/data/static").join(self.filename.clone()))?;
        let playback_speed = self.playback_speed;
        let background = self.background;
        let artboard = self.artboard.clone();

        Ok(Box::new(RiveViewAssistant::new(
            &self.app_context,
            view_key,
            file,
            playback_speed,
            background,
            artboard,
        )))
    }

    fn filter_config(&mut self, config: &mut Config) {
        config.display_rotation = self.display_rotation;
    }
}

struct SceneDetails {
    scene: Scene,
    rive: FacetId,
    artboard: rive::Object<rive::Artboard>,
    animations: Vec<(rive::animation::LinearAnimationInstance, bool)>,
}

struct RiveViewAssistant {
    app_context: AppContext,
    view_key: ViewKey,
    file: rive::File,
    playback_speed: f32,
    background: Color,
    artboard: Option<String>,
    last_presentation_time: Option<Time>,
    scene_details: Option<SceneDetails>,
}

impl RiveViewAssistant {
    fn new(
        app_context: &AppContext,
        view_key: ViewKey,
        file: rive::File,
        playback_speed: f32,
        background: Color,
        artboard: Option<String>,
    ) -> RiveViewAssistant {
        let background = Color { r: background.r, g: background.g, b: background.b, a: 255 };

        RiveViewAssistant {
            app_context: app_context.clone(),
            view_key,
            file,
            playback_speed,
            background,
            artboard,
            last_presentation_time: None,
            scene_details: None,
        }
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
            if index < scene_details.animations.len() {
                scene_details.animations[index].1 = !scene_details.animations[index].1;
                self.app_context.request_render(self.view_key);
            }
        }
    }

    fn adjust_playback_speed(&mut self, factor: f32) {
        self.playback_speed *= factor;
        self.app_context.request_render(self.view_key);
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
        let mut scene_details = self.scene_details.take().unwrap_or_else(|| {
            let mut builder = SceneBuilder::new().background_color(self.background).mutable(false);
            let artboards: Vec<_> = self
                .file
                .artboards()
                .map(|artboard| artboard.cast::<rive::Component>().as_ref().name())
                .collect();
            println!("artboards: {:#?}", artboards);
            let artboard = self
                .artboard
                .as_ref()
                .map_or_else(|| self.file.artboard(), |name| self.file.get_artboard(name))
                .expect("failed to get artboard");
            let artboard_ref = artboard.as_ref();
            let animations: Vec<_> = artboard_ref
                .animations()
                .enumerate()
                .map(|(i, animation)| {
                    let name = animation.cast::<rive::animation::Animation>().as_ref().name();
                    format!("{}:{}", i, name)
                })
                .collect();
            println!("animations: {:#?}", animations);
            let rive_facet = RiveFacet::new(context.size, artboard.clone());
            let rive = builder.facet(Box::new(rive_facet));
            let scene = builder.build();

            // Create animation instances and enable animation at index 0.
            let animations: Vec<(rive::animation::LinearAnimationInstance, bool)> = artboard_ref
                .animations()
                .enumerate()
                .map(|(i, animation)| {
                    (rive::animation::LinearAnimationInstance::new(animation), i == 0)
                })
                .collect();

            SceneDetails { scene, rive, artboard, animations }
        });

        let presentation_time = context.presentation_time;
        let elapsed = if let Some(last_presentation_time) = self.last_presentation_time {
            const NANOS_PER_SECOND: f32 = 1_000_000_000.0;
            (presentation_time - last_presentation_time).into_nanos() as f32 / NANOS_PER_SECOND
        } else {
            0.0
        };
        self.last_presentation_time = Some(presentation_time);

        // Apply playback speed to elapsed time.
        let elapsed = elapsed * self.playback_speed;

        let mut request_render = false;
        for (animation_instance, is_animating) in scene_details.animations.iter_mut() {
            if *is_animating {
                duration!("gfx", "rive::animation::advance");
                animation_instance.advance(elapsed);
                animation_instance.apply(scene_details.artboard.clone(), 1.0);
            }
            if animation_instance.is_done() {
                animation_instance.reset();
                *is_animating = false;
            } else {
                request_render = true;
            }
        }
        duration_begin!("gfx", "rive::artboard::advance");
        let artboard_ref = scene_details.artboard.as_ref();
        artboard_ref.advance(elapsed);
        duration_end!("gfx", "rive::artboard::advance");

        scene_details.scene.render(render_context, ready_event, context)?;
        self.scene_details = Some(scene_details);
        if request_render {
            context.request_render();
        }
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
        const MINUS: u32 = '-' as u32;
        const EQUAL: u32 = '=' as u32;
        if let Some(code_point) = keyboard_event.code_point {
            if keyboard_event.phase == input::keyboard::Phase::Pressed {
                match code_point {
                    // "0-9" can be used to toggle animations on/off.
                    ZERO..=NINE => self.toggle_animation((code_point - ZERO) as usize),
                    // "-" can be used to decrease playback speed.
                    MINUS => self.adjust_playback_speed(0.5),
                    // "=" can be used to increase playback speed.
                    EQUAL => self.adjust_playback_speed(2.0),
                    _ => {}
                }
            }
        }
        Ok(())
    }
}

fn make_app_assistant_fut(
    app_context: &AppContext,
) -> LocalBoxFuture<'_, Result<AppAssistantPtr, Error>> {
    let f = async move {
        let assistant = Box::new(RiveAppAssistant::new(app_context));
        Ok::<AppAssistantPtr, Error>(assistant)
    };
    Box::pin(f)
}

fn make_app_assistant() -> AssistantCreatorFunc {
    Box::new(make_app_assistant_fut)
}

fn main() -> Result<(), Error> {
    fuchsia_trace_provider::trace_provider_create_with_fdio();
    App::run(make_app_assistant())
}
