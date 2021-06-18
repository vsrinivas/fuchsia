// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::RefCell, convert::TryInto, rc::Rc};

use crate::{
    container_component::ContainerComponent,
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property},
    math::Mat,
    option_cell::OptionCell,
    shapes::{
        paint::{shape_paint_mutator::ShapePaintMutator, BlendMode, Fill, Stroke},
        CommandPath, PathSpace, ShapePaintContainer,
    },
    status_code::StatusCode,
    Component, RenderPaint, Renderer,
};

#[derive(Debug)]
pub struct ShapePaint {
    container_component: ContainerComponent,
    is_visible: Property<bool>,
    render_paint: OptionCell<Rc<RefCell<RenderPaint>>>,
    shape_paint_mutator: OptionCell<Object<ShapePaintMutator>>,
}

impl ObjectRef<'_, ShapePaint> {
    pub fn is_visible(&self) -> bool {
        self.is_visible.get()
    }

    pub fn set_is_visible(&self, is_visible: bool) {
        self.is_visible.set(is_visible);
    }
}

impl ObjectRef<'_, ShapePaint> {
    pub fn init_render_paint(
        &self,
        mutator: Object<ShapePaintMutator>,
    ) -> Option<Rc<RefCell<RenderPaint>>> {
        self.shape_paint_mutator.set(Some(mutator));
        self.render_paint.set(Some(Rc::new(RefCell::new(RenderPaint::default()))));

        self.render_paint.get()
    }

    pub fn path_space(&self) -> PathSpace {
        if let Some(fill) = self.try_cast::<Fill>() {
            return fill.path_space();
        }

        if let Some(stroke) = self.try_cast::<Stroke>() {
            return stroke.path_space();
        }

        unreachable!()
    }

    pub fn render_paint(&self) -> Rc<RefCell<RenderPaint>> {
        self.render_paint.get().expect("init_render_paint has not been called yet")
    }

    pub fn set_blend_mode(&self, blend_mode: BlendMode) {
        self.render_paint().borrow_mut().blend_mode = blend_mode;
    }

    pub fn set_render_opacity(&self, render_opacity: f32) {
        self.shape_paint_mutator
            .get()
            .expect("init_paint_mutator has not been called yet")
            .as_ref()
            .set_render_opacity(render_opacity);
    }

    pub fn set_is_clipped(&self, is_clipped: bool) {
        self.render_paint().borrow_mut().is_clipped = is_clipped;
    }

    pub fn draw(&self, renderer: &mut impl Renderer, path: &CommandPath, transform: Mat) {
        if let Some(fill) = self.try_cast::<Fill>() {
            return fill.draw(renderer, path, transform);
        }

        if let Some(stroke) = self.try_cast::<Stroke>() {
            return stroke.draw(renderer, path, transform);
        }

        unreachable!()
    }
}

impl Core for ShapePaint {
    parent_types![(container_component, ContainerComponent)];

    properties![(41, is_visible, set_is_visible), container_component];
}

impl OnAdded for ObjectRef<'_, ShapePaint> {
    on_added!([on_added_dirty, import], ContainerComponent);

    fn on_added_clean(&self, _context: &dyn CoreContext) -> StatusCode {
        let container = self.cast::<Component>().parent().and_then(|parent| {
            let object: Object = parent.into();
            object.try_into().ok()
        });

        if let Some(container) = container {
            let container: Object<ShapePaintContainer> = container;
            container.as_ref().push_paint(self.as_object());

            StatusCode::Ok
        } else {
            StatusCode::MissingObject
        }
    }
}

impl Default for ShapePaint {
    fn default() -> Self {
        Self {
            container_component: ContainerComponent::default(),
            is_visible: Property::new(true),
            render_paint: OptionCell::new(),
            shape_paint_mutator: OptionCell::new(),
        }
    }
}
