// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Carnelian
//!
//! Carnelian is a prototype framework for writing
//! [Fuchsia](https://fuchsia.googlesource.com/fuchsia/+/master/docs/the-book/README.md)
//! [modules](https://fuchsia.googlesource.com/fuchsia/+/master/docs/glossary.md#module) in
//! [Rust](https://www.rust-lang.org/).

#![deny(missing_docs)]

mod app;
mod canvas;
mod geometry;
mod label;
mod message;
mod scenic_utils;
mod view;

pub use crate::{
    app::{App, AppAssistant},
    canvas::{
        measure_text, Canvas, Color, FontDescription, FontFace, MappingPixelSink, Paint, PixelSink,
    },
    geometry::{Coord, IntCoord, IntPoint, IntRect, IntSize, Point, Rect, Size},
    label::Label,
    message::{make_message, Message},
    scenic_utils::set_node_color,
    view::{
        AnimationMode, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewController,
        ViewKey, ViewMessages,
    },
};
