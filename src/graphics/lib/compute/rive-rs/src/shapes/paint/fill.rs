// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::RefCell, rc::Rc};

use crate::{
    core::{Core, Object, ObjectRef, OnAdded, Property},
    math::Mat,
    renderer::Style,
    shapes::{
        paint::{ShapePaint, ShapePaintMutator},
        path_space::PathSpace,
        CommandPath, FillRule,
    },
    RenderPaint, Renderer,
};

#[derive(Debug, Default)]
pub struct Fill {
    shape_paint: ShapePaint,
    fill_rule: Property<FillRule>,
}

impl ObjectRef<'_, Fill> {
    pub fn fill_rule(&self) -> FillRule {
        self.fill_rule.get()
    }

    pub fn set_fill_rule(&self, fill_rule: FillRule) {
        self.fill_rule.set(fill_rule);
    }
}

impl ObjectRef<'_, Fill> {
    pub fn init_render_paint(
        &self,
        mutator: Object<ShapePaintMutator>,
    ) -> Option<Rc<RefCell<RenderPaint>>> {
        let render_paint = self.cast::<ShapePaint>().init_render_paint(mutator).unwrap();
        render_paint.borrow_mut().style = Style::Fill;

        Some(render_paint)
    }

    pub fn path_space(&self) -> PathSpace {
        PathSpace::LOCAL
    }

    pub fn draw(&self, renderer: &mut impl Renderer, path: &CommandPath, transform: Mat) {
        if !self.cast::<ShapePaint>().is_visible() {
            return;
        }

        let render_paint = self.cast::<ShapePaint>().render_paint();
        render_paint.borrow_mut().fill_rule = self.fill_rule();

        renderer.draw(path, transform, &*render_paint.borrow());
    }
}

impl Core for Fill {
    parent_types![(shape_paint, ShapePaint)];

    properties![(40, fill_rule, set_fill_rule), shape_paint];
}

impl OnAdded for ObjectRef<'_, Fill> {
    on_added!(ShapePaint);
}
