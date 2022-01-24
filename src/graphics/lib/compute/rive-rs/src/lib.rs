// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Direct float comparison happens in a few places in the rive-cpp code.
#![allow(clippy::float_cmp)]

#[macro_use]
mod core;

pub mod animation;
mod artboard;
mod backboard;
pub mod bones;
mod component;
mod component_dirt;
mod container_component;
mod dependency_sorter;
mod draw_rules;
mod draw_target;
mod drawable;
mod dyn_vec;
mod file;
mod importers;
pub mod layout;
pub mod math;
mod node;
mod option_cell;
mod renderer;
mod runtime_header;
pub mod shapes;
mod status_code;
mod transform_component;

pub use crate::core::{BinaryReader, Object};
pub use artboard::Artboard;
pub use backboard::Backboard;
pub use component::Component;
pub use container_component::ContainerComponent;
pub use draw_rules::DrawRules;
pub use draw_target::DrawTarget;
pub use drawable::Drawable;
pub use file::{File, ImportError};
pub use node::Node;
pub use renderer::{Gradient, GradientType, PaintColor, RenderPaint, Renderer, StrokeStyle, Style};
pub use status_code::StatusCode;
pub use transform_component::TransformComponent;
