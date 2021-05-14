// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::session_manager::SessionManager,
    crate::view::VirtualConsoleViewAssistant,
    anyhow::{anyhow, Error},
    carnelian::{drawing::DisplayRotation, AppAssistant, ViewAssistantPtr, ViewKey},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_virtualconsole::SessionManagerMarker,
    fuchsia_async as fasync,
};

// TODO(reveman): Read from boot arguments.
const DISPLAY_ROTATION: &'static str = "0";

fn display_rotation_from_str(s: &str) -> Result<DisplayRotation, Error> {
    match s {
        "0" => Ok(DisplayRotation::Deg0),
        "90" => Ok(DisplayRotation::Deg90),
        "180" => Ok(DisplayRotation::Deg180),
        "270" => Ok(DisplayRotation::Deg270),
        _ => Err(anyhow!("Invalid DisplayRotation {}", s)),
    }
}

pub struct VirtualConsoleAppAssistant {
    session_manager: SessionManager,
    display_rotation: DisplayRotation,
}

impl VirtualConsoleAppAssistant {
    pub fn new() -> Result<VirtualConsoleAppAssistant, Error> {
        let session_manager = SessionManager::new();
        let display_rotation = display_rotation_from_str(DISPLAY_ROTATION)?;
        Ok(VirtualConsoleAppAssistant { session_manager, display_rotation })
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

    fn get_display_rotation(&self) -> DisplayRotation {
        self.display_rotation
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
    async fn can_create_app() -> Result<(), Error> {
        let _ = VirtualConsoleAppAssistant::new()?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_create_virtual_console_view() -> Result<(), Error> {
        let mut app = VirtualConsoleAppAssistant::new()?;
        app.create_view_assistant(1)?;
        Ok(())
    }
}
