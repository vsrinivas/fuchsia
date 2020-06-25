// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::base::{
    AgentError, Context as AgentContext, Descriptor, Invocation, InvocationResult, Lifespan,
};
use crate::agent::earcons::bluetooth_handler::watch_bluetooth_connections;
use crate::agent::earcons::volume_change_handler::VolumeChangeHandler;
use crate::blueprint_definition;
use crate::internal::agent::Payload;
use crate::internal::switchboard;
use crate::service_context::ServiceContextHandle;

use fidl_fuchsia_media_sounds::PlayerProxy;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use std::collections::HashSet;
use std::sync::{atomic::AtomicBool, Arc};

blueprint_definition!(Descriptor::Component("earcons_agent"), Agent::create);

/// The Earcons Agent is responsible for watching updates to relevant sources that need to play
/// sounds.
pub struct Agent {
    sound_player_connection: Arc<Mutex<Option<PlayerProxy>>>,
    switchboard_messenger: switchboard::message::Messenger,
}

/// Params that are common to handlers of the earcons agent.
#[derive(Debug, Clone)]
pub struct CommonEarconsParams {
    pub service_context: ServiceContextHandle,
    pub sound_player_added_files: Arc<Mutex<HashSet<&'static str>>>,
    pub sound_player_connection: Arc<Mutex<Option<PlayerProxy>>>,
}

impl Agent {
    async fn create(mut context: AgentContext) {
        let messenger_result = context.create_switchboard_messenger().await;

        if messenger_result.is_err() {
            fx_log_err!("EarconAgent: could not acquire switchboard messenger");
            return;
        }

        let mut agent = Agent {
            sound_player_connection: Arc::new(Mutex::new(None)),
            switchboard_messenger: messenger_result.unwrap(),
        };

        fasync::spawn(async move {
            while let Ok((payload, client)) = context.receptor.next_payload().await {
                if let Payload::Invocation(invocation) = payload {
                    client.reply(Payload::Complete(agent.handle(invocation).await)).send().ack();
                }
            }
        });
    }

    async fn handle(&mut self, invocation: Invocation) -> InvocationResult {
        // Only process service lifespans.
        if let Lifespan::Initialization(_context) = invocation.lifespan {
            let common_earcons_params = CommonEarconsParams {
                service_context: invocation.service_context,
                sound_player_added_files: Arc::new(Mutex::new(HashSet::new())),
                sound_player_connection: self.sound_player_connection.clone(),
            };

            if VolumeChangeHandler::create(
                common_earcons_params.clone(),
                self.switchboard_messenger.clone(),
            )
            .await
            .is_err()
            {
                // For now, report back as an error to prevent issues on
                // platforms that don't support the handler's dependencies.
                fx_log_err!("Could not set up VolumeChangeHandler");
            }

            fasync::spawn(async move {
                // Watch for bluetooth connections and play sounds on change.
                let bluetooth_connection_active = Arc::new(AtomicBool::new(true));
                watch_bluetooth_connections(common_earcons_params, bluetooth_connection_active);
            });

            return Ok(());
        } else {
            return Err(AgentError::UnhandledLifespan);
        }
    }
}
