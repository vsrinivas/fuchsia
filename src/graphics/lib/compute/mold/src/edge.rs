// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cmp::{self, Ordering};

use crate::{point::Point, GRID_LIMIT, PIXEL_WIDTH};

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Edge<T> {
    pub p0: Point<T>,
    pub p1: Point<T>,
}

impl<T> Edge<T> {
    pub fn new(p0: Point<T>, p1: Point<T>) -> Self {
        Self { p0, p1 }
    }
}

impl Edge<f32> {
    fn to_subpixel(&self) -> Edge<i32> {
        Edge { p0: self.p0.to_subpixel(), p1: self.p1.to_subpixel() }
    }

    pub fn to_sp_edges(&self) -> SubPixelEdges {
        let aligned = self.to_subpixel();

        let direction_x = match aligned.p0.x.cmp(&aligned.p1.x) {
            Ordering::Less => PIXEL_WIDTH,
            Ordering::Equal => 0,
            Ordering::Greater => -PIXEL_WIDTH,
        };
        let direction_y = match aligned.p0.y.cmp(&aligned.p1.y) {
            Ordering::Less => PIXEL_WIDTH,
            Ordering::Equal => 0,
            Ordering::Greater => -PIXEL_WIDTH,
        };

        let mut axes = aligned.p0.border();
        if aligned.p0.x > aligned.p1.x && axes.x as i32 != aligned.p0.x {
            axes.x += PIXEL_WIDTH;
        }
        if aligned.p0.y > aligned.p1.y && axes.y as i32 != aligned.p0.y {
            axes.y += PIXEL_WIDTH;
        }

        let slope = (aligned.p1.y - aligned.p0.y) as f32 / (aligned.p1.x - aligned.p0.x) as f32;

        SubPixelEdges {
            start: aligned.p0,
            start_f: Point::new(aligned.p0.x as f32, aligned.p0.y as f32),
            current: aligned.p0,
            end: aligned.p1,
            is_done: aligned.is_horizontal(), // Skip horizontal edges
            axes,
            direction: (direction_x, direction_y),
            cached_x: None,
            cached_y: None,
            slope,
            slope_recip: slope.recip(),
        }
    }
}

impl Edge<i32> {
    pub fn border(&self) -> Point<i32> {
        Point::new(cmp::min(self.p0.x, self.p1.x), cmp::min(self.p0.y, self.p1.y)).border()
    }

    pub fn double_signed_area(&self) -> i16 {
        let width = (self.p1.x - self.p0.x).abs();
        let height = self.p1.y - self.p0.y;
        let right = cmp::max(self.p0.x, self.p1.x);

        let border = self.border().x + PIXEL_WIDTH;

        let triangle = width * height;
        let rectangle = height * (border - right) * 2;

        (triangle + rectangle) as i16
    }

    pub fn cover(&self) -> i8 {
        (self.p1.y - self.p0.y) as i8
    }

    pub fn is_horizontal(&self) -> bool {
        self.p0.y == self.p1.y
    }

    pub fn translate(&self, translation: Point<i32>) -> Self {
        Self { p0: self.p0.translate(translation), p1: self.p1.translate(translation) }
    }
}

pub struct SubPixelEdges {
    start: Point<i32>,
    start_f: Point<f32>,
    current: Point<i32>,
    end: Point<i32>,
    is_done: bool,
    axes: Point<i32>,
    direction: (i32, i32),
    cached_x: Option<Point<i32>>,
    cached_y: Option<Point<i32>>,
    slope: f32,
    slope_recip: f32,
}

impl SubPixelEdges {
    fn next_x_intersection(&mut self) -> Option<Point<i32>> {
        if self.cached_x.is_some() {
            return self.cached_x;
        }

        self.axes.x += self.direction.0;
        let x0 = self.start_f.x;
        let y0 = self.start_f.y;
        let y = (self.slope * (self.axes.x as f32 - x0) + y0).round();

        if !y.is_finite() {
            return None;
        }

        let y = y as i32;

        if !(-GRID_LIMIT <= y && y <= GRID_LIMIT) {
            return None;
        }

        let point = Some(Point::new(self.axes.x, y));
        self.cached_x = point;
        point
    }

    fn next_y_intersection(&mut self) -> Option<Point<i32>> {
        if self.cached_y.is_some() {
            return self.cached_y;
        }

        self.axes.y += self.direction.1;
        let x0 = self.start_f.x;
        let y0 = self.start_f.y;
        let x = (self.slope_recip * (self.axes.y as f32 - y0) + x0).round();

        if !x.is_finite() {
            return None;
        }

        let x = x as i32;

        if !(-GRID_LIMIT <= x && x <= GRID_LIMIT) {
            return None;
        }

        let point = Some(Point::new(x, self.axes.y));
        self.cached_y = point;
        point
    }
}

impl Iterator for SubPixelEdges {
    type Item = Edge<i32>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.start == self.end {
            self.is_done = true;
        }

        if self.is_done {
            return None;
        }

        let x = self.next_x_intersection();
        let y = self.next_y_intersection();

        let next = match (x, y) {
            (Some(x), Some(y)) => {
                if x == y {
                    self.cached_x = None;
                    self.cached_y = None;
                    x
                } else if x.manhattan_distance(self.start) < y.manhattan_distance(self.start) {
                    self.cached_x = None;
                    x
                } else {
                    self.cached_y = None;
                    y
                }
            }
            (Some(x), None) => {
                self.cached_x = None;
                x
            }
            (None, Some(y)) => {
                self.cached_y = None;
                y
            }
            (None, None) => panic!("path exceeds grid limit (-32,768 to 32,768)"),
        };

        if self.current.manhattan_distance(self.end) < self.current.manhattan_distance(next) {
            self.is_done = true;
            return if self.current != self.end {
                Some(Edge::new(self.current, self.end))
            } else {
                None
            };
        }

        let edge = Some(Edge::new(self.current, next));
        self.current = next;
        edge
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_edge() {
        let p0 = Point::new(0.5, 0.5);
        let p1 = p0;

        assert_eq!(Edge::new(p0, p1).to_sp_edges().collect::<Vec<_>>(), vec![],);
    }

    #[test]
    fn skipped_intersections_x() {
        let p0 = Point::new(0.5, 0.5);
        let p1 = Point::new(3.5, 0.5);

        assert_eq!(Edge::new(p0, p1).to_sp_edges().collect::<Vec<_>>(), vec![],);
    }

    #[test]
    fn one_intersection_y() {
        let p0 = Point::new(0.5, 0.5);
        let p1 = Point::new(0.5, -0.5);

        assert_eq!(
            Edge::new(p0, p1).to_sp_edges().collect::<Vec<_>>(),
            vec![
                Edge::new(Point::new(8, 8), Point::new(8, 0),),
                Edge::new(Point::new(8, 0), Point::new(8, -8),),
            ],
        );
    }

    #[test]
    fn three_intersections_y() {
        let p0 = Point::new(0.5, 0.5);
        let p1 = Point::new(0.5, -2.5);

        assert_eq!(
            Edge::new(p0, p1).to_sp_edges().collect::<Vec<_>>(),
            vec![
                Edge::new(Point::new(8, 8), Point::new(8, 0),),
                Edge::new(Point::new(8, 0), Point::new(8, -16),),
                Edge::new(Point::new(8, -16), Point::new(8, -32),),
                Edge::new(Point::new(8, -32), Point::new(8, -40),),
            ],
        );
    }

    #[test]
    fn intersections_in_corners() {
        let p0 = Point::new(0.5, 0.5);
        let p1 = Point::new(-2.5, -2.5);

        assert_eq!(
            Edge::new(p0, p1).to_sp_edges().collect::<Vec<_>>(),
            vec![
                Edge::new(Point::new(8, 8), Point::new(0, 0),),
                Edge::new(Point::new(0, 0), Point::new(-16, -16),),
                Edge::new(Point::new(-16, -16), Point::new(-32, -32),),
                Edge::new(Point::new(-32, -32), Point::new(-40, -40),),
            ],
        );
    }

    #[test]
    fn intersections_in_middles() {
        let p0 = Point::new(0.75, 0.25);
        let p1 = Point::new(-2.25, -2.75);

        assert_eq!(
            Edge::new(p0, p1).to_sp_edges().collect::<Vec<_>>(),
            vec![
                Edge::new(Point::new(12, 4), Point::new(8, 0),),
                Edge::new(Point::new(8, 0), Point::new(0, -8),),
                Edge::new(Point::new(0, -8), Point::new(-8, -16),),
                Edge::new(Point::new(-8, -16), Point::new(-16, -24),),
                Edge::new(Point::new(-16, -24), Point::new(-24, -32),),
                Edge::new(Point::new(-24, -32), Point::new(-32, -40),),
                Edge::new(Point::new(-32, -40), Point::new(-36, -44),),
            ],
        );
    }

    #[test]
    fn number_of_intersections() {
        fn assert_length(angle: f32, intersections: usize) {
            const RADIUS: f32 = 3.0;
            let p0 = Point::new(0.0, 0.0);
            let p1 = Point::new(angle.cos() * RADIUS, angle.sin() * RADIUS);

            assert_eq!(Edge::new(p0, p1).to_sp_edges().collect::<Vec<_>>().len(), intersections);
        }

        let increment = std::f32::consts::PI / 8.0;

        assert_length(increment * 0.0, 0);
        assert_length(increment * 1.0, 4);
        assert_length(increment * 2.0, 3);
        assert_length(increment * 3.0, 4);
        assert_length(increment * 4.0, 3);
        assert_length(increment * 5.0, 4);
        assert_length(increment * 6.0, 3);
        assert_length(increment * 7.0, 4);
        assert_length(increment * 8.0, 0);
        assert_length(increment * 9.0, 4);
        assert_length(increment * 10.0, 3);
        assert_length(increment * 11.0, 4);
        assert_length(increment * 12.0, 3);
        assert_length(increment * 13.0, 4);
        assert_length(increment * 14.0, 3);
        assert_length(increment * 15.0, 4);
    }

    #[test]
    fn horizontal_edge() {
        let edge = Edge::new(Point::new(1, 1), Point::new(3, 1));

        assert_eq!(edge.double_signed_area(), 0);
        assert_eq!(edge.cover(), 0);
    }

    #[test]
    fn vertical_edge() {
        let edge = Edge::new(Point::new(1, 1), Point::new(1, 3));

        assert_eq!(edge.double_signed_area(), 60);
        assert_eq!(edge.cover(), 2);
    }

    #[test]
    fn vertical_edge_negative() {
        let edge =
            Edge::new(Point::new(1 - 16 * 5, 1 - 16 * 5), Point::new(1 - 16 * 5, 3 - 16 * 5));

        assert_eq!(edge.double_signed_area(), 60);
        assert_eq!(edge.cover(), 2);
    }

    #[test]
    fn slanted_edge() {
        let edge = Edge::new(Point::new(1, 1), Point::new(3, 3));

        assert_eq!(edge.double_signed_area(), 56);
        assert_eq!(edge.cover(), 2);
    }

    #[test]
    fn slanted_edge_flipped() {
        let edge = Edge::new(Point::new(3, 3), Point::new(1, 1));

        assert_eq!(edge.double_signed_area(), -56);
        assert_eq!(edge.cover(), -2);
    }

    #[test]
    fn slanted_edge_negative() {
        let edge =
            Edge::new(Point::new(1 - 16 * 5, 1 - 16 * 5), Point::new(3 - 16 * 5, 3 - 16 * 5));

        assert_eq!(edge.double_signed_area(), 56);
        assert_eq!(edge.cover(), 2);
    }

    #[test]
    fn slanted_edge_flipped_negative() {
        let edge =
            Edge::new(Point::new(3 - 16 * 5, 3 - 16 * 5), Point::new(1 - 16 * 5, 1 - 16 * 5));

        assert_eq!(edge.double_signed_area(), -56);
        assert_eq!(edge.cover(), -2);
    }
}
