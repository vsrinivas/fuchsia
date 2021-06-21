// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    component::Component,
    core::{Core, CoreContext, ObjectRef, OnAdded, Property},
    shapes::paint::{Color32, ShapePaintMutator},
    status_code::StatusCode,
    PaintColor,
};

#[derive(Debug)]
pub struct SolidColor {
    component: Component,
    shape_paint_mutator: ShapePaintMutator,
    color_value: Property<Color32>,
}

impl ObjectRef<'_, SolidColor> {
    pub fn color_value(&self) -> Color32 {
        self.color_value.get()
    }

    pub fn set_color_value(&self, color_value: Color32) {
        self.color_value.set(color_value);
        self.render_opacity_changed();
    }
}

impl ObjectRef<'_, SolidColor> {
    pub(crate) fn render_opacity_changed(&self) {
        let mutator = self.cast::<ShapePaintMutator>();
        mutator.render_paint().borrow_mut().color =
            PaintColor::Solid(self.color_value().mul_opacity(mutator.render_opacity()));
    }
}

impl Core for SolidColor {
    parent_types![(component, Component), (shape_paint_mutator, ShapePaintMutator)];

    properties![(37, color_value, set_color_value), component];
}

impl OnAdded for ObjectRef<'_, SolidColor> {
    on_added!([on_added_clean, import], Component);

    fn on_added_dirty(&self, context: &dyn CoreContext) -> StatusCode {
        let component = self.cast::<Component>();

        let code = component.on_added_dirty(context);
        if code != StatusCode::Ok {
            return code;
        }

        let mutator = self.cast::<ShapePaintMutator>();
        if let Some(parent) = component.parent() {
            if mutator.init_paint_mutator(parent.cast()) {
                self.render_opacity_changed();
                return StatusCode::Ok;
            }
        }

        StatusCode::MissingObject
    }
}

impl Default for SolidColor {
    fn default() -> Self {
        Self {
            component: Component::default(),
            shape_paint_mutator: ShapePaintMutator::default(),
            color_value: Property::new(Color32::new(0xFF74_7474)),
        }
    }
}
