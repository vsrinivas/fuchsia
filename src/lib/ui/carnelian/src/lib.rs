// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Carnelian
//!
//! Carnelian is a prototype framework for writing
//! [Fuchsia](https://fuchsia.dev/fuchsia-src/concepts)
//! [modules](https://fuchsia.dev/fuchsia-src/glossary#module) in
//! [Rust](https://www.rust-lang.org/).

mod app;
mod canvas;
mod geometry;
mod label;
mod message;
mod scenic_utils;
mod view;

pub use crate::{
    app::{
        make_app_assistant, App, AppAssistant, AppAssistantPtr, AppContext, AssistantCreator,
        AssistantCreatorFunc, FrameBufferPtr, LocalBoxFuture, ViewMode,
    },
    canvas::{
        measure_text, Canvas, Color, FontDescription, FontFace, MappingPixelSink, Paint, PixelSink,
    },
    geometry::{Coord, IntCoord, IntPoint, IntRect, IntSize, Point, Rect, Size},
    label::{make_font_description, Label},
    message::{make_message, Message},
    scenic_utils::set_node_color,
    view::{
        AnimationMode, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewController,
        ViewKey, ViewMessages,
    },
};
