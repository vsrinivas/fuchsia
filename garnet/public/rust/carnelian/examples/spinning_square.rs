// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use carnelian::{
    App, AppAssistant, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey,
    ViewMessages, APP,
};
use failure::Error;
use fidl_fuchsia_ui_gfx::{self as gfx, ColorRgba};
use fuchsia_async::{self as fasync, Interval};
use fuchsia_scenic::{Material, Rectangle, SessionPtr, ShapeNode};
use fuchsia_zircon::{ClockId, Duration, Time};
use futures::StreamExt;
use parking_lot::Mutex;
use std::{any::Any, cell::RefCell, f32::consts::PI};

struct SpinningSquareAppAssistant {}

impl AppAssistant for SpinningSquareAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, session: &SessionPtr) -> Result<ViewAssistantPtr, Error> {
        Ok(Mutex::new(RefCell::new(Box::new(
            SpinningSquareViewAssistant {
                background_node: ShapeNode::new(session.clone()),
                spinning_square_node: ShapeNode::new(session.clone()),
                width: 0.0,
                height: 0.0,
                start: Time::get(ClockId::Monotonic),
            },
        ))))
    }
}

struct SpinningSquareViewAssistant {
    background_node: ShapeNode,
    spinning_square_node: ShapeNode,
    width: f32,
    height: f32,
    start: Time,
}

impl SpinningSquareViewAssistant {
    fn setup_timer(key: ViewKey) {
        let timer = Interval::new(Duration::from_millis(10));
        let f = timer
            .map(move |_| {
                let mut app = APP.lock();
                app.send_message(key, &ViewMessages::Update);
            })
            .collect::<()>();
        fasync::spawn(f);
    }
}

impl ViewAssistant for SpinningSquareViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        context
            .import_node
            .resource()
            .set_event_mask(gfx::METRICS_EVENT_MASK);
        context.import_node.add_child(&self.background_node);
        let material = Material::new(context.session.clone());
        material.set_color(ColorRgba {
            red: 0xb7,
            green: 0x41,
            blue: 0x0e,
            alpha: 0xff,
        });
        self.background_node.set_material(&material);

        context.import_node.add_child(&self.spinning_square_node);
        let material = Material::new(context.session.clone());
        material.set_color(ColorRgba {
            red: 0xff,
            green: 0x00,
            blue: 0xff,
            alpha: 0xff,
        });
        self.spinning_square_node.set_material(&material);
        Self::setup_timer(context.key);
        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        self.width = context.width;
        self.height = context.height;
        const SPEED: f32 = 0.25;
        const SECONDS_PER_NANOSECOND: f32 = 1e-9;

        let center_x = self.width * 0.5;
        let center_y = self.height * 0.5;
        self.background_node.set_shape(&Rectangle::new(
            context.session.clone(),
            self.width,
            self.height,
        ));
        self.background_node
            .set_translation(center_x, center_y, 0.0);
        let square_size = self.width.min(self.height) * 0.6;
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
        self.spinning_square_node
            .set_translation(center_x, center_y, 8.0);
        self.spinning_square_node
            .set_rotation(0.0, 0.0, (angle * 0.5).sin(), (angle * 0.5).cos());
        Ok(())
    }

    fn handle_message(&mut self, _message: &Any) {
        // If spinning square had any custom messages they
        // would be handled here.
    }
}

fn main() -> Result<(), Error> {
    let assistant = SpinningSquareAppAssistant {};
    App::run(Box::new(assistant))
}
