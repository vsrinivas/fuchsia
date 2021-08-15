// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use carnelian::{
    color::Color,
    drawing::{load_font, FontFace},
    input::{self},
    render::Context as RenderContext,
    scene::{
        facets::{
            FacetId, SetColorMessage, SetTextMessage, TextFacetOptions, TextHorizontalAlignment,
            TextVerticalAlignment,
        },
        scene::{Scene, SceneBuilder},
    },
    App, AppAssistant, AppAssistantPtr, AppContext, AssistantCreatorFunc, Coord, LocalBoxFuture,
    Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey,
};
use euclid::point2;
use fuchsia_zircon::Event;
use lipsum::lipsum_words;
use std::path::PathBuf;

const LINE_THICKNESS: Coord = 2.0;
const BASELINE_INDENT: Coord = 0.15;
const PADDING: Coord = 0.005;

struct FontAppAssistant {
    app_context: AppContext,
}

impl AppAssistant for FontAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, view_key: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(FontMetricsViewAssistant::new(&self.app_context, view_key)?)
    }
}

struct SceneDetails {
    scene: Scene,
    sample_title: FacetId,
    sample_paragraph: FacetId,
    lines: Vec<FacetId>,
}

struct FontMetricsViewAssistant {
    app_context: AppContext,
    view_key: ViewKey,
    sample_title: String,
    sample_paragraph: String,
    sample_faces: Vec<FontFace>,
    sample_index: usize,
    sample_size_divisor: f32,
    label_face: FontFace,
    scene_details: Option<SceneDetails>,
    line_color: Color,
    round_scene_corners: bool,
}

impl FontMetricsViewAssistant {
    fn new(app_context: &AppContext, view_key: ViewKey) -> Result<ViewAssistantPtr, Error> {
        let ss = load_font(PathBuf::from("/pkg/data/fonts/ShortStack-Regular.ttf"))?;
        let ds = load_font(PathBuf::from("/pkg/data/fonts/DroidSerif-Regular.ttf"))?;
        let qr = load_font(PathBuf::from("/pkg/data/fonts/Quintessential-Regular.ttf"))?;
        let sample_faces = vec![ss, ds, qr];
        let label_face = load_font(PathBuf::from("/pkg/data/fonts/Roboto-Regular.ttf"))?;
        let (title, para) = Self::new_sample_pair();
        Ok(Box::new(Self {
            app_context: app_context.clone(),
            view_key,
            sample_title: title,
            sample_paragraph: para,
            sample_faces,
            sample_index: 0,
            sample_size_divisor: 6.0,
            label_face,
            scene_details: None,
            line_color: Color::new(),
            round_scene_corners: true,
        }))
    }

    fn new_sample_pair() -> (String, String) {
        (lipsum_words(2), lipsum_words(25))
    }

    fn update(&mut self) {
        self.scene_details = None;
        self.app_context.request_render(self.view_key);
    }

    fn update_text(&mut self, title: String, para: String) {
        self.sample_title = title.to_string();
        self.sample_paragraph = para.to_string();
        if let Some(scene_details) = self.scene_details.as_mut() {
            scene_details.scene.send_message(
                &scene_details.sample_title.clone(),
                Box::new(SetTextMessage { text: self.sample_title.clone() }),
            );
            scene_details.scene.send_message(
                &scene_details.sample_paragraph.clone(),
                Box::new(SetTextMessage { text: self.sample_paragraph.clone() }),
            );
        }
        self.app_context.request_render(self.view_key);
    }

    fn new_sample_text(&mut self) {
        let (title, para) = Self::new_sample_pair();
        self.update_text(title, para);
    }

    fn new_blank_text(&mut self) {
        let title = "".to_string();
        let para = "\nnow is the time\n\n".to_string();
        self.update_text(title, para);
    }

    fn show_next_face(&mut self) {
        self.sample_index = (self.sample_index + 1) % self.sample_faces.len();
        self.update();
    }

    fn increase_sample_size(&mut self) {
        self.sample_size_divisor = (self.sample_size_divisor - 0.5).max(1.0);
        self.update();
    }

    fn decrease_sample_size(&mut self) {
        self.sample_size_divisor = (self.sample_size_divisor + 0.5).min(10.0);
        self.update();
    }

    fn toggle_line_color(&mut self) {
        let black = Color::new();
        if self.line_color == black {
            self.line_color = Color::white();
        } else {
            self.line_color = black;
        }
        if let Some(scene_details) = self.scene_details.as_mut() {
            let targets: Vec<FacetId> = scene_details.lines.iter().cloned().collect();
            for target in targets {
                scene_details
                    .scene
                    .send_message(&target, Box::new(SetColorMessage { color: self.line_color }));
            }
        }
        self.app_context.request_render(self.view_key);
    }

    fn toggle_round_scene_corners(&mut self) {
        self.round_scene_corners = !self.round_scene_corners;
        if let Some(scene_details) = self.scene_details.as_mut() {
            scene_details.scene.round_scene_corners(self.round_scene_corners);
        }
        self.app_context.request_render(self.view_key);
    }
}

impl ViewAssistant for FontMetricsViewAssistant {
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
            let mut builder = SceneBuilder::new()
                .background_color(Color::white())
                .round_scene_corners(self.round_scene_corners);
            let size = context.size;
            let text_size = size.height.min(size.width) / self.sample_size_divisor;
            let ascent = self.sample_faces[self.sample_index].ascent(text_size);
            let descent = self.sample_faces[self.sample_index].descent(text_size);
            let sample_text_size = text_size / 3.0;
            let baseline_location = size.height / 3.0;
            let baseline_left = size.width * BASELINE_INDENT;
            let baseline_width = size.width * (1.0 - BASELINE_INDENT * 2.0);
            let baseline_right = baseline_left + baseline_width;

            let sample_title = builder.text(
                self.sample_faces[self.sample_index].clone(),
                &self.sample_title,
                text_size,
                point2(size.width / 2.0, baseline_location),
                TextFacetOptions {
                    horizontal_alignment: TextHorizontalAlignment::Center,
                    ..TextFacetOptions::default()
                },
            );

            let paragraph_baseline_location = size.height / 2.0;

            let sample_paragraph = builder.text(
                self.sample_faces[self.sample_index].clone(),
                &self.sample_paragraph,
                sample_text_size,
                point2(baseline_left, paragraph_baseline_location),
                TextFacetOptions {
                    horizontal_alignment: TextHorizontalAlignment::Left,
                    max_width: Some(baseline_width),
                    ..TextFacetOptions::default()
                },
            );

            let mut lines = Vec::new();

            lines.push(builder.h_line(
                baseline_width,
                LINE_THICKNESS,
                self.line_color,
                Some(point2(baseline_left, baseline_location)),
            ));

            let ascent_x = baseline_left + size.width * BASELINE_INDENT / 5.0;
            lines.push(builder.v_line(
                ascent,
                LINE_THICKNESS,
                self.line_color,
                Some(point2(ascent_x, baseline_location - ascent)),
            ));
            lines.push(builder.v_line(
                -descent,
                LINE_THICKNESS,
                self.line_color,
                Some(point2(ascent_x, baseline_location)),
            ));

            let label_size = size.height.min(size.width) / 30.0;

            builder.text(
                self.label_face.clone(),
                "baseline",
                label_size,
                point2(baseline_right + PADDING * size.width, baseline_location),
                TextFacetOptions {
                    vertical_alignment: TextVerticalAlignment::Center,
                    ..TextFacetOptions::default()
                },
            );

            builder.text(
                self.label_face.clone(),
                "ascent",
                label_size,
                point2(ascent_x - PADDING * size.width, baseline_location - ascent / 2.0),
                TextFacetOptions {
                    vertical_alignment: TextVerticalAlignment::Center,
                    horizontal_alignment: TextHorizontalAlignment::Right,
                    ..TextFacetOptions::default()
                },
            );

            builder.text(
                self.label_face.clone(),
                "descent",
                label_size,
                point2(ascent_x - PADDING * size.width, baseline_location - descent / 2.0),
                TextFacetOptions {
                    vertical_alignment: TextVerticalAlignment::Center,
                    horizontal_alignment: TextHorizontalAlignment::Right,
                    ..TextFacetOptions::default()
                },
            );

            let scene = builder.build();
            SceneDetails { scene, sample_paragraph, sample_title, lines }
        });

        scene_details.scene.render(render_context, ready_event, context)?;
        self.scene_details = Some(scene_details);
        Ok(())
    }

    fn handle_keyboard_event(
        &mut self,
        _context: &mut ViewAssistantContext,
        _event: &input::Event,
        keyboard_event: &input::keyboard::Event,
    ) -> Result<(), Error> {
        const B: u32 = 'b' as u32;
        const C: u32 = 'c' as u32;
        const F: u32 = 'f' as u32;
        const K: u32 = 'k' as u32;
        const T: u32 = 't' as u32;
        const PLUS: u32 = '+' as u32;
        const EQUALS: u32 = '=' as u32;
        const MINUS: u32 = '-' as u32;
        if let Some(code_point) = keyboard_event.code_point {
            if keyboard_event.phase == input::keyboard::Phase::Pressed
                || keyboard_event.phase == input::keyboard::Phase::Repeat
            {
                match code_point {
                    B => self.new_blank_text(),
                    T => self.new_sample_text(),
                    F => self.show_next_face(),
                    PLUS | EQUALS => self.increase_sample_size(),
                    MINUS => self.decrease_sample_size(),
                    C => self.toggle_line_color(),
                    K => self.toggle_round_scene_corners(),
                    _ => println!("code_point = {}", code_point),
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
        let assistant = Box::new(FontAppAssistant { app_context: app_context.clone() });
        Ok::<AppAssistantPtr, Error>(assistant)
    };
    Box::pin(f)
}

pub fn make_app_assistant() -> AssistantCreatorFunc {
    Box::new(make_app_assistant_fut)
}

fn main() -> Result<(), Error> {
    App::run(make_app_assistant())
}
