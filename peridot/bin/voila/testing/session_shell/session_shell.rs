// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use carnelian::{
    set_node_color, App, AppAssistant, Color, Label, Paint, ViewAssistant, ViewAssistantContext,
    ViewAssistantPtr, ViewKey,
};
use failure::Error;
use fidl::endpoints::{RequestStream, ServiceMarker};
use fidl_fuchsia_modular::{SessionShellMarker, SessionShellRequest, SessionShellRequestStream};
use fuchsia_async as fasync;
use fuchsia_scenic::{Rectangle, SessionPtr, ShapeNode};
use fuchsia_syslog::{self as fx_log, fx_log_err, fx_log_info, fx_log_warn};
use futures::{TryFutureExt, TryStreamExt};
use std::env;

const BACKGROUND_Z: f32 = 0.0;
const LABEL_Z: f32 = BACKGROUND_Z - 0.01;

struct SessionShellAppAssistant;

impl AppAssistant for SessionShellAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(
        &mut self,
        _: ViewKey,
        session: &SessionPtr,
    ) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(SessionShellViewAssistant::new(session)?))
    }

    fn outgoing_services_names(&self) -> Vec<&'static str> {
        [SessionShellMarker::NAME].to_vec()
    }

    fn handle_service_connection_request(
        &mut self,
        _service_name: &str,
        channel: fasync::Channel,
    ) -> Result<(), Error> {
        Self::spawn_session_shell_service(SessionShellRequestStream::from_channel(channel));
        Ok(())
    }
}

impl SessionShellAppAssistant {
    fn spawn_session_shell_service(stream: SessionShellRequestStream) {
        fx_log_info!("spawning a session shell implementation");
        fasync::spawn_local(
            stream
                .map_ok(move |req| match req {
                    SessionShellRequest::AttachView {
                        view_id: _, view_holder_token: _, ..
                    } => {
                        fx_log_info!("SessionShell::AttachView()");
                    }
                    SessionShellRequest::AttachView2 {
                        view_id: _, view_holder_token: _, ..
                    } => {
                        fx_log_info!("SessionShell::AttachView2()");
                    }
                    SessionShellRequest::DetachView { view_id: _, .. } => {
                        fx_log_info!("SessionShell::DetachView()");
                    }
                })
                .try_collect::<()>()
                .unwrap_or_else(|e| fx_log_err!("Session shell failed: {:?}", e)),
        )
    }
}

struct SessionShellViewAssistant {
    background_node: ShapeNode,
    label: Label,
    bg_color: Color,
    fg_color: Color,
}

impl SessionShellViewAssistant {
    fn new(session: &SessionPtr) -> Result<SessionShellViewAssistant, Error> {
        Ok(SessionShellViewAssistant {
            background_node: ShapeNode::new(session.clone()),
            label: Label::new(&session, "Hello, world!")?,
            fg_color: Color::from_hash_code("#00FF41")?,
            bg_color: Color::from_hash_code("#0D0208")?,
        })
    }
}

impl ViewAssistant for SessionShellViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        set_node_color(context.session, &self.background_node, &Color::from_hash_code("#0D0208")?);
        context.root_node.add_child(&self.background_node);
        context.root_node.add_child(self.label.node());
        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        if context.size.width == 0.0 && context.size.height == 0.0 {
            fx_log_warn!("skipping update – got drawing context of size 0x0");
            return Ok(());
        }

        // Position and size the background.
        let center_x = context.size.width * 0.5;
        let center_y = context.size.height * 0.5;
        self.background_node.set_shape(&Rectangle::new(
            context.session.clone(),
            context.size.width,
            context.size.height,
        ));
        self.background_node.set_translation(center_x, center_y, BACKGROUND_Z);

        // Update and position the label.
        let paint = Paint { fg: self.fg_color, bg: self.bg_color };
        let min_dimension = context.size.width.min(context.size.height);
        let font_size = (min_dimension / 5.0).ceil().min(64.0) as u32;
        self.label.update(font_size, &paint)?;
        self.label.node().set_translation(center_x, center_y, LABEL_Z);

        Ok(())
    }
}

fn main() -> Result<(), Error> {
    env::set_var("RUST_BACKTRACE", "full");

    fx_log::init_with_tags(&["voila_test_session_shell"])?;
    fx_log::set_severity(fx_log::levels::INFO);

    let assistant = SessionShellAppAssistant {};
    App::run(Box::new(assistant))
}
