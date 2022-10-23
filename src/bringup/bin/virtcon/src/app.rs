// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::args::VirtualConsoleArgs,
    crate::colors::ColorScheme,
    crate::log::{Log, LogClient},
    crate::session_manager::{SessionManager, SessionManagerClient},
    crate::terminal::Terminal,
    crate::view::{ViewMessages, VirtualConsoleViewAssistant},
    anyhow::Error,
    carnelian::{
        app::Config, make_message, AppAssistant, AppSender, MessageTarget, ViewAssistantPtr,
        ViewKey,
    },
    fidl::prelude::*,
    fidl_fuchsia_hardware_display::VirtconMode,
    fidl_fuchsia_virtualconsole::SessionManagerMarker,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    pty::ServerPty,
    std::collections::BTreeMap,
    term_model::event::{Event, EventListener},
};

const DEBUGLOG_ID: u32 = 0;
const FIRST_SESSION_ID: u32 = 1;

pub struct EventProxy {
    app_sender: AppSender,
    id: u32,
}

impl EventProxy {
    pub fn new(app_sender: &AppSender, id: u32) -> Self {
        Self { app_sender: app_sender.clone(), id }
    }
}

impl EventListener for EventProxy {
    fn send_event(&self, event: Event) {
        match event {
            Event::MouseCursorDirty => {
                self.app_sender.queue_message(
                    MessageTarget::Application,
                    make_message(AppMessages::RequestTerminalUpdateMessage(self.id)),
                );
            }
            _ => (),
        }
    }
}

enum AppMessages {
    AddTerminalMessage(u32, Terminal<EventProxy>, bool),
    RequestTerminalUpdateMessage(u32),
}

#[derive(Clone)]
struct VirtualConsoleClient {
    app_sender: AppSender,
    color_scheme: ColorScheme,
    scrollback_rows: u32,
}

impl VirtualConsoleClient {
    fn create_terminal(
        &self,
        id: u32,
        title: String,
        make_active: bool,
        pty: Option<ServerPty>,
    ) -> Result<Terminal<EventProxy>, Error> {
        let event_proxy = EventProxy::new(&self.app_sender, id);
        let terminal =
            Terminal::new(event_proxy, title, self.color_scheme, self.scrollback_rows, pty);
        let terminal_clone = terminal.try_clone()?;
        self.app_sender.queue_message(
            MessageTarget::Application,
            make_message(AppMessages::AddTerminalMessage(id, terminal_clone, make_active)),
        );
        Ok(terminal)
    }

    fn request_update(&self, id: u32) {
        self.app_sender.queue_message(
            MessageTarget::Application,
            make_message(AppMessages::RequestTerminalUpdateMessage(id)),
        );
    }
}

impl LogClient for VirtualConsoleClient {
    type Listener = EventProxy;

    fn create_terminal(&self, id: u32, title: String) -> Result<Terminal<Self::Listener>, Error> {
        VirtualConsoleClient::create_terminal(self, id, title, false, None)
    }

    fn request_update(&self, id: u32) {
        VirtualConsoleClient::request_update(self, id)
    }
}

impl SessionManagerClient for VirtualConsoleClient {
    type Listener = EventProxy;

    fn create_terminal(
        &self,
        id: u32,
        title: String,
        make_active: bool,
        pty: ServerPty,
    ) -> Result<Terminal<Self::Listener>, Error> {
        VirtualConsoleClient::create_terminal(self, id, title, make_active, Some(pty))
    }

    fn request_update(&self, id: u32) {
        VirtualConsoleClient::request_update(self, id)
    }
}

pub struct VirtualConsoleAppAssistant {
    app_sender: AppSender,
    args: VirtualConsoleArgs,
    read_only_debuglog: Option<zx::DebugLog>,
    session_manager: SessionManager,
    terminals: BTreeMap<u32, (Terminal<EventProxy>, bool)>,
    first_view: Option<ViewKey>,
}

impl VirtualConsoleAppAssistant {
    pub fn new(
        app_sender: &AppSender,
        args: VirtualConsoleArgs,
        read_only_debuglog: Option<zx::DebugLog>,
    ) -> Result<VirtualConsoleAppAssistant, Error> {
        let app_sender = app_sender.clone();
        let session_manager = SessionManager::new(args.keep_log_visible, FIRST_SESSION_ID);
        let terminals = BTreeMap::new();

        Ok(VirtualConsoleAppAssistant {
            app_sender,
            args,
            read_only_debuglog,
            session_manager,
            terminals,
            first_view: None,
        })
    }

    fn start_log(&self, read_only_debuglog: zx::DebugLog) -> Result<(), Error> {
        let app_sender = self.app_sender.clone();
        let color_scheme = self.args.color_scheme;
        let scrollback_rows = self.args.scrollback_rows;
        let client = VirtualConsoleClient { app_sender, color_scheme, scrollback_rows };
        Log::start(read_only_debuglog, &client, DEBUGLOG_ID)
    }

    #[cfg(test)]
    fn new_for_test() -> Result<VirtualConsoleAppAssistant, Error> {
        let app_sender = AppSender::new_for_testing_purposes_only();
        Self::new(&app_sender, VirtualConsoleArgs::default(), None)
    }
}

impl AppAssistant for VirtualConsoleAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        if let Some(read_only_debuglog) = self.read_only_debuglog.take() {
            self.start_log(read_only_debuglog).expect("failed to start debuglog");
        }
        Ok(())
    }

    fn create_view_assistant(&mut self, view_key: ViewKey) -> Result<ViewAssistantPtr, Error> {
        // The first view created will take the role as primary output.
        let is_primary = if self.first_view.is_none() {
            // Terminal messages will be routed to this view from this point forward.
            self.first_view = Some(view_key);
            true
        } else {
            false
        };

        let view_assistant = VirtualConsoleViewAssistant::new(
            &self.app_sender,
            view_key,
            self.args.color_scheme,
            self.args.rounded_corners,
            self.args.font_size,
            self.args.dpi.iter().cloned().collect(),
            self.args.boot_animation,
            is_primary,
        )?;

        // Early out if terminals are already associated with a view.
        // TODO(reveman): Improve this when we have multi-display support.
        if !is_primary {
            return Ok(view_assistant);
        }

        // Primary display has connected when this is called.
        self.session_manager.set_has_primary_connected(true);

        // Add all terminals to this view.
        for (id, (terminal, make_active)) in &self.terminals {
            let terminal_clone = terminal.try_clone().expect("failed to clone terminal");
            self.app_sender.queue_message(
                MessageTarget::View(view_key),
                make_message(ViewMessages::AddTerminalMessage(*id, terminal_clone, *make_active)),
            );
        }

        Ok(view_assistant)
    }

    fn outgoing_services_names(&self) -> Vec<&'static str> {
        [SessionManagerMarker::PROTOCOL_NAME].to_vec()
    }

    fn handle_service_connection_request(
        &mut self,
        _service_name: &str,
        channel: fasync::Channel,
    ) -> Result<(), Error> {
        let app_sender = self.app_sender.clone();
        let color_scheme = self.args.color_scheme;
        let scrollback_rows = self.args.scrollback_rows;
        let client = VirtualConsoleClient { app_sender, color_scheme, scrollback_rows };
        self.session_manager.bind(&client, channel);
        Ok(())
    }

    fn filter_config(&mut self, config: &mut Config) {
        config.view_mode = carnelian::app::ViewMode::Direct;
        config.virtcon_mode = Some(VirtconMode::Forced);
        config.keyboard_autorepeat = self.args.keyrepeat;
        config.display_rotation = self.args.display_rotation;
        config.keymap_name = Some(self.args.keymap.clone());
        config.buffer_count = Some(self.args.buffer_count);
    }

    fn handle_message(&mut self, message: carnelian::Message) {
        if let Some(message) = message.downcast_ref::<AppMessages>() {
            match message {
                AppMessages::AddTerminalMessage(id, terminal, make_active) => {
                    let terminal_clone = terminal.try_clone().expect("failed to clone terminal");
                    self.terminals.insert(*id, (terminal_clone, *make_active));
                    if let Some(view_key) = self.first_view {
                        let terminal_clone =
                            terminal.try_clone().expect("failed to clone terminal");
                        self.app_sender.queue_message(
                            MessageTarget::View(view_key),
                            make_message(ViewMessages::AddTerminalMessage(
                                *id,
                                terminal_clone,
                                *make_active,
                            )),
                        );
                    }
                }
                AppMessages::RequestTerminalUpdateMessage(id) => {
                    if let Some(view_key) = self.first_view {
                        self.app_sender.queue_message(
                            MessageTarget::View(view_key),
                            make_message(ViewMessages::RequestTerminalUpdateMessage(*id)),
                        );
                    }
                }
            }
        }
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
        app.create_view_assistant(Default::default())?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_handle_service_connection_request_without_view() -> Result<(), Error> {
        let mut app = VirtualConsoleAppAssistant::new_for_test()?;
        let (_, server_end) = zx::Channel::create().unwrap();
        let channel = fasync::Channel::from_channel(server_end).unwrap();
        app.handle_service_connection_request(SessionManagerMarker::PROTOCOL_NAME, channel)?;
        Ok(())
    }
}
