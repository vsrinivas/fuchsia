// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use carnelian::{
    color::Color,
    geometry::Corners,
    input::{self},
    make_app_assistant,
    render::{
        BlendMode, Composition, Context as RenderContext, Fill, FillRule, Layer, Path, PreClear,
        Raster, RenderExt, Style,
    },
    App, AppAssistant, Coord, Point, Rect, Size, ViewAssistant, ViewAssistantContext,
    ViewAssistantPtr, ViewKey,
};
use euclid::{Angle, Transform2D, Vector2D};
use fidl::endpoints::{RequestStream, ServiceMarker};
use fidl_fidl_examples_echo::{EchoMarker, EchoRequest, EchoRequestStream};
use fuchsia_async as fasync;
use fuchsia_zircon::{AsHandleRef, ClockId, Event, Signals, Time};
use futures::prelude::*;
use std::f32::consts::PI;

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

fn path_for_rectangle(bounds: &Rect, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    path_builder.move_to(bounds.origin);
    path_builder.line_to(bounds.top_right());
    path_builder.line_to(bounds.bottom_right());
    path_builder.line_to(bounds.bottom_left());
    path_builder.line_to(bounds.origin);
    path_builder.build()
}

fn path_for_rounded_rectangle(
    bounds: &Rect,
    corner_radius: Coord,
    render_context: &mut RenderContext,
) -> Path {
    let kappa = 4.0 / 3.0 * (std::f32::consts::PI / 8.0).tan();
    let control_dist = kappa * corner_radius;

    let mut path_builder = render_context.path_builder().expect("path_builder");
    let top_left_arc_start = bounds.origin + Vector2D::new(0.0, corner_radius);
    let top_left_arc_end = bounds.origin + Vector2D::new(corner_radius, 0.0);
    path_builder.move_to(top_left_arc_start);
    let top_left_curve_center = bounds.origin + Vector2D::new(corner_radius, corner_radius);
    let p1 = top_left_curve_center + Vector2D::new(-corner_radius, -control_dist);
    let p2 = top_left_curve_center + Vector2D::new(-control_dist, -corner_radius);
    path_builder.cubic_to(p1, p2, top_left_arc_end);

    let top_right = bounds.top_right();
    let top_right_arc_start = top_right + Vector2D::new(-corner_radius, 0.0);
    let top_right_arc_end = top_right + Vector2D::new(0.0, corner_radius);
    path_builder.line_to(top_right_arc_start);
    let top_right_curve_center = top_right + Vector2D::new(-corner_radius, corner_radius);
    let p1 = top_right_curve_center + Vector2D::new(control_dist, -corner_radius);
    let p2 = top_right_curve_center + Vector2D::new(corner_radius, -control_dist);
    path_builder.cubic_to(p1, p2, top_right_arc_end);

    let bottom_right = bounds.bottom_right();
    let bottom_right_arc_start = bottom_right + Vector2D::new(0.0, -corner_radius);
    let bottom_right_arc_end = bottom_right + Vector2D::new(-corner_radius, 0.0);
    path_builder.line_to(bottom_right_arc_start);
    let bottom_right_curve_center = bottom_right + Vector2D::new(-corner_radius, -corner_radius);
    let p1 = bottom_right_curve_center + Vector2D::new(corner_radius, control_dist);
    let p2 = bottom_right_curve_center + Vector2D::new(control_dist, corner_radius);
    path_builder.cubic_to(p1, p2, bottom_right_arc_end);

    let bottom_left = bounds.bottom_left();
    let bottom_left_arc_start = bottom_left + Vector2D::new(corner_radius, 0.0);
    let bottom_left_arc_end = bottom_left + Vector2D::new(0.0, -corner_radius);
    path_builder.line_to(bottom_left_arc_start);
    let bottom_left_curve_center = bottom_left + Vector2D::new(corner_radius, -corner_radius);
    let p1 = bottom_left_curve_center + Vector2D::new(-control_dist, corner_radius);
    let p2 = bottom_left_curve_center + Vector2D::new(-corner_radius, control_dist);
    path_builder.cubic_to(p1, p2, bottom_left_arc_end);

    path_builder.line_to(top_left_arc_start);
    path_builder.build()
}

struct SpinningSquareViewAssistant {
    background_color: Color,
    square_color: Color,
    rounded: bool,
    start: Time,
    square_raster: Option<Raster>,
    square_path: Option<Path>,
    composition: Composition,
}

impl SpinningSquareViewAssistant {
    fn new() -> Result<ViewAssistantPtr, Error> {
        let square_color = Color { r: 0xff, g: 0x00, b: 0xff, a: 0xff };
        let background_color = Color { r: 0xb7, g: 0x41, b: 0x0e, a: 0xff };
        let start = Time::get_monotonic();
        let composition = Composition::new(background_color);
        Ok(Box::new(SpinningSquareViewAssistant {
            background_color,
            square_color,
            rounded: false,
            start,
            square_raster: None,
            square_path: None,
            composition,
        }))
    }

    fn clone_square_raster(&self) -> Raster {
        self.square_raster.as_ref().expect("square_raster").clone()
    }

    fn clone_square_path(&self) -> Path {
        self.square_path.as_ref().expect("square_path").clone()
    }

    fn toggle_rounded(&mut self) {
        self.rounded = !self.rounded;
        self.square_path = None;
    }
}

impl ViewAssistant for SpinningSquareViewAssistant {
    fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        const SPEED: f32 = 0.25;
        const SECONDS_PER_NANOSECOND: f32 = 1e-9;
        const SQUARE_PATH_SIZE: Coord = 1.0;
        const SQUARE_PATH_SIZE_2: Coord = SQUARE_PATH_SIZE / 2.0;
        const CORNER_RADIUS: Coord = SQUARE_PATH_SIZE / 4.0;

        let center_x = context.size.width * 0.5;
        let center_y = context.size.height * 0.5;
        let square_size = context.size.width.min(context.size.height) * 0.6;
        let t = ((context.presentation_time.into_nanos() - self.start.into_nanos()) as f32
            * SECONDS_PER_NANOSECOND
            * SPEED)
            % 1.0;
        let angle = t * PI * 2.0;

        if self.square_path.is_none() {
            let top_left = Point::new(-SQUARE_PATH_SIZE_2, -SQUARE_PATH_SIZE_2);
            let square = Rect::new(top_left, Size::new(SQUARE_PATH_SIZE, SQUARE_PATH_SIZE));
            let square_path = if self.rounded {
                path_for_rounded_rectangle(&square, CORNER_RADIUS, render_context)
            } else {
                path_for_rectangle(&square, render_context)
            };
            self.square_path.replace(square_path);
        }

        let transformation = Transform2D::create_rotation(Angle::radians(angle))
            .post_scale(square_size, square_size)
            .post_translate(Vector2D::new(center_x, center_y));
        let mut raster_builder = render_context.raster_builder().expect("raster_builder");
        raster_builder.add(&self.clone_square_path(), Some(&transformation));
        self.square_raster = Some(raster_builder.build());

        let layers = std::iter::once(Layer {
            raster: self.clone_square_raster(),
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(self.square_color),
                blend_mode: BlendMode::Over,
            },
        });

        self.composition.replace(.., layers);

        let image = render_context.get_current_image(context);
        let ext = RenderExt {
            pre_clear: Some(PreClear { color: self.background_color }),
            ..Default::default()
        };
        render_context.render(&self.composition, None, image, &ext);
        ready_event.as_handle_ref().signal(Signals::NONE, Signals::EVENT_SIGNALED)?;
        context.request_render();
        Ok(())
    }

    fn handle_keyboard_event(
        &mut self,
        _context: &mut ViewAssistantContext,
        _event: &input::Event,
        keyboard_event: &input::keyboard::Event,
    ) -> Result<(), Error> {
        if let Some(code_point) = keyboard_event.code_point {
            if code_point == ' ' as u32 && keyboard_event.phase == input::keyboard::Phase::Pressed {
                self.toggle_rounded();
            }
        }
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    fuchsia_trace_provider::trace_provider_create_with_fdio();
    App::run(make_app_assistant::<SpinningSquareAppAssistant>())
}
