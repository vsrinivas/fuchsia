// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    component::Component,
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property},
    shapes::paint::{Color32, LinearGradient},
    status_code::StatusCode,
};

#[derive(Debug)]
pub struct GradientStop {
    component: Component,
    color: Property<Color32>,
    position: Property<f32>,
}

impl ObjectRef<'_, GradientStop> {
    pub fn color(&self) -> Color32 {
        self.color.get()
    }

    pub fn set_color(&self, color: Color32) {
        if self.color() == color {
            return;
        }

        self.color.set(color);
        self.linear_gradient().as_ref().mark_gradient_dirty();
    }

    pub fn position(&self) -> f32 {
        self.position.get()
    }

    pub fn set_position(&self, position: f32) {
        if self.position() == position {
            return;
        }

        self.position.set(position);
        self.linear_gradient().as_ref().mark_stops_dirty();
    }
}

impl ObjectRef<'_, GradientStop> {
    fn linear_gradient(&self) -> Object<LinearGradient> {
        self.cast::<Component>().parent().expect("GradientStop should have a parent").cast()
    }
}

impl Core for GradientStop {
    parent_types![(component, Component)];

    properties![(38, color, set_color), (39, position, set_position), component];
}

impl OnAdded for ObjectRef<'_, GradientStop> {
    on_added!([on_added_clean, import], Component);

    fn on_added_dirty(&self, context: &dyn CoreContext) -> StatusCode {
        let component = self.cast::<Component>();

        let code = component.on_added_dirty(context);
        if code != StatusCode::Ok {
            return code;
        }

        if let Some(linear_gradient) =
            component.parent().and_then(|parent| parent.try_cast::<LinearGradient>())
        {
            linear_gradient.as_ref().push_stop(self.as_object());
            StatusCode::Ok
        } else {
            StatusCode::MissingObject
        }
    }
}

impl Default for GradientStop {
    fn default() -> Self {
        Self {
            component: Component::default(),
            color: Property::new(Color32::from(0xFFFF_FFFF)),
            position: Property::new(0.0),
        }
    }
}
