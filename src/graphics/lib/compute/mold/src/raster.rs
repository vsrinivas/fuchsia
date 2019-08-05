// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cell::{Ref, RefCell},
    cmp,
    rc::Rc,
};

use crate::{edge::Edge, path::Path, point::Point, GRID_LIMIT, PIXEL_SHIFT, PIXEL_WIDTH};

#[derive(Clone, Debug)]
pub(crate) struct BoundingBox {
    min: Point<i32>,
    max: Point<i32>,
}

impl BoundingBox {
    pub fn new() -> Self {
        Self { min: Point::new(GRID_LIMIT, GRID_LIMIT), max: Point::new(-GRID_LIMIT, -GRID_LIMIT) }
    }

    pub fn maxed() -> Self {
        Self { min: Point::new(-GRID_LIMIT, -GRID_LIMIT), max: Point::new(GRID_LIMIT, GRID_LIMIT) }
    }

    pub fn min_x(&self) -> i32 {
        self.min.x >> PIXEL_SHIFT
    }

    pub fn min_y(&self) -> i32 {
        self.min.y >> PIXEL_SHIFT
    }

    pub fn max_x(&self) -> i32 {
        (self.max.x + PIXEL_WIDTH - 1) >> PIXEL_SHIFT
    }

    pub fn max_y(&self) -> i32 {
        (self.max.y + PIXEL_WIDTH - 1) >> PIXEL_SHIFT
    }

    pub fn translate(&mut self, translation: Point<i32>) {
        self.min = self.min.translate(translation);
        self.max = self.max.translate(translation);
    }

    #[allow(clippy::float_cmp)]
    pub fn enclose(&mut self, edge: &Edge<i32>) {
        let edge_min = Point::new(cmp::min(edge.p0.x, edge.p1.x), cmp::min(edge.p0.y, edge.p1.y));
        let edge_max = Point::new(cmp::max(edge.p0.x, edge.p1.x), cmp::max(edge.p0.y, edge.p1.y));

        if edge_min.x < self.min.x {
            self.min.x = edge_min.x;
        }
        if edge_min.y < self.min.y {
            self.min.y = edge_min.y;
        }

        if edge_max.x > self.max.x {
            self.max.x = edge_max.x;
        }
        if edge_max.y > self.max.y {
            self.max.y = edge_max.y;
        }

        if self.min.x & (PIXEL_WIDTH - 1) == 0 && self.min.x == self.max.x {
            self.max.x += PIXEL_WIDTH;
        }
        if self.min.y & (PIXEL_WIDTH - 1) == 0 && self.min.y == self.max.y {
            self.max.y += PIXEL_WIDTH;
        }
    }

    //    pub fn union(&mut self, other: &Self) {
    //        self.enclose(&Edge::new(other.min, other.max));
    //    }
}

impl Default for BoundingBox {
    fn default() -> Self {
        Self::new()
    }
}

#[derive(Debug)]
struct RasterInner {
    edges: Vec<Edge<i32>>,
    new_edges: bool,
    bounding_box: BoundingBox,
}

#[derive(Clone, Debug)]
pub struct Raster {
    inner: Rc<RefCell<RasterInner>>,
}

impl Raster {
    pub fn new(path: &Path) -> Self {
        let mut bounding_box = BoundingBox::new();
        let edges = path.edges().iter().map(Edge::to_sp_edges).flatten().collect();

        for edge in &edges {
            bounding_box.enclose(edge);
        }

        Self { inner: Rc::new(RefCell::new(RasterInner { edges, new_edges: true, bounding_box })) }
    }

    pub fn empty() -> Self {
        let inner =
            RasterInner { edges: vec![], new_edges: true, bounding_box: BoundingBox::new() };

        Self { inner: Rc::new(RefCell::new(inner)) }
    }

    pub(crate) fn maxed() -> Self {
        let inner =
            RasterInner { edges: vec![], new_edges: true, bounding_box: BoundingBox::maxed() };

        Self { inner: Rc::new(RefCell::new(inner)) }
    }

    pub fn translate(&mut self, translation: Point<f32>) {
        let translation = Point::new(
            (translation.x * PIXEL_WIDTH as f32).round() as i32,
            (translation.y * PIXEL_WIDTH as f32).round() as i32,
        );

        for edge in &mut self.inner.borrow_mut().edges {
            *edge = edge.translate(translation);
        }

        self.inner.borrow_mut().bounding_box.translate(translation);
        self.inner.borrow_mut().new_edges = true;
    }

    pub(crate) fn new_edges(&self) -> bool {
        let inner = self.inner.borrow();
        inner.new_edges
    }

    pub(crate) fn edges(&self) -> Ref<[Edge<i32>]> {
        self.inner.borrow_mut().new_edges = false;
        Ref::map(self.inner.borrow(), |inner| &inner.edges[..])
    }

    pub(crate) fn bounding_box(&self) -> BoundingBox {
        self.inner.borrow().bounding_box.clone()
    }
}

impl Eq for Raster {}

impl PartialEq for Raster {
    fn eq(&self, other: &Self) -> bool {
        Rc::ptr_eq(&self.inner, &other.inner)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::point::Point;

    #[test]
    fn bounding_box_enclose() {
        let edge = Edge::new(
            Point::new(PIXEL_WIDTH * 3 / 2, PIXEL_WIDTH * 3 / 2),
            Point::new(PIXEL_WIDTH * 7 / 2, PIXEL_WIDTH * 7 / 2),
        );

        let mut bounding_box = BoundingBox::new();
        bounding_box.enclose(&edge);

        assert_eq!(bounding_box.min_x(), 1);
        assert_eq!(bounding_box.min_y(), 1);
        assert_eq!(bounding_box.max_x(), 4);
        assert_eq!(bounding_box.max_y(), 4);
    }

    #[test]
    fn bounding_box_translate() {
        let edge = Edge::new(
            Point::new(PIXEL_WIDTH * 3 / 2, PIXEL_WIDTH * 3 / 2),
            Point::new(PIXEL_WIDTH * 7 / 2, PIXEL_WIDTH * 7 / 2),
        );

        let mut bounding_box = BoundingBox::new();
        bounding_box.enclose(&edge);
        bounding_box.translate(Point::new(PIXEL_WIDTH / 2, PIXEL_WIDTH / 2));

        assert_eq!(bounding_box.min_x(), 2);
        assert_eq!(bounding_box.min_y(), 2);
        assert_eq!(bounding_box.max_x(), 4);
        assert_eq!(bounding_box.max_y(), 4);
    }

    #[test]
    fn bounding_box_horizontal() {
        let edge = Edge::new(
            Point::new(PIXEL_WIDTH * 3 / 2, PIXEL_WIDTH * 3 / 2),
            Point::new(PIXEL_WIDTH * 7 / 2, PIXEL_WIDTH * 3 / 2),
        );

        let mut bounding_box = BoundingBox::new();
        bounding_box.enclose(&edge);

        assert_eq!(bounding_box.min_x(), 1);
        assert_eq!(bounding_box.min_y(), 1);
        assert_eq!(bounding_box.max_x(), 4);
        assert_eq!(bounding_box.max_y(), 2);
    }

    #[test]
    fn bounding_box_vertical() {
        let edge = Edge::new(
            Point::new(PIXEL_WIDTH * 3 / 2, PIXEL_WIDTH * 3 / 2),
            Point::new(PIXEL_WIDTH * 3 / 2, PIXEL_WIDTH * 7 / 2),
        );

        let mut bounding_box = BoundingBox::new();
        bounding_box.enclose(&edge);

        assert_eq!(bounding_box.min_x(), 1);
        assert_eq!(bounding_box.min_y(), 1);
        assert_eq!(bounding_box.max_x(), 2);
        assert_eq!(bounding_box.max_y(), 4);
    }

    //    #[test]
    //    fn bounding_box_union() {
    //        let edge = Edge::new(
    //            Point::new(PIXEL_WIDTH * 3 / 2, PIXEL_WIDTH * 3 / 2),
    //            Point::new(PIXEL_WIDTH * 3 / 2, PIXEL_WIDTH * 7 / 2),
    //        );
    //
    //        let mut bounding_box = BoundingBox::new();
    //        bounding_box.enclose(&edge);
    //
    //        let edge = Edge::new(
    //            Point::new(PIXEL_WIDTH * 9 / 2, PIXEL_WIDTH * 9 / 2),
    //            Point::new(PIXEL_WIDTH * 11 / 2, PIXEL_WIDTH * 11 / 2),
    //        );
    //
    //        let mut other_bounding_box = BoundingBox::new();
    //        other_bounding_box.enclose(&edge);
    //
    //        bounding_box.union(&other_bounding_box);
    //
    //        assert_eq!(bounding_box.min_x(), 1);
    //        assert_eq!(bounding_box.min_y(), 1);
    //        assert_eq!(bounding_box.max_x(), 6);
    //        assert_eq!(bounding_box.max_y(), 6);
    //    }

    #[test]
    fn path_line_enclose() {
        let mut path = Path::new();

        path.line(Point::new(1.5, 1.5), Point::new(3.5, 3.5));

        let raster = Raster::new(&path);

        assert_eq!(raster.bounding_box().min_x(), 1);
        assert_eq!(raster.bounding_box().min_y(), 1);
        assert_eq!(raster.bounding_box().max_x(), 4);
        assert_eq!(raster.bounding_box().max_y(), 4);
    }
}
