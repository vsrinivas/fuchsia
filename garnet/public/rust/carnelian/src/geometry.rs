// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Coordinate type
pub type Coord = f32;
/// A two-dimensional point
pub type Point = euclid::Point2D<Coord>;
/// A two-dimensional rectangle
pub type Rect = euclid::Rect<Coord>;
/// A type representing the extent of an element
/// in two-dimensionals.
pub type Size = euclid::Size2D<Coord>;

/// Integer cordinate type
pub type IntCoord = i32;
/// A two-dimensional integer point
pub type IntPoint = euclid::Point2D<IntCoord>;
/// A two-dimensional integer rectangle
pub type IntRect = euclid::Rect<IntCoord>;
/// A type representing the extent of an element
/// in two-dimensionals.
pub type IntSize = euclid::Size2D<IntCoord>;

/// A type representing the extent of an element
/// in two-dimensions.
pub type UintSize = euclid::Size2D<u32>;
