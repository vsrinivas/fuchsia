// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    carnelian::{App, AppAssistantPtr, AppContext, AssistantCreatorFunc, LocalBoxFuture},
    fuchsia_syslog as syslog,
    fuchsia_trace_provider::trace_provider_create_with_fdio,
    std::env,
    terminal_lib::TerminalAssistant,
};

fn make_app_assistant_fut(
    app_context: &AppContext,
) -> LocalBoxFuture<'_, Result<AppAssistantPtr, Error>> {
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
    syslog::init().unwrap();
    trace_provider_create_with_fdio();
    env::set_var("RUST_BACKTRACE", "full");
    App::run(make_app_assistant())
}
