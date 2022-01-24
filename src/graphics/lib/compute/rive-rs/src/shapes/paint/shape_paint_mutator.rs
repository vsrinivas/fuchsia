// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cell::{Cell, RefCell},
    rc::Rc,
};

use crate::{
    component::Component,
    core::{Core, Object, ObjectRef, OnAdded},
    option_cell::OptionCell,
    renderer::{Gradient, PaintColor, RenderPaint},
    shapes::paint::{Fill, LinearGradient, ShapePaint, SolidColor, Stroke},
};

#[derive(Debug)]
pub struct ShapePaintMutator {
    render_opacity: Cell<f32>,
    render_paint: OptionCell<Rc<RefCell<RenderPaint>>>,
}

impl ObjectRef<'_, ShapePaintMutator> {
    pub fn render_opacity(&self) -> f32 {
        self.render_opacity.get()
    }

    pub fn set_render_opacity(&self, render_opacity: f32) {
        if self.render_opacity() == render_opacity {
            return;
        }

        self.render_opacity.set(render_opacity);
        self.render_opacity_changed();
    }
}

impl ObjectRef<'_, ShapePaintMutator> {
    fn render_opacity_changed(&self) {
        match_cast!(self, {
            LinearGradient(linear_gradient) => linear_gradient.mark_gradient_dirty(),
            SolidColor(solid_color) => solid_color.render_opacity_changed(),
        })
    }

    pub fn init_paint_mutator(&self, component: Object<Component>) -> bool {
        match_cast!(component, {
            Fill(fill) => {
                self.render_paint.set(fill.as_ref().init_render_paint(self.as_object()));
                true
            },
            Stroke(stroke) => {
                self.render_paint.set(stroke.as_ref().init_render_paint(self.as_object()));
                true
            },
            ShapePaint(shape_paint) => {
                self.render_paint.set(shape_paint.as_ref().init_render_paint(self.as_object()));
                true
            },
            _ => false,
        })
    }

    pub(crate) fn render_paint(&self) -> Rc<RefCell<RenderPaint>> {
        self.render_paint.get().expect("init_paint_mutator has not been called yet")
    }

    pub(crate) fn set_gradient(&self, gradient: Gradient) {
        self.render_paint().borrow_mut().color = PaintColor::Gradient(gradient);
    }
}

impl Core for ShapePaintMutator {}

impl OnAdded for ObjectRef<'_, ShapePaintMutator> {
    on_added!();
}

impl Default for ShapePaintMutator {
    fn default() -> Self {
        Self { render_opacity: Cell::new(1.0), render_paint: OptionCell::new() }
    }
}
