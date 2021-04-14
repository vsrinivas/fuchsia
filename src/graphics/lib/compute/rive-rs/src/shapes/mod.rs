// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod clipping_shape;
mod command_path;
mod cubic_asymmetric_vertex;
mod cubic_detached_vertex;
mod cubic_mirrored_vertex;
mod cubic_vertex;
mod ellipse;
mod fill_rule;
mod metrics_path;
pub mod paint;
mod parametric_path;
mod path;
mod path_composer;
mod path_space;
mod path_vertex;
mod points_path;
mod polygon;
mod rectangle;
mod shape;
mod shape_paint_container;
mod star;
mod straight_vertex;
mod triangle;

pub use clipping_shape::ClippingShape;
pub use command_path::{Command, CommandPath, CommandPathBuilder};
pub use cubic_asymmetric_vertex::CubicAsymmetricVertex;
pub use cubic_detached_vertex::CubicDetachedVertex;
pub use cubic_mirrored_vertex::CubicMirroredVertex;
pub use cubic_vertex::CubicVertex;
pub use ellipse::Ellipse;
pub use fill_rule::FillRule;
pub use metrics_path::MetricsPath;
pub use parametric_path::ParametricPath;
pub use path::Path;
pub use path_composer::PathComposer;
pub use path_space::PathSpace;
pub use path_vertex::PathVertex;
pub use points_path::PointsPath;
pub use polygon::Polygon;
pub use rectangle::Rectangle;
pub use shape::Shape;
pub use shape_paint_container::ShapePaintContainer;
pub use star::Star;
pub use straight_vertex::StraightVertex;
pub use triangle::Triangle;
