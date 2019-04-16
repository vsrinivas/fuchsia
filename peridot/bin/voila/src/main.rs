// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use carnelian::{
    set_node_color, App, AppAssistant, Color, Message, Point, ViewAssistant, ViewAssistantContext,
    ViewAssistantPtr, ViewKey,
};
use failure::{Error, ResultExt};
use fidl::encoding::OutOfLine;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_modular::AppConfig;
use fidl_fuchsia_modular_auth::{Account, IdentityProvider};
use fidl_fuchsia_modular_internal::{SessionContextMarker, SessionmgrMarker};
use fidl_fuchsia_sys::EnvironmentControllerProxy;
use fidl_fuchsia_ui_input::PointerEvent;
use fuchsia_app::{
    client::{App as LaunchedApp, Launcher},
    fuchsia_single_component_package_url,
};
use fuchsia_async as fasync;
use fuchsia_scenic::{
    new_view_token_pair, Circle, EntityNode, Rectangle, SessionPtr, ShapeNode, ViewHolder,
};
use fuchsia_syslog::{self as fx_log, fx_log_info, fx_log_warn};
use futures::prelude::*;
use rand::Rng;
use std::collections::BTreeMap;
use std::sync::Arc;

mod layout;
mod message;
mod replica;
mod session_context;
mod toggle;

use crate::layout::{layout, ChildViewData};
use crate::message::VoilaMessage;
use crate::replica::make_replica_env;
use crate::session_context::SessionContext;
use crate::toggle::Toggle;

const CLOUD_PROVIDER_URI: &str = fuchsia_single_component_package_url!("cloud_provider_in_memory");
const ERMINE_URI: &str = fuchsia_single_component_package_url!("ermine");
const MONDRIAN_URI: &str = fuchsia_single_component_package_url!("mondrian");

struct VoilaAppAssistant {}

impl AppAssistant for VoilaAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(
        &mut self,
        _key: ViewKey,
        session: &SessionPtr,
    ) -> Result<ViewAssistantPtr, Error> {
        let cloud_provider = Launcher::new()?.launch(CLOUD_PROVIDER_URI.to_string(), None)?;
        Ok(Box::new(VoilaViewAssistant {
            background_node: ShapeNode::new(session.clone()),
            circle_node: ShapeNode::new(session.clone()),
            cloud_provider_app: Arc::new(cloud_provider),
            replicas: BTreeMap::new(),
        }))
    }
}

struct VoilaViewAssistant {
    background_node: ShapeNode,
    circle_node: ShapeNode,
    cloud_provider_app: Arc<LaunchedApp>,
    replicas: BTreeMap<u32, ReplicaData>,
}

/// Represents an emulated replica and holds its internal state.
#[allow(unused)]
struct ReplicaData {
    environment_ctrl: EnvironmentControllerProxy,
    sessionmgr_app: LaunchedApp,
    view: ChildViewData,
}

impl VoilaViewAssistant {
    fn create_replica(
        &mut self,
        profile_id: &str,
        session: &SessionPtr,
        root_node: &EntityNode,
    ) -> Result<(), Error> {
        let replica_random_number = rand::thread_rng().gen_range(1, 1000000);
        let replica_id = format!("voila-r{}", replica_random_number.to_string());
        fx_log_info!("creating a replica {}", replica_id);

        // Launch an instance of sessionmgr for the replica.
        let (server, environment_ctrl, app) =
            make_replica_env(&replica_id, Arc::clone(&self.cloud_provider_app))?;
        fasync::spawn(server.unwrap_or_else(|e| panic!("error providing services: {:?}", e)));
        let sessionmgr = app.connect_to_service(SessionmgrMarker)?;

        // Set up the emulated account.
        let mut account = Account {
            id: replica_id.clone(),
            identity_provider: IdentityProvider::Dev,
            display_name: replica_id.clone(),
            image_url: "https://example.com".to_string(),
            url: "https://example.com".to_string(),
            profile_id: profile_id.to_string(),
        };

        // Set up shell configs.
        let mut session_shell_config = AppConfig { url: ERMINE_URI.to_string(), args: None };
        let mut story_shell_config = AppConfig { url: MONDRIAN_URI.to_string(), args: None };

        // Set up views.
        let (mut view_token, view_holder_token) = new_view_token_pair()?;

        let host_node = EntityNode::new(session.clone());
        let host_view_holder = ViewHolder::new(session.clone(), view_holder_token, None);
        host_node.attach(&host_view_holder);
        root_node.add_child(&host_node);

        let view_data = ChildViewData::new(host_node, host_view_holder, Toggle::new(&session)?);
        root_node.add_child(&view_data.toggle.node());
        let session_data = ReplicaData {
            environment_ctrl: environment_ctrl,
            sessionmgr_app: app,
            view: view_data,
        };
        self.replicas.insert(session_data.view.id(), session_data);

        // Set up SessionContext.
        let (session_context_client, session_context_server) =
            create_endpoints::<SessionContextMarker>()?;
        let session_context = SessionContext {};
        let session_context_stream = session_context_server.into_stream()?;
        fasync::spawn_local(
            async move {
                await!(session_context.handle_requests_from_stream(session_context_stream))
                    .unwrap_or_else(|err| {
                        fx_log_warn!("error handling SessionContext request channel: {:?}", err);
                    })
            },
        );

        sessionmgr
            .initialize(
                "session_id", /* session_id */
                Some(OutOfLine(&mut account)),
                &mut session_shell_config,
                &mut story_shell_config,
                false, /* use_session_shell_for_story_shell_factory */
                None,  /* ledger_token_manager */
                None,  /* agent_token_manager */
                session_context_client,
                &mut view_token,
            )
            .context("Failed to issue initialize request for sessionmgr")?;
        Ok(())
    }
}

impl ViewAssistant for VoilaViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        set_node_color(
            context.session,
            &self.background_node,
            &Color { r: 0x00, g: 0x00, b: 0xff, a: 0xff },
        );
        context.root_node.add_child(&self.background_node);

        set_node_color(
            context.session,
            &self.circle_node,
            &Color { r: 0xff, g: 0x00, b: 0xff, a: 0xff },
        );
        context.root_node.add_child(&self.circle_node);

        let profile_random_number = rand::thread_rng().gen_range(1, 1000000);
        let profile_id = format!("voila-p{}", profile_random_number.to_string());
        self.create_replica(&profile_id, context.session, context.root_node)?;
        self.create_replica(&profile_id, context.session, context.root_node)?;
        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        let center_x = context.size.width * 0.5;
        let center_y = context.size.height * 0.5;
        self.background_node.set_shape(&Rectangle::new(
            context.session.clone(),
            context.size.width,
            context.size.height,
        ));
        self.background_node.set_translation(center_x, center_y, 0.0);

        let circle_radius = context.size.width.min(context.size.height) * 0.25;
        self.circle_node.set_shape(&Circle::new(context.session.clone(), circle_radius));
        self.circle_node.set_translation(center_x, center_y, 0.0);

        let mut views: Vec<&mut ChildViewData> =
            self.replicas.iter_mut().map(|(_key, child_session)| &mut child_session.view).collect();
        layout(&mut views, &context.size, &context.session)?;
        Ok(())
    }

    fn handle_message(&mut self, message: Message) {
        if let Some(voila_message) = message.downcast_ref::<VoilaMessage>() {
            match voila_message {
                VoilaMessage::ReplicaConnectivityToggled(key) => {
                    let maybe_replica = self.replicas.get_mut(key);
                    match maybe_replica {
                        Some(replica) => replica.view.toggle.toggle(),
                        None => {
                            fx_log_warn!("did not find the toggle to toggle");
                        }
                    }
                }
            }
        }
    }

    fn handle_pointer_event(
        &mut self,
        context: &mut ViewAssistantContext,
        pointer_event: &PointerEvent,
    ) -> Result<(), Error> {
        for (key, child_session) in &mut self.replicas {
            if child_session
                .view
                .toggle
                .bounds
                .contains(&Point::new(pointer_event.x, pointer_event.y))
            {
                child_session.view.toggle.handle_pointer_event(context, pointer_event, *key);
            }
        }
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    fx_log::init_with_tags(&["voila"])?;
    fx_log::set_severity(fx_log::levels::INFO);
    let assistant = VoilaAppAssistant {};
    App::run(Box::new(assistant))
}
