// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        color::Color,
        render::{
            BlendMode, Context as RenderContext, Fill, FillRule, Gradient, GradientType, Layer,
            Path, PathBuilder, Raster, Style,
        },
        Point,
    },
    anyhow::{Context, Error},
    euclid::{vec2, Transform2D},
    fuchsia_trace::duration,
    rive_rs::{
        self as rive,
        math::{self, Mat},
        shapes::{Command, CommandPath},
        ImportError, PaintColor, RenderPaint,
    },
    std::{collections::HashMap, fs, num::NonZeroU64, path::PathBuf},
};

pub fn load_rive(path: PathBuf) -> Result<rive::File, Error> {
    let buffer = fs::read(path.clone())
        .with_context(|| format!("Failed to read rive from {}", path.display()))?;
    let mut reader = rive::BinaryReader::new(&buffer);
    let file = rive::File::import(&mut reader).map_err(|error| {
        let context = match error {
            ImportError::UnsupportedVersion => format!("Unsupported version: {}", path.display()),
            ImportError::Malformed => format!("Malformed: {}", path.display()),
        };
        anyhow::anyhow!(context)
    })?;

    Ok(file)
}

#[derive(Clone, Debug)]
struct CachedRaster {
    raster: Raster,
    inverted_transform: Mat,
    was_used: bool,
}

#[derive(Debug)]
pub struct RenderCache {
    cached_rasters: HashMap<NonZeroU64, CachedRaster>,
    tag: NonZeroU64,
    pub layers: Vec<Layer>,
}

impl RenderCache {
    pub fn new() -> Self {
        Self {
            cached_rasters: HashMap::new(),
            tag: NonZeroU64::new(1).unwrap(),
            layers: Vec::new(),
        }
    }

    fn reset(&mut self) {
        duration!("gfx", "render::Rive::RenderCache::reset");

        // Retain rasters used last frame and reset `was_used` field.
        self.cached_rasters
            .retain(|_, cached_raster| std::mem::replace(&mut cached_raster.was_used, false));
    }

    pub fn with_renderer(&mut self, context: &RenderContext, f: impl FnOnce(&mut Renderer<'_>)) {
        duration!("gfx", "render::Rive::RenderCache::with_renderer");

        self.reset();
        f(&mut Renderer { context, cache: self, clip: None });
    }
}

#[derive(Debug)]
pub struct Renderer<'c> {
    context: &'c RenderContext,
    cache: &'c mut RenderCache,
    clip: Option<Raster>,
}

impl<'c> Renderer<'c> {
    pub fn tag(&mut self) -> NonZeroU64 {
        let tag = self.cache.tag;
        self.cache.tag = NonZeroU64::new(tag.get().checked_add(1).unwrap_or(1)).unwrap();
        tag
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
            .and_then(|tag| self.cache.cached_rasters.get_mut(&tag))
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
                    self.cache.cached_rasters.insert(
                        tag,
                        CachedRaster { raster: raster.clone(), inverted_transform, was_used: true },
                    );
                }

                raster
            }
        };

        let blend_mode = match paint.blend_mode {
            rive::shapes::paint::BlendMode::SrcOver => BlendMode::Over,
            rive::shapes::paint::BlendMode::Screen => BlendMode::Screen,
            rive::shapes::paint::BlendMode::Overlay => BlendMode::Overlay,
            rive::shapes::paint::BlendMode::Darken => BlendMode::Darken,
            rive::shapes::paint::BlendMode::Lighten => BlendMode::Lighten,
            rive::shapes::paint::BlendMode::ColorDodge => BlendMode::ColorDodge,
            rive::shapes::paint::BlendMode::ColorBurn => BlendMode::ColorBurn,
            rive::shapes::paint::BlendMode::HardLight => BlendMode::HardLight,
            rive::shapes::paint::BlendMode::SoftLight => BlendMode::SoftLight,
            rive::shapes::paint::BlendMode::Difference => BlendMode::Difference,
            rive::shapes::paint::BlendMode::Exclusion => BlendMode::Exclusion,
            rive::shapes::paint::BlendMode::Multiply => BlendMode::Multiply,
            _ => {
                println!("unsupported blend_mode: {:?}", paint.blend_mode);
                BlendMode::Over
            }
        };

        let fill_rule = match paint.fill_rule {
            rive::shapes::FillRule::NonZero => FillRule::NonZero,
            rive::shapes::FillRule::EvenOdd => FillRule::EvenOdd,
        };

        fn to_color(color: &rive::shapes::paint::Color32) -> Color {
            Color { r: color.red(), g: color.green(), b: color.blue(), a: color.alpha() }
        }

        let style = match &paint.color {
            PaintColor::Solid(color) => {
                Style { fill_rule, fill: Fill::Solid(to_color(color)), blend_mode }
            }
            PaintColor::Gradient(gradient) => {
                let start = transform * gradient.start;
                let end = transform * gradient.end;

                Style {
                    fill_rule,
                    fill: Fill::Gradient(Gradient {
                        r#type: match gradient.r#type {
                            rive::GradientType::Linear => GradientType::Linear,
                            rive::GradientType::Radial => GradientType::Radial,
                        },
                        start: Point::new(start.x, start.y),
                        end: Point::new(end.x, end.y),
                        stops: gradient
                            .stops
                            .iter()
                            .map(|(color, stop)| (to_color(color), *stop))
                            .collect::<Vec<_>>(),
                    }),
                    blend_mode,
                }
            }
        };

        self.cache.layers.push(Layer {
            raster,
            clip: paint.is_clipped.then(|| self.clip.clone()).flatten(),
            style,
        });
    }

    fn clip(&mut self, path: &CommandPath, transform: Mat, _: usize) {
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
            .and_then(|tag| self.cache.cached_rasters.get_mut(&tag))
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
                    self.cache.cached_rasters.insert(
                        tag,
                        CachedRaster { raster: raster.clone(), inverted_transform, was_used: true },
                    );
                }

                raster
            }
        };

        self.clip = Some(raster);
    }
}
