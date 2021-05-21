// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::RefCell, rc::Rc};

use crate::{
    core::{Core, Object, ObjectRef, OnAdded, Property, TryFromU64},
    math::Mat,
    option_cell::OptionCell,
    renderer::{StrokeStyle, Style},
    shapes::{
        paint::{
            shape_paint_mutator::ShapePaintMutator,
            stroke_effect::{self, StrokeEffect},
            ShapePaint,
        },
        path_space::PathSpace,
        CommandPath, MetricsPath,
    },
    RenderPaint, Renderer,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum StrokeCap {
    Butt,
    Round,
    Square,
}

impl Default for StrokeCap {
    fn default() -> Self {
        Self::Butt
    }
}

impl TryFromU64 for StrokeCap {
    fn try_from(val: u64) -> Option<Self> {
        match val {
            0 => Some(Self::Butt),
            1 => Some(Self::Round),
            2 => Some(Self::Square),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum StrokeJoin {
    Miter,
    Round,
    Bevel,
}

impl Default for StrokeJoin {
    fn default() -> Self {
        Self::Miter
    }
}

impl TryFromU64 for StrokeJoin {
    fn try_from(val: u64) -> Option<Self> {
        match val {
            0 => Some(Self::Miter),
            1 => Some(Self::Round),
            2 => Some(Self::Bevel),
            _ => None,
        }
    }
}

#[derive(Debug)]
pub struct Stroke {
    shape_paint: ShapePaint,
    thickness: Property<f32>,
    cap: Property<StrokeCap>,
    join: Property<StrokeJoin>,
    transform_affects_stroke: Property<bool>,
    metrics_path: OptionCell<MetricsPath>,
    effect: OptionCell<Object>,
    pub(crate) outlined_stroke: OptionCell<CommandPath>,
}

impl ObjectRef<'_, Stroke> {
    pub fn thickness(&self) -> f32 {
        self.thickness.get()
    }

    pub fn set_thickness(&self, thickness: f32) {
        if self.thickness() == thickness {
            return;
        }

        self.thickness.set(thickness);
        self.cast::<ShapePaint>().render_paint().borrow_mut().style = self.style();
        self.outlined_stroke.set(None);
    }

    pub fn cap(&self) -> StrokeCap {
        self.cap.get()
    }

    pub fn set_cap(&self, cap: StrokeCap) {
        if self.cap() == cap {
            return;
        }

        self.cap.set(cap);
        self.cast::<ShapePaint>().render_paint().borrow_mut().style = self.style();
        self.outlined_stroke.set(None);
    }

    pub fn join(&self) -> StrokeJoin {
        self.join.get()
    }

    pub fn set_join(&self, join: StrokeJoin) {
        if self.join() == join {
            return;
        }

        self.join.set(join);
        self.cast::<ShapePaint>().render_paint().borrow_mut().style = self.style();
        self.outlined_stroke.set(None);
    }

    pub fn transform_affects_stroke(&self) -> bool {
        self.transform_affects_stroke.get()
    }

    pub fn set_transform_affects_stroke(&self, transform_affects_stroke: bool) {
        self.transform_affects_stroke.set(transform_affects_stroke);
    }
}

impl ObjectRef<'_, Stroke> {
    pub fn init_render_paint(
        &self,
        mutator: Object<ShapePaintMutator>,
    ) -> Option<Rc<RefCell<RenderPaint>>> {
        let render_paint = self.cast::<ShapePaint>().init_render_paint(mutator).unwrap();
        render_paint.borrow_mut().style = self.style();

        Some(render_paint)
    }

    pub(crate) fn style(&self) -> Style {
        Style::Stroke(StrokeStyle {
            thickness: self.thickness(),
            cap: self.cap(),
            join: self.join(),
        })
    }

    pub fn path_space(&self) -> PathSpace {
        if self.transform_affects_stroke() {
            PathSpace::LOCAL
        } else {
            PathSpace::WORLD
        }
    }

    pub fn has_stroke_effect(&self) -> bool {
        self.effect.get().is_some()
    }

    pub(crate) fn set_stroke_effect(&self, effect: Option<Object>) {
        self.effect.set(effect);
    }

    pub fn invalidate_effects(&self) {
        if let Some(effect) = self.effect.get() {
            stroke_effect::as_ref(effect.as_ref()).invalidate_effect();
        }
        self.outlined_stroke.set(None);
    }

    pub fn draw(&self, renderer: &mut impl Renderer, path: &CommandPath, transform: Mat) {
        if !self.cast::<ShapePaint>().is_visible() {
            return;
        }

        let mut new_outline = None;
        self.outlined_stroke.with(|outline| {
            if let Some(outline) = outline {
                renderer.draw(
                    outline,
                    transform,
                    &*self.cast::<ShapePaint>().render_paint().borrow(),
                );
            } else {
                let new_path;
                let path = if let Some(effect) = self.effect.get() {
                    let effect = stroke_effect::as_ref(effect.as_ref());

                    self.metrics_path.maybe_init(|| MetricsPath::new(path));
                    new_path = self
                        .metrics_path
                        .with_mut(|metrics_path| effect.effect_path(metrics_path.unwrap()));

                    &new_path
                } else {
                    path
                };

                let outline = path.outline_strokes(&StrokeStyle {
                    thickness: self.thickness(),
                    cap: self.cap(),
                    join: self.join(),
                });

                renderer.draw(
                    &outline,
                    transform,
                    &*self.cast::<ShapePaint>().render_paint().borrow(),
                );

                new_outline = Some(outline);
            }
        });

        if new_outline.is_some() {
            self.outlined_stroke.set(new_outline);
        }
    }
}

impl Core for Stroke {
    parent_types![(shape_paint, ShapePaint)];

    properties![
        (47, thickness, set_thickness),
        (48, cap, set_cap),
        (49, join, set_join),
        (50, transform_affects_stroke, set_transform_affects_stroke),
        shape_paint,
    ];
}

impl OnAdded for ObjectRef<'_, Stroke> {
    on_added!(ShapePaint);
}

impl Default for Stroke {
    fn default() -> Self {
        Self {
            shape_paint: ShapePaint::default(),
            thickness: Property::new(1.0),
            cap: Property::new(StrokeCap::Butt),
            join: Property::new(StrokeJoin::Miter),
            transform_affects_stroke: Property::new(true),
            metrics_path: OptionCell::new(),
            effect: OptionCell::new(),
            outlined_stroke: OptionCell::new(),
        }
    }
}
