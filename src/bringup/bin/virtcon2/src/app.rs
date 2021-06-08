// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::args::VirtualConsoleArgs,
    crate::session_manager::{SessionManager, SessionManagerClient},
    crate::terminal::Terminal,
    crate::view::{EventProxy, ViewMessages, VirtualConsoleViewAssistant},
    anyhow::Error,
    carnelian::{
        drawing::DisplayRotation, make_message, AppAssistant, AppContext, ViewAssistantPtr, ViewKey,
    },
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_virtualconsole::SessionManagerMarker,
    fuchsia_async as fasync,
    std::fs::File,
};

const FIRST_SESSION_ID: u32 = 0;

#[derive(Clone)]
struct VirtualConsoleSessionManagerClient {
    app_context: AppContext,
    view_key: ViewKey,
}

impl SessionManagerClient for VirtualConsoleSessionManagerClient {
    type Listener = EventProxy;

    fn create_terminal(
        &self,
        id: u32,
        pty_fd: File,
        title: String,
    ) -> Result<Terminal<Self::Listener>, Error> {
        let event_proxy = EventProxy::new(&self.app_context, self.view_key, id);
        let terminal = Terminal::new(event_proxy, pty_fd, title);
        let terminal_clone = terminal.try_clone()?;
        self.app_context.queue_message(
            self.view_key,
            make_message(ViewMessages::AddTerminalMessage(id, terminal_clone)),
        );
        Ok(terminal)
    }

    fn request_update(&self, id: u32) {
        self.app_context.queue_message(
            self.view_key,
            make_message(ViewMessages::RequestTerminalUpdateMessage(id)),
        );
    }
}

pub struct VirtualConsoleAppAssistant {
    app_context: AppContext,
    view_key: ViewKey,
    args: VirtualConsoleArgs,
    session_manager: SessionManager,
}

impl VirtualConsoleAppAssistant {
    pub fn new(
        app_context: &AppContext,
        args: VirtualConsoleArgs,
    ) -> Result<VirtualConsoleAppAssistant, Error> {
        let app_context = app_context.clone();
        let session_manager = SessionManager::new(FIRST_SESSION_ID);

        Ok(VirtualConsoleAppAssistant { app_context, view_key: 0, args, session_manager })
    }

    #[cfg(test)]
    fn new_for_test() -> Result<VirtualConsoleAppAssistant, Error> {
        let app_context = AppContext::new_for_testing_purposes_only();
        Self::new(&app_context, VirtualConsoleArgs::default())
    }
}

impl AppAssistant for VirtualConsoleAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, view_key: ViewKey) -> Result<ViewAssistantPtr, Error> {
        let view_assistant = VirtualConsoleViewAssistant::new(
            &self.app_context,
            view_key,
            self.args.color_scheme,
            self.args.rounded_corners,
            self.args.font_size,
            self.args.animation,
        )?;
        self.view_key = view_key;
        Ok(view_assistant)
    }

    fn get_display_rotation(&self) -> DisplayRotation {
        self.args.display_rotation
    }

    fn outgoing_services_names(&self) -> Vec<&'static str> {
        [SessionManagerMarker::NAME].to_vec()
    }

    fn handle_service_connection_request(
        &mut self,
        _service_name: &str,
        channel: fasync::Channel,
    ) -> Result<(), Error> {
        let app_context = self.app_context.clone();
        let view_key = self.view_key;
        if self.view_key == 0 {
            panic!("Trying to service session manager connection without a view.");
        }
        let client = VirtualConsoleSessionManagerClient { app_context, view_key };
        self.session_manager.bind(&client, channel);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync};

    #[fasync::run_singlethreaded(test)]
    async fn can_create_app() -> Result<(), Error> {
        let _ = VirtualConsoleAppAssistant::new_for_test()?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_create_virtual_console_view() -> Result<(), Error> {
        let mut app = VirtualConsoleAppAssistant::new_for_test()?;
        app.create_view_assistant(1)?;
        Ok(())
    }
}
