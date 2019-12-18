// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use carnelian::{
    make_message, set_node_color, AnimationMode, App, AppAssistant, Color, ViewAssistant,
    ViewAssistantContext, ViewAssistantPtr, ViewKey, ViewMessages,
};
use failure::{Error, ResultExt};
use fidl::endpoints::{RequestStream, ServiceMarker};
use fidl_fidl_examples_echo::{EchoMarker, EchoRequest, EchoRequestStream};
use fidl_fuchsia_ui_input::{KeyboardEvent, KeyboardEventPhase};
use fuchsia_async as fasync;
use fuchsia_scenic::{Rectangle, RoundedRectangle, SessionPtr, ShapeNode};
use fuchsia_zircon::{ClockId, Time};
use futures::prelude::*;
use std::f32::consts::PI;

const BACKGROUND_Z: f32 = 0.0;
const SQUARE_Z: f32 = BACKGROUND_Z - 8.0;

struct SpinningSquareAppAssistant;

impl AppAssistant for SpinningSquareAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(
        &mut self,
        _: ViewKey,
        session: &SessionPtr,
    ) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(SpinningSquareViewAssistant {
            background_node: ShapeNode::new(session.clone()),
            spinning_square_node: ShapeNode::new(session.clone()),
            rounded: false,
            start: Time::get(ClockId::Monotonic),
        }))
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
        fasync::spawn_local(
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
            .unwrap_or_else(|e: failure::Error| eprintln!("{:?}", e)),
        );
    }
}

struct SpinningSquareViewAssistant {
    background_node: ShapeNode,
    spinning_square_node: ShapeNode,
    rounded: bool,
    start: Time,
}

impl ViewAssistant for SpinningSquareViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        set_node_color(
            context.session(),
            &self.background_node,
            &Color { r: 0xb7, g: 0x41, b: 0x0e, a: 0xff },
        );
        set_node_color(
            context.session(),
            &self.spinning_square_node,
            &Color { r: 0xff, g: 0x00, b: 0xff, a: 0xff },
        );
        context.root_node().add_child(&self.background_node);
        context.root_node().add_child(&self.spinning_square_node);
        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        const SPEED: f32 = 0.25;
        const SECONDS_PER_NANOSECOND: f32 = 1e-9;

        let center_x = context.size.width * 0.5;
        let center_y = context.size.height * 0.5;
        self.background_node.set_shape(&Rectangle::new(
            context.session().clone(),
            context.size.width,
            context.size.height,
        ));
        self.background_node.set_translation(center_x, center_y, BACKGROUND_Z);
        let square_size = context.size.width.min(context.size.height) * 0.6;
        let t = ((context.presentation_time.into_nanos() - self.start.into_nanos()) as f32
            * SECONDS_PER_NANOSECOND
            * SPEED)
            % 1.0;
        let angle = t * PI * 2.0;
        if self.rounded {
            let corner_radius = (square_size / 4.0).ceil();
            self.spinning_square_node.set_shape(&RoundedRectangle::new(
                context.session().clone(),
                square_size,
                square_size,
                corner_radius,
                corner_radius,
                corner_radius,
                corner_radius,
            ));
        } else {
            self.spinning_square_node.set_shape(&Rectangle::new(
                context.session().clone(),
                square_size,
                square_size,
            ));
        }
        self.spinning_square_node.set_translation(center_x, center_y, SQUARE_Z);
        self.spinning_square_node.set_rotation(0.0, 0.0, (angle * 0.5).sin(), (angle * 0.5).cos());
        Ok(())
    }

    fn initial_animation_mode(&mut self) -> AnimationMode {
        return AnimationMode::EveryFrame;
    }

    fn handle_keyboard_event(
        &mut self,
        context: &mut ViewAssistantContext<'_>,
        keyboard_event: &KeyboardEvent,
    ) -> Result<(), Error> {
        if keyboard_event.code_point == ' ' as u32
            && keyboard_event.phase == KeyboardEventPhase::Pressed
        {
            self.rounded = !self.rounded;
        }
        context.queue_message(make_message(ViewMessages::Update));
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    let assistant = SpinningSquareAppAssistant {};
    App::run(Box::new(assistant))
}
