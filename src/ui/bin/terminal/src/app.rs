// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::terminal_view::TerminalViewAssistant,
    anyhow::Error,
    carnelian::{AppAssistant, AppSender, ViewAssistantPtr, ViewKey},
    std::ffi::CString,
};

const TERMINAL_ENVIRON: &[&str; 1] = &["TERM=xterm-256color"];

const TERMINAL_SCROLL_TO_BOTTOM_ON_INPUT: bool = true;

pub struct TerminalAssistant {
    app_sender: AppSender,
    cmd: Vec<CString>,
}

impl TerminalAssistant {
    pub fn new(app_sender: &AppSender, cmd: Vec<CString>) -> TerminalAssistant {
        TerminalAssistant { app_sender: app_sender.clone(), cmd }
    }

    #[cfg(test)]
    fn new_for_test() -> TerminalAssistant {
        let app_sender = AppSender::new_for_testing_purposes_only();
        Self::new(&app_sender, vec![])
    }
}

impl AppAssistant for TerminalAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, view_key: ViewKey) -> Result<ViewAssistantPtr, Error> {
        let environ =
            TERMINAL_ENVIRON.iter().map(|s| CString::new(*s).unwrap()).collect::<Vec<_>>();
        Ok(Box::new(TerminalViewAssistant::new(
            &self.app_sender,
            view_key,
            TERMINAL_SCROLL_TO_BOTTOM_ON_INPUT,
            self.cmd.clone(),
            environ,
        )))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;

    #[fasync::run_singlethreaded(test)]
    async fn creates_terminal_view() -> Result<(), Error> {
        let mut app = TerminalAssistant::new_for_test();
        app.create_view_assistant(1)?;
        Ok(())
    }
}
