// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use carnelian::{make_message, set_node_color, Color, Point, Rect, ViewAssistantContext};
use failure::Error;
use fidl_fuchsia_ui_input::{PointerEvent, PointerEventPhase};
use fuchsia_scenic::{EntityNode, Rectangle, SessionPtr, ShapeNode};

use crate::{message::VoilaMessage, TOGGLE_Z};

pub struct Toggle {
    background_node: ShapeNode,
    container: EntityNode,
    pub bounds: Rect,
    bg_color_on: Color,
    bg_color_off: Color,
    is_on: bool,
}

impl Toggle {
    pub fn new(session: &SessionPtr) -> Result<Toggle, Error> {
        let toggle = Toggle {
            background_node: ShapeNode::new(session.clone()),
            container: EntityNode::new(session.clone()),
            bounds: Rect::zero(),
            bg_color_on: Color::from_hash_code("#00C0DE")?,
            bg_color_off: Color::from_hash_code("#404040")?,
            is_on: false,
        };

        toggle.container.add_child(&toggle.background_node);
        Ok(toggle)
    }

    pub fn toggle(&mut self) {
        self.is_on = !self.is_on;
    }

    pub fn update(&mut self, bounds: &Rect, session: &SessionPtr) -> Result<(), Error> {
        self.container.set_translation(
            bounds.origin.x + (bounds.size.width / 2.0),
            bounds.origin.y + (bounds.size.height / 2.0),
            TOGGLE_Z,
        );

        let color = if self.is_on { self.bg_color_on } else { self.bg_color_off };
        set_node_color(session, &self.background_node, &color);

        // Record bounds for hit testing.
        self.bounds = bounds.clone();

        self.background_node.set_shape(&Rectangle::new(
            session.clone(),
            self.bounds.size.width,
            self.bounds.size.height,
        ));

        Ok(())
    }

    pub fn node(&self) -> &EntityNode {
        &self.container
    }

    pub fn handle_pointer_event(
        &mut self,
        context: &mut ViewAssistantContext,
        pointer_event: &PointerEvent,
        key: u32,
    ) {
        match pointer_event.phase {
            PointerEventPhase::Down => {}
            PointerEventPhase::Add => {}
            PointerEventPhase::Hover => {}
            PointerEventPhase::Move => {}
            PointerEventPhase::Up => {
                if self.bounds.contains(&Point::new(pointer_event.x, pointer_event.y)) {
                    context
                        .queue_message(make_message(VoilaMessage::ReplicaConnectivityToggled(key)));
                }
            }
            PointerEventPhase::Remove => {}
            PointerEventPhase::Cancel => {}
        }
    }
}
