// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::base::{Agent, Invocation, Lifespan};
use crate::agent::volume_change_earcons_handler::VolumeChangeEarconsHandler;
use crate::service_context::ServiceContextHandle;
use crate::switchboard::base::{ListenSession, SettingType, SwitchboardError};

use anyhow::{Context, Error};
use fidl::endpoints::create_request_stream;
use fidl_fuchsia_media::{
    AudioRenderUsage,
    Usage::RenderUsage,
    UsageReporterMarker,
    UsageState::{Ducked, Muted},
    UsageWatcherRequest::OnStateChanged,
};
use fidl_fuchsia_media_sounds::{PlayerMarker, PlayerProxy};
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use futures::StreamExt;
use std::collections::HashSet;
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc,
};

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
    listen_session: Option<Box<dyn ListenSession + Send + Sync + 'static>>,
}

/// Only used to tell if the listen session was inadvertently dropped. If it was dropped, the listen
/// session will also drop out of scope, and the listen callback will not receive any events.
impl Drop for SwitchboardListenSessionHolder {
    fn drop(&mut self) {
        fx_log_err!("[earcons_agent] SwitchboardListenSessionHolder dropped");
    }
}

impl EarconsAgent {
    pub fn new() -> EarconsAgent {
        Self {
            priority_stream_playing: Arc::new(AtomicBool::new(false)),
            sound_player_connection: Arc::new(Mutex::new(None)),
        }
    }
}

impl SwitchboardListenSessionHolder {
    pub fn new() -> SwitchboardListenSessionHolder {
        Self { listen_session: None }
    }
}

async fn reply(invocation: Invocation, result: Result<(), Error>) {
    if let Some(sender) = invocation.ack_sender.lock().await.take() {
        sender.send(result).ok();
    }
}

impl Agent for EarconsAgent {
    fn invoke(&mut self, invocation: Invocation) -> Result<bool, Error> {
        // Only process service lifespans.
        if invocation.context.lifespan != Lifespan::Service {
            return Ok(false);
        }

        let priority_stream_playing = self.priority_stream_playing.clone();
        let service_context = invocation.context.service_context.clone();
        let return_invocation = invocation.clone();
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

        let volume_change_handler = VolumeChangeEarconsHandler::new(common_earcons_params.clone());
        let volume_change_handler_clone = volume_change_handler.clone();

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
                invocation,
                volume_change_handler.clone(),
                common_earcons_params.clone(),
            )
            .await;

            reply(return_invocation, Ok(())).await;
        });

        return Ok(true);
    }
}

// Listen to all the events on the switchboard that come through on the Audio setting
// type. Make corresponding requests on the corresponding handlers to get the necessary state.
async fn listen_to_audio_events(
    invocation: Invocation,
    volume_change_handler: VolumeChangeEarconsHandler,
    common_earcons_params: CommonEarconsParams,
) {
    let switchboard = invocation.context.switchboard.clone();
    let common_earcons_params_clone = common_earcons_params.clone();
    let listen_result = switchboard.clone().lock().await.listen(
        SettingType::Audio,
        Arc::new(move |setting| {
            // Connect to the SoundPlayer the first time an earcons event occurs.
            connect_to_sound_player(
                common_earcons_params.service_context.clone(),
                common_earcons_params.sound_player_connection.clone(),
            );

            volume_change_handler.get_volume_info(switchboard.clone(), setting);
        }),
    );
    assert!(listen_result.is_ok());

    // Store the listen session so it doesn't get dropped and cancel the connection.
    if let Ok(session) = listen_result {
        let mut listen_session_holder_lock =
            common_earcons_params_clone.listen_session_holder.lock().await;
        listen_session_holder_lock.listen_session = Some(session);
    }
}

/// Determine when the background usage is being played on.
///
/// We should not play earcons over a higher priority stream. In order to figure out whether there
/// is a more high-priority stream playing, we watch the BACKGROUND audio usage. If it is muted or
/// ducked, we should not play the earcons.
// TODO (fxb/44381): Remove this when it is no longer necessary to check the background usage.
async fn watch_background_usage(
    service_context_handle: &ServiceContextHandle,
    priority_stream_playing: Arc<AtomicBool>,
) -> Result<(), Error> {
    let usage_reporter_proxy =
        service_context_handle.lock().await.connect::<UsageReporterMarker>().await?;

    // Create channel for usage reporter watch results.
    let (watcher_client, mut watcher_requests) = create_request_stream()?;

    // Watch for changes in usage.
    usage_reporter_proxy.watch(&mut RenderUsage(AudioRenderUsage::Background), watcher_client)?;

    // Handle changes in the usage, update state.
    while let Some(event) = watcher_requests.next().await {
        match event {
            Ok(OnStateChanged { usage: _usage, state, responder }) => {
                responder.send()?;
                priority_stream_playing.store(
                    match state {
                        Muted(_) | Ducked(_) => true,
                        _ => false,
                    },
                    Ordering::SeqCst,
                );
            }
            Err(_) => {
                return Err(Error::new(SwitchboardError::ExternalFailure {
                    setting_type: SettingType::Audio,
                    dependency: "UsageReporterProxy".to_string(),
                    request: "watch".to_string(),
                }));
            }
        }
    }
    Ok(())
}

/// Establish a connection to the sound player and return the proxy representing the service.
/// Will not do anything if the sound player connection is already established.
fn connect_to_sound_player(
    service_context_handle: ServiceContextHandle,
    sound_player_connection: Arc<Mutex<Option<PlayerProxy>>>,
) {
    fasync::spawn(async move {
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
    });
}
