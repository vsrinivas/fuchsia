// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::Cell;

use crate::{
    component::Component,
    component_dirt::ComponentDirt,
    core::{Core, Object, ObjectRef, OnAdded},
    drawable::Drawable,
    dyn_vec::DynVec,
    math::Mat,
    option_cell::OptionCell,
    shapes::{Path, PathComposer, PathSpace, ShapePaintContainer},
    transform_component::TransformComponent,
    Renderer,
};

#[derive(Debug, Default)]
pub struct Shape {
    drawable: Drawable,
    shape_paint_container: ShapePaintContainer,
    path_composer: OptionCell<Object<PathComposer>>,
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
        self.path_composer
            .get()
            .expect("path_composer shoudl already be set on Shape")
            .as_ref()
            .cast::<Component>()
            .add_dirt(ComponentDirt::PATH, true);

        self.cast::<ShapePaintContainer>().invalidate_stroke_effects();
    }

    pub fn path_composer(&self) -> Option<Object<PathComposer>> {
        self.path_composer.get()
    }

    pub fn set_path_composer(&self, path_composer: Object<PathComposer>) {
        self.path_composer.set(Some(path_composer));
    }

    pub fn draw(&self, renderer: &mut impl Renderer, transform: Mat) {
        // todo!("clip");

        let path_composer =
            self.path_composer().expect("path_composer should already be set on Shape");

        for shape_paint in self.cast::<ShapePaintContainer>().shape_paints() {
            let shape_paint = shape_paint.as_ref();

            if !shape_paint.is_visible() {
                continue;
            }

            if shape_paint.path_space() & PathSpace::LOCAL == PathSpace::LOCAL {
                let transform = transform * self.cast::<TransformComponent>().world_transform();
                path_composer.as_ref().with_local_path(|path| {
                    shape_paint.draw(
                        renderer,
                        path.expect("local_path should already be set on PathComposer"),
                        transform,
                    );
                });
            } else {
                path_composer.as_ref().with_world_path(|path| {
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
    on_added!(Drawable);
}
