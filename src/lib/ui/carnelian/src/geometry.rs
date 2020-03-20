// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Coordinate type
pub type Coord = f32;
/// A two-dimensional point
pub type Point = euclid::default::Point2D<Coord>;
/// A two-dimensional rectangle
pub type Rect = euclid::default::Rect<Coord>;
/// A type representing the extent of an element
/// in two-dimensionals.
pub type Size = euclid::default::Size2D<Coord>;

/// Integer cordinate type
pub type IntCoord = i32;
/// A two-dimensional integer point
pub type IntPoint = euclid::default::Point2D<IntCoord>;
/// A two-dimensional integer rectangle
pub type IntRect = euclid::default::Rect<IntCoord>;
/// A type representing the extent of an element
/// in two-dimensionals.
pub type IntSize = euclid::default::Size2D<IntCoord>;

/// A type representing the extent of an element
/// in two-dimensions.
pub type UintSize = euclid::default::Size2D<u32>;

/// Replacement for methods removed from Euclid
pub trait Corners {
    fn top_left(&self) -> Point;
    fn top_right(&self) -> Point;
    fn bottom_left(&self) -> Point;
    fn bottom_right(&self) -> Point;
}

impl Corners for Rect {
    fn top_left(&self) -> Point {
        Point::new(self.min_x(), self.min_y())
    }

    fn top_right(&self) -> Point {
        Point::new(self.max_x(), self.min_y())
    }

    fn bottom_left(&self) -> Point {
        Point::new(self.min_x(), self.max_y())
    }

    fn bottom_right(&self) -> Point {
        Point::new(self.max_x(), self.max_y())
    }
}
