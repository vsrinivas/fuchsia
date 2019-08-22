// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]
#![recursion_limit = "4096"]

mod app;
mod pty;
mod terminal_view;

use {app::TerminalAssistant, carnelian::App, failure::Error, std::env};

fn main() -> Result<(), Error> {
    env::set_var("RUST_BACKTRACE", "full");
    App::run(Box::new(TerminalAssistant::new()))
}
