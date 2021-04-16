// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::session_manager::SessionManager,
    crate::view::VirtualConsoleViewAssistant,
    anyhow::Error,
    carnelian::{AppAssistant, ViewAssistantPtr, ViewKey},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_virtualconsole::SessionManagerMarker,
    fuchsia_async as fasync,
};

pub struct VirtualConsoleAppAssistant {
    session_manager: SessionManager,
}

impl VirtualConsoleAppAssistant {
    pub fn new() -> VirtualConsoleAppAssistant {
        let session_manager = SessionManager::new();
        VirtualConsoleAppAssistant { session_manager }
    }
}

impl AppAssistant for VirtualConsoleAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        fasync::Task::local(async move {
            stdout_to_debuglog::init().await.expect("Failed to redirect stdout to debuglog");
            println!("vc: started");
        })
        .detach();
        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        VirtualConsoleViewAssistant::new()
    }

    /// Return the list of names of services this app wants to provide.
    fn outgoing_services_names(&self) -> Vec<&'static str> {
        [SessionManagerMarker::NAME].to_vec()
    }

    /// Handle a request to connect to a service provided by this app.
    fn handle_service_connection_request(
        &mut self,
        _service_name: &str,
        channel: fasync::Channel,
    ) -> Result<(), Error> {
        self.session_manager.bind(channel);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync};

    #[fasync::run_singlethreaded(test)]
    async fn can_create_virtual_console_view() -> Result<(), Error> {
        let mut app = VirtualConsoleAppAssistant::new();
        app.create_view_assistant(1)?;
        Ok(())
    }
}
