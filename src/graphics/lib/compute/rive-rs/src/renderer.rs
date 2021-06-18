// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;

use crate::{
    math::{self, Mat},
    shapes::{
        paint::{BlendMode, Color32, StrokeCap, StrokeJoin},
        CommandPath, FillRule,
    },
};

#[derive(Clone, Debug, Default)]
pub struct RenderPaint {
    pub fill_rule: FillRule,
    pub is_clipped: bool,
    pub color: PaintColor,
    pub style: Style,
    pub blend_mode: BlendMode,
}

#[derive(Clone, Debug)]
pub enum PaintColor {
    Solid(Color32),
    Gradient(Gradient),
}

impl Default for PaintColor {
    fn default() -> Self {
        Self::Solid(Color32::default())
    }
}

#[derive(Clone, Copy, Debug)]
pub enum Style {
    Fill,
    Stroke(StrokeStyle),
}

#[derive(Clone, Copy, Debug)]
pub struct StrokeStyle {
    pub thickness: f32,
    pub cap: StrokeCap,
    pub join: StrokeJoin,
}

impl Default for Style {
    fn default() -> Self {
        Self::Fill
    }
}

#[derive(Clone, Debug)]
pub struct Gradient {
    pub r#type: GradientType,
    pub start: math::Vec,
    pub end: math::Vec,
    pub stops: Vec<(Color32, f32)>,
}

#[derive(Debug)]
pub struct GradientBuilder {
    gradient: Gradient,
}

impl GradientBuilder {
    pub fn new(r#type: GradientType) -> Self {
        Self {
            gradient: Gradient {
                r#type,
                start: math::Vec::default(),
                end: math::Vec::default(),
                stops: Vec::new(),
            },
        }
    }

    pub fn start(&mut self, start: math::Vec) -> &mut Self {
        self.gradient.start = start;
        self
    }

    pub fn end(&mut self, end: math::Vec) -> &mut Self {
        self.gradient.end = end;
        self
    }

    pub fn push_stop(&mut self, color: Color32, position: f32) -> &mut Self {
        self.gradient.stops.push((color, position));
        self
    }

    pub fn build(self) -> Gradient {
        self.gradient
    }
}

#[derive(Clone, Copy, Debug)]
pub enum GradientType {
    Linear,
    Radial,
}

pub trait Renderer: fmt::Debug {
    fn draw(&mut self, path: &CommandPath, transform: Mat, paint: &RenderPaint);
    fn clip(&mut self, path: &CommandPath, transform: Mat, layers: usize);
}
