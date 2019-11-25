// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use carnelian::{
    set_node_color, AnimationMode, App, AppAssistant, Color, ViewAssistant, ViewAssistantContext,
    ViewAssistantPtr, ViewKey,
};
use chrono::prelude::*;
use failure::Error;
use fuchsia_scenic::{Rectangle, RoundedRectangle, SessionPtr, ShapeNode};
use std::f32::consts::PI;

const MIN_SEC_HAND_ANGLE_RADIANS: f32 = (PI * 2.0) / 60.0;
const HOUR_HAND_ANGLE_RADIANS: f32 = (PI * 2.0) / 12.0;

struct ClockAppAssistant;

impl AppAssistant for ClockAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(
        &mut self,
        _: ViewKey,
        session: &SessionPtr,
    ) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(ClockViewAssistant {
            background_node: ShapeNode::new(session.clone()),
            hour_hand_node: ShapeNode::new(session.clone()),
            minute_hand_node: ShapeNode::new(session.clone()),
            second_hand_node: ShapeNode::new(session.clone()),
        }))
    }
}

struct ClockViewAssistant {
    background_node: ShapeNode,
    hour_hand_node: ShapeNode,
    minute_hand_node: ShapeNode,
    second_hand_node: ShapeNode,
}

impl ClockViewAssistant {
    fn position_hand(
        context: &ViewAssistantContext<'_>,
        node: &ShapeNode,
        hand_width: f32,
        hand_height: f32,
        radius: f32,
        angle: f32,
        z: f32,
    ) {
        let center_x = context.size.width * 0.5;
        let center_y = context.size.height * 0.5;
        node.set_shape(&RoundedRectangle::new(
            context.session().clone(),
            hand_width,
            hand_height,
            radius,
            radius,
            radius,
            radius,
        ));
        node.set_translation(center_x + hand_width / 2.0 - radius, center_y, z);
        node.set_anchor(-hand_width / 2.0 + radius / 2.0, 0.0, 0.0);
        node.set_rotation(0.0, 0.0, (angle * 0.5).sin(), (angle * 0.5).cos());
    }
}

impl ViewAssistant for ClockViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        context.root_node().add_child(&self.background_node);
        set_node_color(
            context.session(),
            &self.background_node,
            &Color::from_hash_code("#B31B1B")?,
        );

        let hand_color = Color::from_hash_code("#A9A9A9")?;
        context.root_node().add_child(&self.hour_hand_node);
        set_node_color(context.session(), &self.hour_hand_node, &hand_color);

        context.root_node().add_child(&self.minute_hand_node);
        set_node_color(context.session(), &self.minute_hand_node, &hand_color);

        context.root_node().add_child(&self.second_hand_node);
        set_node_color(context.session(), &self.second_hand_node, &hand_color);

        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        let local: DateTime<Local> = Local::now();

        let nanos = local.nanosecond() as f32 / 1_000_000_000.0;
        let fractional_seconds = local.second() as f32 + nanos;
        let fractional_minutes = local.minute() as f32 + (fractional_seconds / 60.0);

        let hour_angle = (local.hour() as f32 + (fractional_minutes / 60.0))
            * HOUR_HAND_ANGLE_RADIANS
            - PI / 2.0;
        const MIN_SEC_HAND_FACTOR: f32 = 0.5;
        const HOUR_HAND_FACTOR: f32 = 0.3;
        const HOUR_HAND_Z: f32 = -9.0;
        const MINUTE_HAND_Z: f32 = HOUR_HAND_Z + 1.0;
        const SECOND_HAND_Z: f32 = MINUTE_HAND_Z + 1.0;

        let min_size = context.size.width.min(context.size.height);

        let center_x = context.size.width * 0.5;
        let center_y = context.size.height * 0.5;
        self.background_node.set_shape(&Rectangle::new(
            context.session().clone(),
            context.size.width,
            context.size.height,
        ));
        self.background_node.set_translation(center_x, center_y, 0.0);
        let hand_width = min_size * HOUR_HAND_FACTOR;
        let hand_height = 10.0;
        let radius = 10.0;
        Self::position_hand(
            context,
            &self.hour_hand_node,
            hand_width,
            hand_height,
            radius,
            hour_angle,
            HOUR_HAND_Z,
        );

        let minute_angle = fractional_minutes * MIN_SEC_HAND_ANGLE_RADIANS - PI / 2.0;

        let hand_width = min_size * MIN_SEC_HAND_FACTOR;
        Self::position_hand(
            context,
            &self.minute_hand_node,
            hand_width,
            hand_height,
            radius,
            minute_angle,
            MINUTE_HAND_Z,
        );

        let second_angle = fractional_seconds * MIN_SEC_HAND_ANGLE_RADIANS - PI / 2.0;

        let hand_width = min_size * MIN_SEC_HAND_FACTOR;
        let hand_height = hand_width * 0.02;
        self.second_hand_node.set_shape(&RoundedRectangle::new(
            context.session().clone(),
            hand_width,
            hand_height,
            radius,
            radius,
            radius,
            radius,
        ));
        Self::position_hand(
            context,
            &self.second_hand_node,
            hand_width,
            hand_height,
            radius,
            second_angle,
            SECOND_HAND_Z,
        );

        Ok(())
    }

    fn initial_animation_mode(&mut self) -> AnimationMode {
        return AnimationMode::EveryFrame;
    }
}

fn main() -> Result<(), Error> {
    let assistant = ClockAppAssistant {};
    App::run(Box::new(assistant))
}
