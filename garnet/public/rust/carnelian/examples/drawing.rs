// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use carnelian::{
    make_app_assistant, make_message, AnimationMode, App, AppAssistant, Canvas, Color, Coord,
    MappingPixelSink, Point, Rect, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr,
    ViewKey, ViewMessages, ViewMode,
};
use euclid::rect;
use failure::Error;
use fidl_fuchsia_ui_input::{KeyboardEvent, KeyboardEventPhase};
use fuchsia_zircon::{AsHandleRef, ClockId, Signals, Time};
use std::{
    collections::BTreeMap,
    env,
    f32::consts::PI,
    io::{self, Read},
    thread,
};

/// Convenience function that can be called from main and causes the Fuchsia process being
/// run over ssh to be terminated when the user hits control-C.
pub fn wait_for_close() {
    if let Some(argument) = env::args().next() {
        if !argument.starts_with("/tmp") {
            return;
        }
    }

    thread::spawn(move || loop {
        let mut input = [0; 1];
        match io::stdin().read_exact(&mut input) {
            Ok(()) => {}
            Err(_) => {
                std::process::exit(0);
            }
        }
    });
}

#[derive(Default)]
struct DrawingAppAssistant;

impl AppAssistant for DrawingAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant_canvas(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        let r = rect(0.0, 0.0, 0.0, 0.0);
        Ok(Box::new(DrawingViewAssistant::new(r)?))
    }

    fn get_mode(&self) -> ViewMode {
        ViewMode::Canvas
    }

    fn signals_wait_event(&self) -> bool {
        return true;
    }
}

struct DrawingViewAssistant {
    bounds: Rect,
    start: Time,
    bg_color: Color,
    color1: Color,
    color2: Color,
    color3: Color,
    color4: Color,
    color5: Color,
    last_radius_map: BTreeMap<u64, Coord>,
}

impl DrawingViewAssistant {
    pub fn new(bounds: Rect) -> Result<DrawingViewAssistant, Error> {
        let bg_color = Color::from_hash_code("#EBD5B3")?;
        let color1 = Color::from_hash_code("#B7410E")?;
        let color2 = Color::from_hash_code("#008080")?;
        let color3 = Color::from_hash_code("#FF00FF")?;
        let color4 = Color::from_hash_code("#00008B")?;
        let color5 = Color::from_hash_code("#7B68EE")?;

        Ok(DrawingViewAssistant {
            bounds,
            start: Time::get(ClockId::Monotonic),
            bg_color,
            color1,
            color2,
            color3,
            color4,
            color5,
            last_radius_map: BTreeMap::new(),
        })
    }

    pub fn inval_circle(pt: &Point, radius: Coord, canvas: &mut Canvas<MappingPixelSink>) {
        let diameter = radius * 2.0;
        let top_left = *pt - Point::new(radius, radius);
        let circle_bounds = Rect::new(top_left.to_point(), Size::new(diameter, diameter));
        canvas.add_to_update_area(&circle_bounds);
    }
}

impl ViewAssistant for DrawingViewAssistant {
    fn setup(&mut self, _: &ViewAssistantContext<'_>) -> Result<(), Error> {
        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        const SPEED: f32 = 0.25;
        const SECONDS_PER_NANOSECOND: f32 = 1e-9;

        let canvas = &mut context.canvas.as_ref().unwrap().borrow_mut();

        let new_bounds = Rect::new(Point::zero(), context.size);
        self.bounds = new_bounds;

        let min_dimension = self.bounds.size.width.min(self.bounds.size.height);
        let grid_size = min_dimension / 8.0;

        let v = grid_size * 3.0;
        let pt = Point::new(v, v);

        if let Some(last_radius) = self.last_radius_map.get(&canvas.id) {
            Self::inval_circle(&pt, *last_radius, canvas);
        }

        canvas.fill_rect(&self.bounds, self.bg_color);

        let r = Rect::new(Point::new(grid_size, grid_size), Size::new(grid_size, grid_size));
        canvas.fill_rect(&r, self.color1);

        let time_now = Time::get(ClockId::Monotonic);
        let t = ((time_now.into_nanos() - self.start.into_nanos()) as f32
            * SECONDS_PER_NANOSECOND
            * SPEED)
            % 1.0;
        let angle = t * PI * 2.0;
        let circle_size = grid_size;
        let radius = angle.cos() * circle_size + circle_size;
        self.last_radius_map.insert(canvas.id, radius);
        Self::inval_circle(&pt, radius, canvas);
        canvas.fill_circle(&pt, radius, self.color2);

        let rr = Rect::new(
            Point::new(3.0 * grid_size, grid_size),
            Size::new(grid_size * 2.0, grid_size),
        );
        canvas.fill_roundrect(&rr, grid_size / 8.0, self.color3);

        let r_clipped = Rect::new(
            Point::new(-grid_size, grid_size * 3.0),
            Size::new(grid_size * 2.0, grid_size),
        );
        canvas.fill_roundrect(&r_clipped, grid_size / 6.0, self.color4);

        let r_clipped2 = Rect::new(
            Point::new(
                self.bounds.size.width - grid_size,
                self.bounds.size.height - grid_size / 3.0,
            ),
            Size::new(grid_size * 4.0, grid_size * 2.0),
        );
        canvas.fill_roundrect(&r_clipped2, grid_size / 5.0, self.color5);

        canvas.reset_update_area();

        if let Some(wait_event) = context.wait_event.as_ref() {
            wait_event.as_handle_ref().signal(Signals::NONE, Signals::EVENT_SIGNALED)?;
        }

        Ok(())
    }

    fn handle_keyboard_event(
        &mut self,
        context: &mut ViewAssistantContext<'_>,
        keyboard_event: &KeyboardEvent,
    ) -> Result<(), Error> {
        if keyboard_event.code_point == ' ' as u32
            && keyboard_event.phase == KeyboardEventPhase::Pressed
        {
            self.color3 = Color::from_hash_code("#FFFFFF")?;
        }
        context.queue_message(make_message(ViewMessages::Update));
        Ok(())
    }

    fn initial_animation_mode(&mut self) -> AnimationMode {
        return AnimationMode::EveryFrame;
    }
}

fn main() -> Result<(), Error> {
    println!("drawing: started");
    wait_for_close();
    App::run(make_app_assistant::<DrawingAppAssistant>())
}
