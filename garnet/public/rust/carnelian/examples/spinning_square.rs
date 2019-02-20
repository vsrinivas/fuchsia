// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use carnelian::{
    App, AppAssistant, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey, ViewMessages,
};
use failure::{Error, ResultExt};
use fidl::endpoints::{RequestStream, ServiceMarker};
use fidl_fidl_examples_echo::{EchoMarker, EchoRequest, EchoRequestStream};
use fidl_fuchsia_ui_gfx::{self as gfx, ColorRgba};
use fuchsia_async::{self as fasync, Interval};
use fuchsia_scenic::{Material, Rectangle, SessionPtr, ShapeNode};
use fuchsia_zircon::{ClockId, Duration, Time};
use futures::prelude::*;
use std::f32::consts::PI;

struct SpinningSquareAppAssistant {}

impl AppAssistant for SpinningSquareAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, session: &SessionPtr) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(SpinningSquareViewAssistant {
            background_node: ShapeNode::new(session.clone()),
            spinning_square_node: ShapeNode::new(session.clone()),
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
                    await!(stream.try_next()).context("error running echo server")?
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
    start: Time,
}

impl SpinningSquareViewAssistant {
    fn setup_timer(key: ViewKey) {
        let timer = Interval::new(Duration::from_millis(10));
        let f = timer
            .map(move |_| {
                App::with(|app| {
                    app.send_message(key, &ViewMessages::Update);
                });
            })
            .collect::<()>();
        fasync::spawn_local(f);
    }
}

impl ViewAssistant for SpinningSquareViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        context.import_node.resource().set_event_mask(gfx::METRICS_EVENT_MASK);
        context.import_node.add_child(&self.background_node);
        let material = Material::new(context.session.clone());
        material.set_color(ColorRgba { red: 0xb7, green: 0x41, blue: 0x0e, alpha: 0xff });
        self.background_node.set_material(&material);

        context.import_node.add_child(&self.spinning_square_node);
        let material = Material::new(context.session.clone());
        material.set_color(ColorRgba { red: 0xff, green: 0x00, blue: 0xff, alpha: 0xff });
        self.spinning_square_node.set_material(&material);
        Self::setup_timer(context.key);
        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        const SPEED: f32 = 0.25;
        const SECONDS_PER_NANOSECOND: f32 = 1e-9;

        let center_x = context.size.width * 0.5;
        let center_y = context.size.height * 0.5;
        self.background_node.set_shape(&Rectangle::new(
            context.session.clone(),
            context.size.width,
            context.size.height,
        ));
        self.background_node.set_translation(center_x, center_y, 0.0);
        let square_size = context.size.width.min(context.size.height) * 0.6;
        let t = ((Time::get(ClockId::Monotonic).nanos() - self.start.nanos()) as f32
            * SECONDS_PER_NANOSECOND
            * SPEED)
            % 1.0;
        let angle = t * PI * 2.0;
        self.spinning_square_node.set_shape(&Rectangle::new(
            context.session.clone(),
            square_size,
            square_size,
        ));
        self.spinning_square_node.set_translation(center_x, center_y, -8.0);
        self.spinning_square_node.set_rotation(0.0, 0.0, (angle * 0.5).sin(), (angle * 0.5).cos());
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    let assistant = SpinningSquareAppAssistant {};
    App::run(Box::new(assistant))
}
