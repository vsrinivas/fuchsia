// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {carnelian::App, failure::Error, std::env, terminal_lib::TerminalAssistant};

fn main() -> Result<(), Error> {
    env::set_var("RUST_BACKTRACE", "full");
    App::run(Box::new(TerminalAssistant::new()))
}
