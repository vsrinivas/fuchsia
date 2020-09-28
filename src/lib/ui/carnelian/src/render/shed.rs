// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{fs::File, io::prelude::*, path};

use anyhow::{bail, ensure};
use euclid::default::Transform2D;

use crate::{
    color::Color,
    geometry::Point,
    render::{BlendMode, Context, Fill, FillRule, Layer, Path, PathBuilder, Raster, Style},
};

#[derive(Clone, Debug, PartialEq)]
enum PathCommand {
    MoveTo(Point),
    LineTo(Point),
    QuadTo(Point, Point),
    CubicTo(Point, Point, Point),
    RatQuadTo(Point, Point, f32),
    Close,
}

#[derive(Clone, Debug, PartialEq)]
struct SvgPath {
    pub commands: Vec<PathCommand>,
    pub fill_rule: FillRule,
    pub color: Color,
}

impl SvgPath {
    pub fn apply_commands(&self, path_builder: &mut PathBuilder) {
        let mut initial_point = None;

        for command in &self.commands {
            match *command {
                PathCommand::MoveTo(p) => {
                    initial_point = Some(p);
                    path_builder.move_to(p);
                }
                PathCommand::LineTo(p) => {
                    path_builder.line_to(p);
                }
                PathCommand::QuadTo(p1, p2) => {
                    path_builder.quad_to(p1, p2);
                }
                PathCommand::RatQuadTo(p1, p2, w) => {
                    path_builder.rat_quad_to(p1, p2, w);
                }
                PathCommand::CubicTo(p1, p2, p3) => {
                    path_builder.cubic_to(p1, p2, p3);
                }
                PathCommand::Close => {
                    if let Some(initial_point) = initial_point {
                        path_builder.line_to(initial_point);
                    }
                }
            }
        }
    }
}

#[derive(Debug)]
struct Parser {
    bytes: Vec<u8>,
    index: usize,
}

impl Parser {
    pub fn new(bytes: Vec<u8>) -> Self {
        Self { bytes, index: 4 }
    }

    fn parse_u8(&mut self) -> u8 {
        let value = self.bytes[self.index];
        self.index += 1;
        value
    }

    fn parse_i16(&mut self) -> i16 {
        let mut bytes = [0; 2];
        bytes.copy_from_slice(&self.bytes[self.index..self.index + 2]);
        self.index += 2;
        i16::from_le_bytes(bytes)
    }

    fn parse_f32(&mut self) -> f32 {
        let mut bytes = [0; 4];
        bytes.copy_from_slice(&self.bytes[self.index..self.index + 4]);
        self.index += 4;
        f32::from_le_bytes(bytes)
    }

    fn len(&self) -> usize {
        self.bytes.len() - self.index
    }

    fn parse_fill_rule(&mut self) -> anyhow::Result<FillRule> {
        ensure!(self.len() >= 1, "missing fill rule");

        match self.parse_u8() {
            0 => Ok(FillRule::NonZero),
            1 => Ok(FillRule::EvenOdd),
            _ => bail!("incorrect fill rule"),
        }
    }

    fn parse_color(&mut self) -> anyhow::Result<Color> {
        ensure!(self.len() >= 4, "missing color");

        Ok(Color { r: self.parse_u8(), g: self.parse_u8(), b: self.parse_u8(), a: self.parse_u8() })
    }

    fn parse_point(&mut self) -> Point {
        let x = self.parse_i16() as f32 / 16.0;
        let y = self.parse_i16() as f32 / 16.0;

        Point::new(x, y)
    }

    fn parse_commands(&mut self) -> anyhow::Result<Vec<PathCommand>> {
        let mut commands = Vec::new();

        loop {
            ensure!(self.len() >= 1, "missing commands");

            match self.parse_u8() {
                0 => {
                    ensure!(self.len() >= 4 * 1, "move to missing points");

                    commands.push(PathCommand::MoveTo(self.parse_point()));
                }
                1 => {
                    ensure!(self.len() >= 4 * 1, "line to missing points");

                    commands.push(PathCommand::LineTo(self.parse_point()));
                }
                2 => {
                    ensure!(self.len() >= 4 * 2, "quad to missing points");

                    commands.push(PathCommand::QuadTo(self.parse_point(), self.parse_point()));
                }
                3 => {
                    ensure!(self.len() >= 4 * 3, "cubic to missing points");

                    commands.push(PathCommand::CubicTo(
                        self.parse_point(),
                        self.parse_point(),
                        self.parse_point(),
                    ));
                }
                4 => {
                    ensure!(self.len() >= 4 * 3, "rational quad to missing points");

                    commands.push(PathCommand::RatQuadTo(
                        self.parse_point(),
                        self.parse_point(),
                        self.parse_f32(),
                    ));
                }
                5 => commands.push(PathCommand::Close),
                6 => break,
                _ => bail!("unrecognized command"),
            }
        }

        Ok(commands)
    }

    pub fn parse_svg_paths(&mut self) -> anyhow::Result<Vec<SvgPath>> {
        let mut paths = Vec::new();

        while self.len() > 0 {
            paths.push(SvgPath {
                fill_rule: self.parse_fill_rule()?,
                color: self.parse_color()?,
                commands: self.parse_commands()?,
            });
        }

        Ok(paths)
    }
}

#[derive(Debug)]
pub struct Shed {
    paths: Vec<SvgPath>,
}

impl Shed {
    pub fn open<P: AsRef<path::Path>>(path: P) -> anyhow::Result<Self> {
        let mut file = File::open(path)?;
        let mut bytes = Vec::new();

        file.read_to_end(&mut bytes)?;

        ensure!(&bytes[..4] == "shed".as_bytes(), ".shed file missing header");

        let mut parser = Parser::new(bytes);

        Ok(Shed { paths: parser.parse_svg_paths()? })
    }

    pub fn paths(&self, context: &mut Context) -> Vec<(Path, Style)> {
        self.paths
            .iter()
            .map(|path| {
                let mut path_builder = context.path_builder().expect("failed to get PathBuilder");

                path.apply_commands(&mut path_builder);

                let style = Style {
                    fill_rule: path.fill_rule,
                    fill: Fill::Solid(path.color),
                    blend_mode: BlendMode::Over,
                };

                (path_builder.build(), style)
            })
            .collect()
    }

    pub fn rasters(
        &self,
        context: &mut Context,
        transform: Option<&Transform2D<f32>>,
    ) -> Vec<(Raster, Style)> {
        self.paths
            .iter()
            .map(|path| {
                let mut path_builder = context.path_builder().expect("failed to get PathBuilder");

                path.apply_commands(&mut path_builder);

                let style = Style {
                    fill_rule: path.fill_rule,
                    fill: Fill::Solid(path.color),
                    blend_mode: BlendMode::Over,
                };

                let mut raster_builder =
                    context.raster_builder().expect("failed to get RasterBuilder");
                raster_builder.add(&path_builder.build(), transform);

                (raster_builder.build(), style)
            })
            .collect()
    }

    pub fn layers(
        &self,
        context: &mut Context,
        transform: Option<&Transform2D<f32>>,
    ) -> Vec<Layer> {
        self.paths
            .iter()
            .map(|path| {
                let mut path_builder = context.path_builder().expect("failed to get PathBuilder");

                path.apply_commands(&mut path_builder);

                let style = Style {
                    fill_rule: path.fill_rule,
                    fill: Fill::Solid(path.color),
                    blend_mode: BlendMode::Over,
                };

                let mut raster_builder =
                    context.raster_builder().expect("failed to get RasterBuilder");
                raster_builder.add(&path_builder.build(), transform);

                Layer { raster: raster_builder.build(), style }
            })
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn paths() {
        let bytes = vec![
            b's', b'h', b'e', b'd', 0, // NonZero,
            255, 0, 0, 255, // #ff0000
            0, 16, 0, 32, 0, // M 1, 2
            5, // Z
            6, // End
            1, // EvenOdd,
            0, 255, 0, 255, // #00ff00
            0, 48, 0, 64, 0, // M 3, 4
            1, 80, 0, 96, 0, // L 5, 6
            5, // Z
            6,
        ];

        let mut parser = Parser::new(bytes);

        assert_eq!(
            parser.parse_svg_paths().unwrap(),
            vec![
                SvgPath {
                    commands: vec![PathCommand::MoveTo(Point::new(1.0, 2.0)), PathCommand::Close,],
                    fill_rule: FillRule::NonZero,
                    color: Color { r: 255, g: 0, b: 0, a: 255 },
                },
                SvgPath {
                    commands: vec![
                        PathCommand::MoveTo(Point::new(3.0, 4.0)),
                        PathCommand::LineTo(Point::new(5.0, 6.0)),
                        PathCommand::Close,
                    ],
                    fill_rule: FillRule::EvenOdd,
                    color: Color { r: 0, g: 255, b: 0, a: 255 },
                },
            ]
        );
    }
}
