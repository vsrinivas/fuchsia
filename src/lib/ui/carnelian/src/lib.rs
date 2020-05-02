// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Carnelian
//!
//! Carnelian is a prototype framework for writing
//! [Fuchsia](https://fuchsia.dev/fuchsia-src/concepts)
//! applications in
//! [Rust](https://www.rust-lang.org/).
//!
//! Below is a tiny example of a Carnelian app.
//!
//! The [`ViewAssistant`] trait is a good place to start when learning
//! about Carnelian.
//!
//! ```no_run
//! use anyhow::Error;
//! use carnelian::{
//!     make_app_assistant,
//!     render::{self},
//!     App, AppAssistant, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey,
//! };
//! use fuchsia_zircon::Event;
//!
//! #[derive(Default)]
//! struct SampleAppAssistant;
//!
//! impl AppAssistant for SampleAppAssistant {
//!     fn setup(&mut self) -> Result<(), Error> {
//!         Ok(())
//!     }
//!
//!     fn create_view_assistant_render(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
//!         SampleViewAssistant::new()
//!     }
//! }
//!
//! struct SampleViewAssistant;
//!
//! impl SampleViewAssistant {
//!     fn new() -> Result<ViewAssistantPtr, Error> {
//!         Ok(Box::new(Self {}))
//!     }
//! }
//!
//! impl ViewAssistant for SampleViewAssistant {
//!     fn render(
//!         &mut self,
//!         _render_context: &mut render::Context,
//!         _buffer_ready_event: Event,
//!         _view_context: &ViewAssistantContext<'_>,
//!     ) -> Result<(), Error> {
//!         Ok(())
//!     }
//! }
//!
//! fn main() -> Result<(), Error> {
//!     App::run(make_app_assistant::<SampleAppAssistant>())
//! }
//! ```

mod app;
mod canvas;
pub mod color;
pub mod drawing;
pub mod geometry;
pub mod input;
pub mod input_ext;
mod label;
mod message;
pub mod render;
mod view;

pub use crate::{
    app::{
        make_app_assistant, App, AppAssistant, AppAssistantPtr, AppContext, AssistantCreator,
        AssistantCreatorFunc, FrameBufferPtr, LocalBoxFuture, RenderOptions, ViewMode,
    },
    canvas::{measure_text, Canvas, MappingPixelSink, PixelSink},
    geometry::{Coord, IntCoord, IntPoint, IntRect, IntSize, Point, Rect, Size},
    label::{make_font_description, Label},
    message::{make_message, Message},
    view::{
        AnimationMode, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewController,
        ViewKey, ViewMessages,
    },
};
