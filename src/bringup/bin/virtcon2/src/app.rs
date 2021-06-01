// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::args::VirtualConsoleArgs,
    crate::session_manager::SessionManager,
    crate::view::VirtualConsoleViewAssistant,
    anyhow::Error,
    carnelian::{drawing::DisplayRotation, AppAssistant, ViewAssistantPtr, ViewKey},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_virtualconsole::SessionManagerMarker,
    fuchsia_async as fasync,
};

pub struct VirtualConsoleAppAssistant {
    args: VirtualConsoleArgs,
    session_manager: SessionManager,
}

impl VirtualConsoleAppAssistant {
    pub fn new(args: VirtualConsoleArgs) -> Result<VirtualConsoleAppAssistant, Error> {
        let session_manager = SessionManager::new();
        Ok(VirtualConsoleAppAssistant { args, session_manager })
    }
}

impl AppAssistant for VirtualConsoleAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        VirtualConsoleViewAssistant::new(self.args.color_scheme, self.args.rounded_corners)
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
        self.session_manager.bind(channel);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync};

    #[fasync::run_singlethreaded(test)]
    async fn can_create_app() -> Result<(), Error> {
        let _ = VirtualConsoleAppAssistant::new(VirtualConsoleArgs::default())?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_create_virtual_console_view() -> Result<(), Error> {
        let mut app = VirtualConsoleAppAssistant::new(VirtualConsoleArgs::default())?;
        app.create_view_assistant(1)?;
        Ok(())
    }
}
