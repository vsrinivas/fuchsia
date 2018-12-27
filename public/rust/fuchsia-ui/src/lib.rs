// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Fuchsia-ui Framework
//!
//! Fuchsia-ui is a prototype framework for writing
//! [Fuchsia](https://fuchsia.googlesource.com/docs/+/HEAD/the-book/README.md)
//! [modules](https://fuchsia.googlesource.com/docs/+/HEAD/glossary.md#module) in
//! [Rust](https://www.rust-lang.org/).

#![deny(missing_docs)]

mod app;
mod canvas;
mod view;

pub use crate::{
    app::{App, AppAssistant, AppPtr, APP},
    canvas::{
        Canvas, Color, FontDescription, FontFace, Paint, PixelSink, Point, Rect,
        SharedBufferPixelSink, Size,
    },
    view::{ViewAssistant, ViewAssistantPtr, ViewController, ViewKey, ViewMessages},
};
