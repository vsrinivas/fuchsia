// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::base::{AgentError, Invocation, InvocationResult, Lifespan};
use crate::agent::bluetooth_earcons_handler::watch_bluetooth_connections;
use crate::agent::volume_change_earcons_handler::{
    listen_to_audio_events, watch_background_usage, VolumeChangeEarconsHandler,
};
use crate::internal::agent::{message, Payload};
use crate::service_context::ServiceContextHandle;
use crate::switchboard::base::ListenSession;

use anyhow::Context;
use fidl_fuchsia_media_sounds::{PlayerMarker, PlayerProxy};
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use std::collections::HashSet;
use std::sync::{atomic::AtomicBool, Arc};

/// The Earcons Agent is responsible for watching updates to relevant sources that need to play
/// sounds.
#[derive(Debug)]
pub struct EarconsAgent {
    priority_stream_playing: Arc<AtomicBool>,
    sound_player_connection: Arc<Mutex<Option<PlayerProxy>>>,
}

/// Params that are common to handlers of the earcons agent.
#[derive(Debug, Clone)]
pub struct CommonEarconsParams {
    pub listen_session_holder: Arc<Mutex<SwitchboardListenSessionHolder>>,
    pub priority_stream_playing: Arc<AtomicBool>,
    pub service_context: ServiceContextHandle,
    pub sound_player_added_files: Arc<Mutex<HashSet<&'static str>>>,
    pub sound_player_connection: Arc<Mutex<Option<PlayerProxy>>>,
}

/// Used to hold the listen_session handed back to the client on listening to the Switchboard.
/// It must be saved until the lifetime of the callbacks from the listen call are complete.
pub struct SwitchboardListenSessionHolder {
    /// The listen session that is handed back from the listen call on the Switchboard.
    pub listen_session: Option<Box<dyn ListenSession + Send + Sync + 'static>>,
}

/// Only used to tell if the listen session was inadvertently dropped. If it was dropped, the listen
/// session will also drop out of scope, and the listen callback will not receive any events.
impl Drop for SwitchboardListenSessionHolder {
    fn drop(&mut self) {
        fx_log_err!("[earcons_agent] SwitchboardListenSessionHolder dropped");
    }
}

impl SwitchboardListenSessionHolder {
    pub fn new() -> SwitchboardListenSessionHolder {
        Self { listen_session: None }
    }
}

impl EarconsAgent {
    pub fn create(mut receptor: message::Receptor) {
        let mut agent = Self {
            priority_stream_playing: Arc::new(AtomicBool::new(false)),
            sound_player_connection: Arc::new(Mutex::new(None)),
        };

        fasync::spawn(async move {
            while let Ok((payload, client)) = receptor.next_payload().await {
                if let Payload::Invocation(invocation) = payload {
                    client.reply(Payload::Complete(agent.handle(invocation))).send().ack();
                }
            }
        });
    }

    fn handle(&mut self, invocation: Invocation) -> InvocationResult {
        // Only process service lifespans.
        if let Lifespan::Service(context) = invocation.lifespan.clone() {
            let priority_stream_playing = self.priority_stream_playing.clone();
            let service_context = invocation.service_context.clone();
            let sound_player_connection = self.sound_player_connection.clone();
            let sound_player_added_files: Arc<Mutex<HashSet<&str>>> =
                Arc::new(Mutex::new(HashSet::new()));
            let listen_session_holder = Arc::new(Mutex::new(SwitchboardListenSessionHolder::new()));

            let common_earcons_params = CommonEarconsParams {
                listen_session_holder: listen_session_holder.clone(),
                priority_stream_playing: priority_stream_playing.clone(),
                service_context: service_context.clone(),
                sound_player_added_files: sound_player_added_files.clone(),
                sound_player_connection: sound_player_connection.clone(),
            };

            let volume_change_handler =
                VolumeChangeEarconsHandler::new(common_earcons_params.clone());
            let volume_change_handler_clone = volume_change_handler.clone();

            let common_earcons_params_clone = common_earcons_params.clone();
            fasync::spawn(async move {
                volume_change_handler_clone.watch_last_volume_button_event();

                // Watch the background usage to determine whether another stream is currently active
                // that overrides the earcons.
                fasync::spawn(async move {
                    match watch_background_usage(&service_context, priority_stream_playing).await {
                        Ok(_) => {}
                        Err(err) => fx_log_err!("Failed while watching background usage: {}", err),
                    };
                });

                // Listen on the switchboard for the events that should trigger earcons to be played.
                listen_to_audio_events(
                    context.switchboard_client.clone(),
                    volume_change_handler.clone(),
                    common_earcons_params.clone(),
                )
                .await;

                // Watch for bluetooth connections and play sounds on change.
                let bluetooth_connection_active = Arc::new(AtomicBool::new(true));
                watch_bluetooth_connections(
                    common_earcons_params_clone.clone(),
                    bluetooth_connection_active,
                );
            });

            return Ok(());
        } else {
            return Err(AgentError::UnhandledLifespan);
        }
    }
}

/// Establish a connection to the sound player and return the proxy representing the service.
/// Will not do anything if the sound player connection is already established.
pub async fn connect_to_sound_player(
    service_context_handle: ServiceContextHandle,
    sound_player_connection: Arc<Mutex<Option<PlayerProxy>>>,
) {
    let mut sound_player_connection_lock = sound_player_connection.lock().await;
    if sound_player_connection_lock.is_none() {
        *sound_player_connection_lock = match service_context_handle
            .lock()
            .await
            .connect::<PlayerMarker>()
            .await
            .context("Connecting to fuchsia.media.sounds.Player")
        {
            Ok(result) => Some(result),
            Err(e) => {
                fx_log_err!("Failed to connect to fuchsia.media.sounds.Player: {}", e);
                None
            }
        }
    }
}
