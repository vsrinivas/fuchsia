// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use carnelian::{
    app::Config,
    color::Color,
    drawing::{path_for_rectangle, path_for_rounded_rectangle},
    input::{self},
    make_app_assistant,
    render::{BlendMode, Context as RenderContext, Fill, FillRule, Layer, Path, Style},
    scene::{
        facets::{Facet, FacetId},
        scene::{Scene, SceneBuilder},
        LayerGroup,
    },
    App, AppAssistant, Coord, Rect, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr,
    ViewKey,
};
use euclid::{point2, size2, vec2, Angle, Transform2D};
use fidl::endpoints::{ProtocolMarker, RequestStream};
use fidl_test_placeholders::{EchoMarker, EchoRequest, EchoRequestStream};
use fuchsia_async as fasync;
use fuchsia_zircon::{Event, Time};
use futures::prelude::*;
use std::{any::Any, f32::consts::PI};

#[derive(Default)]
struct SpinningSquareAppAssistant;

impl AppAssistant for SpinningSquareAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        SpinningSquareViewAssistant::new()
    }

    /// Return the list of names of services this app wants to provide
    fn outgoing_services_names(&self) -> Vec<&'static str> {
        [EchoMarker::NAME].to_vec()
    }

    /// Handle a request to connect to a service provided by this app
    fn handle_service_connection_request(
        &mut self,
        _service_name: &str,
        channel: fasync::Channel,
    ) -> Result<(), Error> {
        Self::create_echo_server(channel, false);
        Ok(())
    }

    fn filter_config(&mut self, config: &mut Config) {
        config.display_resource_release_delay = std::time::Duration::new(0, 0);
    }
}

impl SpinningSquareAppAssistant {
    fn create_echo_server(channel: fasync::Channel, quiet: bool) {
        fasync::Task::local(
            async move {
                let mut stream = EchoRequestStream::from_channel(channel);
                while let Some(EchoRequest::EchoString { value, responder }) =
                    stream.try_next().await.context("error running echo server")?
                {
                    if !quiet {
                        println!("Spinning Square received echo request for string {:?}", value);
                    }
                    responder
                        .send(value.as_ref().map(|s| &**s))
                        .context("error sending response")?;
                    if !quiet {
                        println!("echo response sent successfully");
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| eprintln!("{:?}", e)),
        )
        .detach();
    }
}

struct SceneDetails {
    scene: Scene,
    square: FacetId,
}

#[derive(Debug)]
pub struct ToggleRoundedMessage {}

struct SpinningSquareFacet {
    square_color: Color,
    rounded: bool,
    start: Time,
    square_path: Option<Path>,
    size: Size,
}

impl SpinningSquareFacet {
    fn new(square_color: Color, start: Time, size: Size) -> Self {
        Self { square_color, rounded: false, start, square_path: None, size }
    }

    fn clone_square_path(&self) -> Path {
        self.square_path.as_ref().expect("square_path").clone()
    }
}

impl Facet for SpinningSquareFacet {
    fn update_layers(
        &mut self,
        size: Size,
        layer_group: &mut dyn LayerGroup,
        render_context: &mut RenderContext,
        view_context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        const SPEED: f32 = 0.25;
        const SECONDS_PER_NANOSECOND: f32 = 1e-9;
        const SQUARE_PATH_SIZE: Coord = 1.0;
        const SQUARE_PATH_SIZE_2: Coord = SQUARE_PATH_SIZE / 2.0;
        const CORNER_RADIUS: Coord = SQUARE_PATH_SIZE / 4.0;

        let center_x = size.width * 0.5;
        let center_y = size.height * 0.5;
        self.size = size;
        let square_size = size.width.min(size.height) * 0.6;
        let presentation_time = view_context.presentation_time;
        let t = ((presentation_time.into_nanos() - self.start.into_nanos()) as f32
            * SECONDS_PER_NANOSECOND
            * SPEED)
            % 1.0;
        let angle = t * PI * 2.0;

        if self.square_path.is_none() {
            let top_left = point2(-SQUARE_PATH_SIZE_2, -SQUARE_PATH_SIZE_2);
            let square = Rect::new(top_left, size2(SQUARE_PATH_SIZE, SQUARE_PATH_SIZE));
            let square_path = if self.rounded {
                path_for_rounded_rectangle(&square, CORNER_RADIUS, render_context)
            } else {
                path_for_rectangle(&square, render_context)
            };
            self.square_path.replace(square_path);
        }

        let transformation = Transform2D::rotation(Angle::radians(angle))
            .then_scale(square_size, square_size)
            .then_translate(vec2(center_x, center_y));
        let mut raster_builder = render_context.raster_builder().expect("raster_builder");
        raster_builder.add(&self.clone_square_path(), Some(&transformation));
        let square_raster = raster_builder.build();

        layer_group.insert(
            0,
            Layer {
                raster: square_raster,
                clip: None,
                style: Style {
                    fill_rule: FillRule::NonZero,
                    fill: Fill::Solid(self.square_color),
                    blend_mode: BlendMode::Over,
                },
            },
        );
        Ok(())
    }

    fn handle_message(&mut self, msg: Box<dyn Any>) {
        if let Some(_) = msg.downcast_ref::<ToggleRoundedMessage>() {
            self.rounded = !self.rounded;
            self.square_path = None;
        }
    }

    fn calculate_size(&self, _available: Size) -> Size {
        self.size
    }
}

struct SpinningSquareViewAssistant {
    background_color: Color,
    square_color: Color,
    start: Time,
    scene_details: Option<SceneDetails>,
}

impl SpinningSquareViewAssistant {
    fn new() -> Result<ViewAssistantPtr, Error> {
        let square_color = Color { r: 0xbb, g: 0x00, b: 0xff, a: 0xbb };
        let background_color = Color { r: 0x3f, g: 0x8a, b: 0x99, a: 0xff };
        let start = Time::get_monotonic();
        Ok(Box::new(SpinningSquareViewAssistant {
            background_color,
            square_color,
            start,
            scene_details: None,
        }))
    }

    fn toggle_rounded(&mut self) {
        if let Some(scene_details) = self.scene_details.as_mut() {
            scene_details
                .scene
                .send_message(&scene_details.square, Box::new(ToggleRoundedMessage {}));
        }
    }

    fn move_backward(&mut self) {
        if let Some(scene_details) = self.scene_details.as_mut() {
            scene_details
                .scene
                .move_facet_backward(scene_details.square)
                .unwrap_or_else(|e| println!("error in move_facet_backward: {}", e));
        }
    }

    fn move_forward(&mut self) {
        if let Some(scene_details) = self.scene_details.as_mut() {
            scene_details
                .scene
                .move_facet_forward(scene_details.square)
                .unwrap_or_else(|e| println!("error in move_facet_forward: {}", e));
        }
    }
}

impl ViewAssistant for SpinningSquareViewAssistant {
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
            let mut builder = SceneBuilder::new().background_color(self.background_color);
            let mut square = None;
            builder.group().stack().center().contents(|builder| {
                let square_facet =
                    SpinningSquareFacet::new(self.square_color, self.start, context.size);
                square = Some(builder.facet(Box::new(square_facet)));
                const STRIPE_COUNT: usize = 5;
                let stripe_height = context.size.height / (STRIPE_COUNT * 2 + 1) as f32;
                const STRIPE_WIDTH_RATIO: f32 = 0.8;
                let stripe_size = size2(context.size.width * STRIPE_WIDTH_RATIO, stripe_height);
                builder.group().column().max_size().space_evenly().contents(|builder| {
                    for _ in 0..STRIPE_COUNT {
                        builder.rectangle(stripe_size, Color::white());
                    }
                });
            });
            let square = square.expect("square");
            let mut scene = builder.build();
            scene.layout(context.size);
            SceneDetails { scene, square }
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
        const SPACE: u32 = ' ' as u32;
        const B: u32 = 'b' as u32;
        const F: u32 = 'f' as u32;
        if let Some(code_point) = keyboard_event.code_point {
            if keyboard_event.phase == input::keyboard::Phase::Pressed
                || keyboard_event.phase == input::keyboard::Phase::Repeat
            {
                match code_point {
                    SPACE => self.toggle_rounded(),
                    B => self.move_backward(),
                    F => self.move_forward(),
                    _ => println!("code_point = {}", code_point),
                }
            }
        }
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    fuchsia_trace_provider::trace_provider_create_with_fdio();
    App::run(make_app_assistant::<SpinningSquareAppAssistant>())
}
