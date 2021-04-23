// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        color::Color,
        render::{BlendMode, Context, Fill, FillRule, Path, PathBuilder, Raster, Style},
        Point,
    },
    euclid::{vec2, Transform2D},
    rive_rs::{
        self as rive,
        math::{self, Mat},
        shapes::{Command, CommandPath},
        PaintColor, RenderPaint,
    },
    std::{collections::HashMap, num::NonZeroU64},
};

#[derive(Clone, Debug)]
struct CachedRaster {
    raster: Raster,
    inverted_transform: Mat,
    was_used: bool,
}

pub struct RenderCache {
    cached_rasters: HashMap<NonZeroU64, CachedRaster>,
    tag: NonZeroU64,
    pub rasters: Vec<(Raster, Style)>,
}

#[derive(Debug)]
pub struct Renderer<'c> {
    context: &'c Context,
    cached_rasters: HashMap<NonZeroU64, CachedRaster>,
    tag: NonZeroU64,
    rasters: Vec<(Raster, Style)>,
}

impl<'c> Renderer<'c> {
    pub fn new(context: &'c Context) -> Self {
        Self {
            context,
            cached_rasters: HashMap::new(),
            tag: NonZeroU64::new(1).unwrap(),
            rasters: Vec::new(),
        }
    }

    pub fn from_cache(context: &'c Context, cache: RenderCache) -> Self {
        Self {
            context,
            cached_rasters: cache.cached_rasters,
            tag: cache.tag,
            rasters: cache.rasters,
        }
    }

    pub fn tag(&mut self) -> NonZeroU64 {
        let tag = self.tag;
        self.tag = NonZeroU64::new(tag.get().checked_add(1).unwrap_or(1)).unwrap();
        tag
    }

    pub fn reset(&mut self) {
        let cached_rasters = self
            .cached_rasters
            .drain()
            .filter(|(_, cached_raster)| !cached_raster.was_used)
            .collect();
        self.cached_rasters = cached_rasters;

        for cached_raster in &mut self.cached_rasters.values_mut() {
            cached_raster.was_used = false;
        }

        self.rasters.clear();
    }

    pub fn dismantle(self) -> RenderCache {
        RenderCache { cached_rasters: self.cached_rasters, tag: self.tag, rasters: self.rasters }
    }
}

fn to_point(p: math::Vec) -> Point {
    Point::new(p.x, p.y)
}

fn to_path(mut path_builder: PathBuilder, commands: &[Command]) -> Path {
    let mut start_point = None;
    let mut end_point = None;

    for command in commands {
        match *command {
            Command::MoveTo(p) => {
                if start_point != end_point {
                    path_builder.line_to(to_point(start_point.unwrap()));
                }
                path_builder.move_to(to_point(p));
                start_point = Some(p);
                end_point = Some(p);
            }
            Command::LineTo(p) => {
                path_builder.line_to(to_point(p));
                end_point = Some(p);
            }
            Command::CubicTo(c0, c1, p) => {
                path_builder.cubic_to(to_point(c0), to_point(c1), to_point(p));
                end_point = Some(p);
            }
            Command::Close => {
                if start_point != end_point {
                    path_builder.line_to(to_point(start_point.unwrap()));
                }
                end_point = start_point;
            }
        }
    }

    if start_point != end_point {
        path_builder.line_to(to_point(start_point.unwrap()));
    }

    path_builder.build()
}

impl rive::Renderer for Renderer<'_> {
    fn draw(&mut self, path: &CommandPath, transform: Mat, paint: &RenderPaint) {
        fn transform_translates_by_integers(transform: &Mat) -> bool {
            fn approx_eq(a: f32, b: f32) -> bool {
                (a - b).abs() < 0.001
            }

            approx_eq(transform.scale_x, 1.0)
                && approx_eq(transform.shear_x, 0.0)
                && approx_eq(transform.shear_y, 0.0)
                && approx_eq(transform.scale_y, 1.0)
                && transform.translate_x.fract().abs() < 0.001
                && transform.translate_y.fract().abs() < 0.001
        }

        let raster = match path
            .user_tag
            .get()
            .and_then(|tag| self.cached_rasters.get_mut(&tag))
            .and_then(|cached_raster| {
                if cached_raster.was_used {
                    return None;
                }

                cached_raster.was_used = true;
                Some((&mut cached_raster.raster, transform * cached_raster.inverted_transform))
            }) {
            Some((raster, transform)) if transform_translates_by_integers(&transform) => {
                *raster = raster.clone().translate(vec2(
                    transform.translate_x.round() as i32,
                    transform.translate_y.round() as i32,
                ));
                raster.clone()
            }
            _ => {
                let render_path = to_path(self.context.path_builder().unwrap(), &path.commands);

                let mut raster_builder = self.context.raster_builder().unwrap();

                raster_builder.add_with_transform(
                    &render_path,
                    &Transform2D::new(
                        transform.scale_x,
                        transform.shear_y,
                        transform.shear_x,
                        transform.scale_y,
                        transform.translate_x,
                        transform.translate_y,
                    ),
                );

                let raster = raster_builder.build();

                if let Some(inverted_transform) = transform.invert() {
                    let tag = self.tag();

                    path.user_tag.set(Some(tag));
                    self.cached_rasters.insert(
                        tag,
                        CachedRaster { raster: raster.clone(), inverted_transform, was_used: true },
                    );
                }

                raster
            }
        };

        let blend_mode = match paint.blend_mode {
            rive::shapes::paint::BlendMode::SrcOver => BlendMode::Over,
            // rive::shapes::paint::BlendMode::Screen => BlendMode::Screen,
            // rive::shapes::paint::BlendMode::Overlay => BlendMode::Overlay,
            // rive::shapes::paint::BlendMode::Darken => BlendMode::Darken,
            // rive::shapes::paint::BlendMode::Lighten => BlendMode::Lighten,
            // rive::shapes::paint::BlendMode::ColorDodge => BlendMode::ColorDodge,
            // rive::shapes::paint::BlendMode::ColorBurn => BlendMode::ColorBurn,
            // rive::shapes::paint::BlendMode::HardLight => BlendMode::HardLight,
            // rive::shapes::paint::BlendMode::SoftLight => BlendMode::SoftLight,
            // rive::shapes::paint::BlendMode::Difference => BlendMode::Difference,
            // rive::shapes::paint::BlendMode::Exclusion => BlendMode::Exclusion,
            // rive::shapes::paint::BlendMode::Multiply => BlendMode::Multiply,
            _ => {
                println!("unsupported blend_mode: {:?}", paint.blend_mode);
                BlendMode::Over
            }
        };

        let fill_rule = match paint.fill_rule {
            rive::shapes::FillRule::NonZero => FillRule::NonZero,
            rive::shapes::FillRule::EvenOdd => FillRule::EvenOdd,
        };

        let style = match &paint.color {
            PaintColor::Solid(color) => Style {
                fill_rule,
                fill: Fill::Solid(Color {
                    r: color.red(),
                    g: color.green(),
                    b: color.blue(),
                    a: color.alpha(),
                }),
                blend_mode,
            },
            PaintColor::Gradient(gradient) => {
                println!("unsupported paint color: {:?}", paint.color);
                let color_stop = gradient.stops.first().unwrap();
                Style {
                    fill_rule,
                    fill: Fill::Solid(Color {
                        r: color_stop.0.red(),
                        g: color_stop.0.green(),
                        b: color_stop.0.blue(),
                        a: color_stop.0.alpha(),
                    }),
                    blend_mode,
                }
            }
        };

        self.rasters.push((raster, style));
    }
}
