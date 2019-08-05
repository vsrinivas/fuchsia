// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::f32;

use crate::{edge::Edge, point::Point};

const TOLERANCE: f32 = 3.0;
const PIXEL_ACCURACY: f32 = 0.25;

#[derive(Clone, Debug, Default)]
pub struct Path {
    edges: Vec<Edge<f32>>,
    closing_point: Option<Point<f32>>,
}

impl Path {
    pub fn new() -> Self {
        Self::default()
    }

    pub(crate) fn edges(&self) -> &[Edge<f32>] {
        &self.edges[..]
    }

    pub fn line(&mut self, p0: Point<f32>, p1: Point<f32>) {
        if self.closing_point.is_none() {
            self.closing_point = Some(p0);
        }

        let edge = Edge::new(p0, p1);
        self.edges.push(edge);
    }

    pub fn quad(&mut self, p0: Point<f32>, p1: Point<f32>, p2: Point<f32>) {
        let deviation_x = (p0.x + p2.x - 2.0 * p1.x).abs();
        let deviation_y = (p0.y + p2.y - 2.0 * p1.y).abs();
        let deviation_squared = deviation_x * deviation_x + deviation_y * deviation_y;

        if deviation_squared < PIXEL_ACCURACY {
            self.line(p0, p2);
            return;
        }

        let subdivisions = 1 + (TOLERANCE * deviation_squared).sqrt().sqrt().floor() as usize;
        let increment = (subdivisions as f32).recip();

        let mut p = p0;
        let mut t = 0.0;

        for _ in 0..subdivisions - 1 {
            t += increment;
            let p_next = Point::lerp(t, Point::lerp(t, p0, p1), Point::lerp(t, p1, p2));

            self.line(p, p_next);

            p = p_next;
        }

        self.line(p, p2);
    }

    pub fn cubic(&mut self, p0: Point<f32>, p1: Point<f32>, p2: Point<f32>, p3: Point<f32>) {
        let deviation_x = (p0.x + p2.x - 3.0 * (p1.x + p2.x)).abs();
        let deviation_y = (p0.y + p2.y - 3.0 * (p1.y + p2.y)).abs();
        let deviation_squared = deviation_x * deviation_x + deviation_y * deviation_y;

        if deviation_squared < PIXEL_ACCURACY {
            self.line(p0, p3);
            return;
        }

        let subdivisions = 1 + (TOLERANCE * deviation_squared).sqrt().sqrt().floor() as usize;
        let increment = (subdivisions as f32).recip();

        let mut p = p0;
        let mut t = 0.0;

        for _ in 0..subdivisions - 1 {
            t += increment;
            let p_next = Point::lerp(
                t,
                Point::lerp(t, Point::lerp(t, p0, p1), Point::lerp(t, p1, p2)),
                Point::lerp(t, Point::lerp(t, p1, p2), Point::lerp(t, p2, p3)),
            );

            self.line(p, p_next);

            p = p_next;
        }

        self.line(p, p3);
    }

    pub fn close(&mut self) -> bool {
        let last = self.edges.last().cloned();
        if let (Some(closing_point), Some(last)) = (self.closing_point, last) {
            if !closing_point.approx_eq(last.p1) {
                self.line(last.p1, closing_point);
            }
            self.closing_point = None;
            return true;
        }

        false
    }
}
