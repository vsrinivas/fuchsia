// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    component_dirt::ComponentDirt,
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property},
    dyn_vec::DynVec,
    math::{self, Mat},
    node::Node,
    option_cell::OptionCell,
    shapes::{
        command_path::CommandPathBuilder, CommandPath, CubicVertex, PathVertex, PointsPath, Shape,
        StraightVertex,
    },
    status_code::StatusCode,
    Component, TransformComponent,
};

#[derive(Debug, Default)]
pub struct Path {
    node: Node,
    path_flags: Property<u64>,
    shape: OptionCell<Object<Shape>>,
    pub(crate) vertices: DynVec<Object<PathVertex>>,
    command_path: OptionCell<CommandPath>,
}

impl ObjectRef<'_, Path> {
    pub fn path_flags(&self) -> u64 {
        self.path_flags.get()
    }

    pub fn set_path_flags(&self, path_flags: u64) {
        self.path_flags.set(path_flags);
    }
}

impl ObjectRef<'_, Path> {
    pub fn shape(&self) -> Option<Object<Shape>> {
        self.shape.get()
    }

    pub fn transform(&self) -> Mat {
        if let Some(points_path) = self.try_cast::<PointsPath>() {
            return points_path.transform();
        }

        self.cast::<TransformComponent>().world_transform()
    }

    pub fn push_vertex(&self, path_vertex: Object<PathVertex>) {
        self.vertices.push(path_vertex);
    }

    pub(crate) fn vertices(&self) -> impl Iterator<Item = Object<PathVertex>> + '_ {
        self.vertices.iter()
    }

    pub(crate) fn with_command_path(&self, f: impl FnMut(Option<&CommandPath>)) {
        self.command_path.with(f);
    }

    pub fn mark_path_dirty(&self) {
        if let Some(points_path) = self.try_cast::<PointsPath>() {
            points_path.mark_path_dirty();
        }

        self.cast::<Component>().add_dirt(ComponentDirt::PATH, false);

        if let Some(shape) = self.shape() {
            shape.as_ref().path_changed();
        }
    }

    pub fn is_path_closed(&self) -> bool {
        if let Some(points_path) = self.try_cast::<PointsPath>() {
            return points_path.is_path_closed();
        }

        true
    }

    pub fn build_dependencies(&self) {
        self.cast::<TransformComponent>().build_dependencies();
    }

    pub fn on_dirty(&self, dirt: ComponentDirt) {
        if let Some(shape) = self.shape() {
            if Component::value_has_dirt(dirt, ComponentDirt::WORLD_TRANSFORM) {
                shape.as_ref().path_changed();
            }
        }
    }

    pub fn update(&self, value: ComponentDirt) {
        self.cast::<TransformComponent>().update(value);

        if Component::value_has_dirt(value, ComponentDirt::PATH) {
            self.command_path.set(Some(self.build_path()));
        }
    }

    fn build_path(&self) -> CommandPath {
        let mut builder = CommandPathBuilder::new();

        if self.vertices.len() < 2 {
            return builder.build();
        }

        enum Dir {
            In,
            Out,
        }

        impl ObjectRef<'_, PathVertex> {
            fn get_or_translation(&self, dir: Dir) -> math::Vec {
                self.try_cast::<CubicVertex>()
                    .map(|cubic| match dir {
                        Dir::In => cubic.render_in(),
                        Dir::Out => cubic.render_out(),
                    })
                    .unwrap_or_else(|| self.render_translation())
            }
        }

        let first_point = self.vertices.index(0);

        let mut out;
        let mut prev_is_cubic;

        let start;
        let start_in;
        let start_is_cubic;

        if let Some(cubic) = first_point.try_cast::<CubicVertex>() {
            let cubic = cubic.as_ref();
            prev_is_cubic = true;
            start_is_cubic = true;

            start_in = cubic.render_in();
            out = cubic.render_out();
            start = cubic.cast::<PathVertex>().render_translation();

            builder.move_to(start);
        } else {
            prev_is_cubic = false;
            start_is_cubic = false;

            let point = first_point.cast::<StraightVertex>();
            let point = point.as_ref();
            let radius = point.radius();

            if radius > 0.0 {
                let prev = self.vertices.index(self.vertices.len() - 1);

                let pos = point.cast::<PathVertex>().render_translation();
                let to_prev = prev.as_ref().get_or_translation(Dir::Out) - pos;
                let to_prev_length = to_prev.length();
                let to_prev = to_prev * to_prev_length.recip();

                let next = self.vertices.index(1);
                let to_next = next.as_ref().get_or_translation(Dir::In) - pos;
                let to_next_length = to_next.length();
                let to_next = to_next * to_next_length.recip();

                let render_radius = to_prev_length.min(to_next_length.min(radius));

                let translation = pos + to_prev * render_radius;

                start = translation;
                start_in = translation;

                builder.move_to(translation);

                let out_point = pos + to_prev * (1.0 - math::CIRCLE_CONSTANT) * render_radius;
                let in_point = pos + to_next * (1.0 - math::CIRCLE_CONSTANT) * render_radius;

                out = pos + to_next * render_radius;

                builder.cubic_to(out_point, in_point, out);
            } else {
                let translation = point.cast::<PathVertex>().render_translation();

                start = translation;
                start_in = translation;
                out = translation;

                builder.move_to(translation);
            }
        }

        for (i, vertex) in self.vertices.iter().enumerate().skip(1) {
            let vertex = vertex.as_ref();

            if let Some(cubic) = vertex.try_cast::<CubicVertex>() {
                let in_point = cubic.render_in();
                let translation = vertex.render_translation();

                builder.cubic_to(out, in_point, translation);

                prev_is_cubic = true;
                out = cubic.render_out();
            } else {
                let point = vertex.cast::<StraightVertex>();
                let pos = vertex.render_translation();
                let radius = point.radius();

                if radius > 0.0 {
                    let to_prev = out - pos;
                    let to_prev_length = to_prev.length();
                    let to_prev = to_prev * to_prev_length.recip();

                    let next = self.vertices.index((i + 1) % self.vertices.len());
                    let to_next = next.as_ref().get_or_translation(Dir::In) - pos;
                    let to_next_length = to_next.length();
                    let to_next = to_next * to_next_length.recip();

                    let render_radius = to_prev_length.min(to_next_length.min(radius));

                    let translation = pos + to_prev * render_radius;

                    if prev_is_cubic {
                        builder.cubic_to(out, translation, translation);
                    } else {
                        builder.line_to(translation);
                    }

                    let out_point = pos + to_prev * (1.0 - math::CIRCLE_CONSTANT) * render_radius;
                    let in_point = pos + to_next * (1.0 - math::CIRCLE_CONSTANT) * render_radius;

                    out = pos + to_next * render_radius;

                    builder.cubic_to(out_point, in_point, out);

                    prev_is_cubic = false;
                } else if prev_is_cubic {
                    builder.cubic_to(out, pos, pos);

                    prev_is_cubic = false;
                    out = pos;
                } else {
                    builder.line_to(pos);

                    out = pos;
                }
            }
        }

        if self.is_path_closed() {
            if prev_is_cubic || start_is_cubic {
                builder.cubic_to(out, start_in, start);
            }

            builder.close();
        }

        builder.build()
    }
}

impl Core for Path {
    parent_types![(node, Node)];

    properties![(128, path_flags, set_path_flags), node];
}

impl OnAdded for ObjectRef<'_, Path> {
    on_added!([on_added_dirty, import], Node);

    fn on_added_clean(&self, context: &dyn CoreContext) -> StatusCode {
        let code = self.cast::<Node>().on_added_clean(context);
        if code != StatusCode::Ok {
            return code;
        }

        for parent in self.cast::<Component>().parents() {
            if let Some(shape) = parent.try_cast::<Shape>() {
                self.shape.set(Some(shape.clone()));
                shape.as_ref().push_path(self.as_object());
                return StatusCode::Ok;
            }
        }

        StatusCode::MissingObject
    }
}
