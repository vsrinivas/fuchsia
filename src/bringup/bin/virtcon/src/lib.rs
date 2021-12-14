// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "4096"]

mod app;
mod args;
mod colors;
mod log;
mod session_manager;
mod terminal;
mod text_grid;
mod view;

pub use app::VirtualConsoleAppAssistant;
pub use args::VirtualConsoleArgs;
