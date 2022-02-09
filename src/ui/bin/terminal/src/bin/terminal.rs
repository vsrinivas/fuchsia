// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    carnelian::{App, AppAssistantPtr, AppSender, AssistantCreator, AssistantCreatorFunc},
    fuchsia_syslog as syslog,
    fuchsia_trace_provider::trace_provider_create_with_fdio,
    std::env,
    std::ffi::CString,
    terminal_lib::TerminalAssistant,
};

fn make_app_assistant_fut(cmd: Vec<CString>) -> impl FnOnce(&AppSender) -> AssistantCreator<'_> {
    move |app_sender: &AppSender| {
        let f = async move {
            let assistant = Box::new(TerminalAssistant::new(app_sender, cmd));
            Ok::<AppAssistantPtr, Error>(assistant)
        };
        Box::pin(f)
    }
}

pub fn make_app_assistant(cmd: Vec<CString>) -> AssistantCreatorFunc {
    Box::new(make_app_assistant_fut(cmd))
}

fn main() -> Result<(), Error> {
    syslog::init().unwrap();
    trace_provider_create_with_fdio();
    env::set_var("RUST_BACKTRACE", "full");

    // If there are any arguments (besides the first which is the executable path) then they
    // represent the argv of the spawn command.
    let cmd = env::args().skip(1).map(|s| CString::new(s)).collect::<Result<Vec<_>, _>>()?;

    App::run(make_app_assistant(cmd))
}
