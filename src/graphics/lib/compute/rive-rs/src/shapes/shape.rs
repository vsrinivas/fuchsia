// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::Cell, rc::Rc};

use crate::{
    component::Component,
    component_dirt::ComponentDirt,
    core::{Core, CoreContext, Object, ObjectRef, OnAdded},
    drawable::Drawable,
    dyn_vec::DynVec,
    math::Mat,
    shapes::{Path, PathComposer, PathSpace, ShapePaintContainer},
    status_code::StatusCode,
    transform_component::TransformComponent,
    Renderer,
};

#[derive(Debug, Default)]
pub struct Shape {
    drawable: Drawable,
    shape_paint_container: ShapePaintContainer,
    path_composer: Rc<PathComposer>,
    paths: DynVec<Object<Path>>,
    want_difference_path: Cell<bool>,
}

impl ObjectRef<'_, Shape> {
    pub fn paths(&self) -> impl Iterator<Item = Object<Path>> + '_ {
        self.paths.iter()
    }

    pub fn want_difference_path(&self) -> bool {
        self.want_difference_path.get()
    }

    pub fn push_path(&self, path: Object<Path>) {
        self.paths.push(path);
    }

    pub fn path_space(&self) -> PathSpace {
        self.cast::<ShapePaintContainer>().path_space()
    }

    pub fn path_changed(&self) {
        self.path_composer().cast::<Component>().add_dirt(ComponentDirt::PATH, true);

        self.cast::<ShapePaintContainer>().invalidate_stroke_effects();
    }

    pub fn path_composer(&self) -> ObjectRef<'_, PathComposer> {
        ObjectRef::from(Rc::clone(&self.path_composer))
    }

    pub fn draw(&self, renderer: &mut impl Renderer, transform: Mat) {
        let mut is_clipped = false;
        if let Some(path) = self.cast::<Drawable>().clip() {
            is_clipped = true;

            let layers = self
                .cast::<ShapePaintContainer>()
                .shape_paints()
                .filter(|shape_paint| shape_paint.as_ref().is_visible())
                .count();

            renderer.clip(&path, transform, layers);
        }

        let path_composer = self.path_composer();

        for shape_paint in self.cast::<ShapePaintContainer>().shape_paints() {
            let shape_paint = shape_paint.as_ref();

            if !shape_paint.is_visible() {
                continue;
            }

            shape_paint.set_is_clipped(is_clipped);

            if shape_paint.path_space() & PathSpace::LOCAL == PathSpace::LOCAL {
                let transform = transform * self.cast::<TransformComponent>().world_transform();
                path_composer.with_local_path(|path| {
                    shape_paint.draw(
                        renderer,
                        path.expect("local_path should already be set on PathComposer"),
                        transform,
                    );
                });
            } else {
                path_composer.with_world_path(|path| {
                    shape_paint.draw(
                        renderer,
                        path.expect("world_path should already be set on PathComposer"),
                        transform,
                    );
                });
            }
        }
    }

    pub fn build_dependencies(&self) {
        self.path_composer().cast::<Component>().build_dependencies();

        self.cast::<TransformComponent>().build_dependencies();

        // Set the blend mode on all the shape paints. If we ever animate this
        // property, we'll need to update it in the update cycle/mark dirty when the
        // blend mode changes.
        for paint in self.cast::<ShapePaintContainer>().shape_paints() {
            paint.as_ref().set_blend_mode(self.cast::<Drawable>().blend_mode());
        }
    }

    pub fn update(&self, value: ComponentDirt) {
        self.cast::<TransformComponent>().update(value);

        if Component::value_has_dirt(value, ComponentDirt::RENDER_OPACITY) {
            for paint in self.cast::<ShapePaintContainer>().shape_paints() {
                paint
                    .as_ref()
                    .set_render_opacity(self.cast::<TransformComponent>().render_opacity());
            }
        }
    }
}

impl Core for Shape {
    parent_types![(drawable, Drawable), (shape_paint_container, ShapePaintContainer)];

    properties!(drawable);
}

impl OnAdded for ObjectRef<'_, Shape> {
    on_added!([on_added_clean, import], Drawable);

    fn on_added_dirty(&self, context: &dyn CoreContext) -> StatusCode {
        self.path_composer.shape.set(Some(self.as_object()));

        self.cast::<TransformComponent>().on_added_dirty(context);
        ObjectRef::from(Rc::clone(&self.path_composer)).on_added_dirty(context)
    }
}
