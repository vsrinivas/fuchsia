// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    carnelian::{App, AppAssistantPtr, AppContext, AssistantCreatorFunc, LocalBoxFuture},
    failure::Error,
    std::env,
    terminal_lib::TerminalAssistant,
};

fn make_app_assistant_fut(
    app_context: &AppContext,
) -> LocalBoxFuture<Result<AppAssistantPtr, Error>> {
    let f = async move {
        let assistant = Box::new(TerminalAssistant::new(app_context));
        Ok::<AppAssistantPtr, Error>(assistant)
    };
    Box::pin(f)
}

pub fn make_app_assistant() -> AssistantCreatorFunc {
    Box::new(make_app_assistant_fut)
}

fn main() -> Result<(), Error> {
    env::set_var("RUST_BACKTRACE", "full");
    App::run(make_app_assistant())
}
