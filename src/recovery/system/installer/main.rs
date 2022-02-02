// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![allow(dead_code)]

use anyhow::Error;
use argh::FromArgs;
use carnelian::{
    app::Config,
    color::Color,
    drawing::{load_font, DisplayRotation, FontFace},
    render::{rive::load_rive, Context as RenderContext},
    scene::{
        facets::{RiveFacet, TextFacetOptions, TextHorizontalAlignment},
        scene::{Scene, SceneBuilder},
    },
    App, AppAssistant, AppAssistantPtr, AppContext, AssistantCreatorFunc, LocalBoxFuture, Size,
    ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey,
};
use euclid::{point2, size2};
use fuchsia_zircon::Event;
use rive_rs::{self as rive};
use std::path::PathBuf;

const INSTALLER_HEADLINE: &'static str = "Fuchsia Workstation Installer";

const BG_COLOR: Color = Color { r: 238, g: 23, b: 128, a: 255 };
const HEADING_COLOR: Color = Color::new();

const LOGO_IMAGE_PATH: &str = "/pkg/data/logo.riv";

/// Installer
#[derive(Debug, FromArgs)]
#[argh(name = "recovery")]
struct Args {
    /// rotate
    #[argh(option)]
    rotation: Option<DisplayRotation>,
}

struct InstallerAppAssistant {
    app_context: AppContext,
    display_rotation: DisplayRotation,
}

impl InstallerAppAssistant {
    fn new(app_context: AppContext) -> Self {
        let args: Args = argh::from_env();
        Self { app_context, display_rotation: args.rotation.unwrap_or(DisplayRotation::Deg0) }
    }
}

impl AppAssistant for InstallerAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, view_key: ViewKey) -> Result<ViewAssistantPtr, Error> {
        let file = load_rive(LOGO_IMAGE_PATH).ok();
        Ok(Box::new(InstallerViewAssistant::new(
            &self.app_context,
            view_key,
            file,
            INSTALLER_HEADLINE,
        )?))
    }

    fn filter_config(&mut self, config: &mut Config) {
        config.display_rotation = self.display_rotation;
    }
}

struct RenderResources {
    scene: Scene,
}

impl RenderResources {
    fn new(
        _render_context: &mut RenderContext,
        file: &Option<rive::File>,
        target_size: Size,
        heading: &str,
        face: &FontFace,
    ) -> Self {
        let min_dimension = target_size.width.min(target_size.height);
        let logo_edge = min_dimension * 0.24;
        let text_size = min_dimension / 10.0;
        let top_margin = 0.255;

        let mut builder = SceneBuilder::new().background_color(BG_COLOR).round_scene_corners(true);

        let logo_size: Size = size2(logo_edge, logo_edge);
        // Calculate position for centering the logo image
        let logo_position = {
            let x = target_size.width * 0.8;
            let y = target_size.height * 0.7;
            point2(x, y)
        };

        if let Some(file) = file {
            builder.facet_at_location(
                Box::new(
                    RiveFacet::new_from_file(logo_size, &file, None).expect("facet_from_file"),
                ),
                logo_position,
            );
        }
        let heading_text_location =
            point2(target_size.width / 2.0, top_margin + (target_size.height * 0.05));
        builder.text(
            face.clone(),
            &heading,
            text_size,
            heading_text_location,
            TextFacetOptions {
                horizontal_alignment: TextHorizontalAlignment::Center,
                color: HEADING_COLOR,
                ..TextFacetOptions::default()
            },
        );

        Self { scene: builder.build() }
    }
}

struct InstallerViewAssistant {
    face: FontFace,
    heading: &'static str,
    app_context: AppContext,
    view_key: ViewKey,
    file: Option<rive::File>,
    render_resources: Option<RenderResources>,
}

impl InstallerViewAssistant {
    fn new(
        app_context: &AppContext,
        view_key: ViewKey,
        file: Option<rive::File>,
        heading: &'static str,
    ) -> Result<InstallerViewAssistant, Error> {
        InstallerViewAssistant::setup(app_context, view_key)?;

        let face = load_font(PathBuf::from("/pkg/data/fonts/Roboto-Regular.ttf"))?;
        Ok(InstallerViewAssistant {
            face,
            heading: heading,
            app_context: app_context.clone(),
            view_key: 0,
            file,
            render_resources: None,
        })
    }

    fn setup(_: &AppContext, _: ViewKey) -> Result<(), Error> {
        Ok(())
    }
}

impl ViewAssistant for InstallerViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        self.view_key = context.key;
        Ok(())
    }

    fn render(
        &mut self,
        _render_context: &mut RenderContext,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        // Emulate the size that Carnelian passes when the display is rotated
        let target_size = context.size;

        if self.render_resources.is_none() {
            self.render_resources = Some(RenderResources::new(
                _render_context,
                &self.file,
                target_size,
                self.heading,
                &self.face,
            ));
        }

        let render_resources = self.render_resources.as_mut().unwrap();
        render_resources.scene.render(_render_context, ready_event, context)?;
        context.request_render();
        Ok(())
    }
}

fn make_app_assistant_fut(
    app_context: &AppContext,
) -> LocalBoxFuture<'_, Result<AppAssistantPtr, Error>> {
    let f = async move {
        let assistant = Box::new(InstallerAppAssistant::new(app_context.clone()));
        Ok::<AppAssistantPtr, Error>(assistant)
    };
    Box::pin(f)
}

fn make_app_assistant() -> AssistantCreatorFunc {
    Box::new(make_app_assistant_fut)
}

fn main() -> Result<(), Error> {
    println!("workstation installer: started.");
    App::run(make_app_assistant())
}
