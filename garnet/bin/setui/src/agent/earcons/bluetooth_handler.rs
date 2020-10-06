// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::earcons::agent::CommonEarconsParams;
use crate::agent::earcons::sound_ids::{
    BLUETOOTH_CONNECTED_SOUND_ID, BLUETOOTH_DISCONNECTED_SOUND_ID,
};
use crate::agent::earcons::utils::connect_to_sound_player;
use crate::agent::earcons::utils::play_sound;
use crate::call;
use crate::internal::event::Publisher;

use anyhow::{format_err, Context, Error};
use fidl::encoding::Decodable;
use fidl::endpoints::create_request_stream;
use fidl_fuchsia_media_sessions2::{
    DiscoveryMarker, SessionsWatcherRequest, SessionsWatcherRequestStream, WatchOptions,
};
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_warn};
use futures::stream::TryStreamExt;
use std::collections::HashSet;

/// Type for uniquely identifying bluetooth media sessions.
type SessionId = u64;

/// The file path for the earcon to be played for bluetooth connecting.
const BLUETOOTH_CONNECTED_FILE_PATH: &str = "bluetooth-connected.wav";

/// The file path for the earcon to be played for bluetooth disconnecting.
const BLUETOOTH_DISCONNECTED_FILE_PATH: &str = "bluetooth-disconnected.wav";

pub const BLUETOOTH_DOMAIN: &str = "Bluetooth";

/// The `BluetoothHandler` takes care of the earcons functionality on bluetooth connection
/// and disconnection.
#[derive(Debug)]
pub struct BluetoothHandler {
    // Parameters common to all earcons handlers.
    common_earcons_params: CommonEarconsParams,
    // The publisher to use for connecting to services.
    publisher: Publisher,
    // The ids of the media sessions that are currently active.
    active_sessions: HashSet<SessionId>,
}

/// The type of bluetooth earcons sound.
enum BluetoothSoundType {
    CONNECTED,
    DISCONNECTED,
}

impl BluetoothHandler {
    pub async fn create(publisher: Publisher, params: CommonEarconsParams) -> Result<(), Error> {
        let mut handler = Self {
            common_earcons_params: params,
            publisher,
            active_sessions: HashSet::<SessionId>::new(),
        };
        handler.watch_bluetooth_connections().await
    }

    /// Watch for media session changes. The media sessions that have the
    /// Bluetooth mode in their metadata signify a bluetooth connection.
    /// The id of a disconnected device will be received on removal.
    pub async fn watch_bluetooth_connections(&mut self) -> Result<(), Error> {
        // Connect to media session Discovery service.
        let discovery_connection_result = self
            .common_earcons_params
            .service_context
            .lock()
            .await
            .connect_with_publisher::<DiscoveryMarker>(self.publisher.clone())
            .await
            .context("Connecting to fuchsia.media.sessions2.Discovery");

        let discovery_proxy = discovery_connection_result.map_err(|e| {
            format_err!("Failed to connect to fuchsia.media.sessions2.Discovery: {:?}", e)
        })?;

        // Create and handle the request stream of media sessions.
        let (watcher_client, watcher_requests) = create_request_stream()
            .map_err(|e| format_err!("Error creating watcher request stream: {:?}", e))?;

        call!(discovery_proxy =>
            watch_sessions(WatchOptions::new_empty(), watcher_client))
        .map_err(|e| format_err!("Unable to start discovery of MediaSessions: {:?}", e))?;

        self.handle_bluetooth_connections(watcher_requests);
        Ok(())
    }

    /// Handles the stream of media session updates, and possibly plays earcons
    /// sounds based on what type of update is received.
    fn handle_bluetooth_connections(&mut self, mut watcher_requests: SessionsWatcherRequestStream) {
        let mut active_sessions_clone = self.active_sessions.clone();
        let publisher = self.publisher.clone();
        let common_earcons_params = self.common_earcons_params.clone();

        fasync::Task::spawn(async move {
            while let Ok(Some(req)) = watcher_requests.try_next().await {
                match req {
                    SessionsWatcherRequest::SessionUpdated {
                        session_id: id,
                        session_info_delta: delta,
                        responder,
                    } => {
                        if let Err(e) = responder.send() {
                            fx_log_err!("Failed to acknowledge delta from SessionWatcher: {:?}", e);
                            return;
                        }

                        if active_sessions_clone.contains(&id)
                            || !matches!(delta.domain, Some(name) if name == BLUETOOTH_DOMAIN)
                        {
                            continue;
                        }
                        active_sessions_clone.insert(id);

                        let publisher = publisher.clone();
                        let common_earcons_params = common_earcons_params.clone();
                        fasync::Task::spawn(async move {
                            play_bluetooth_sound(
                                common_earcons_params,
                                publisher,
                                BluetoothSoundType::CONNECTED,
                            )
                            .await;
                        })
                        .detach();
                    }
                    SessionsWatcherRequest::SessionRemoved { session_id, responder } => {
                        if let Err(e) = responder.send() {
                            fx_log_err!(
                                "Failed to acknowledge session removal from SessionWatcher: {:?}",
                                e
                            );
                            return;
                        }

                        if !active_sessions_clone.contains(&session_id) {
                            fx_log_warn!(
                                "Tried to remove nonexistent media session id {:?}",
                                session_id
                            );
                            continue;
                        }
                        active_sessions_clone.remove(&session_id);
                        let publisher = publisher.clone();
                        let common_earcons_params = common_earcons_params.clone();
                        fasync::Task::spawn(async move {
                            play_bluetooth_sound(
                                common_earcons_params,
                                publisher,
                                BluetoothSoundType::DISCONNECTED,
                            )
                            .await;
                        })
                        .detach();
                    }
                }
            }
            // try_next failed, print error and exit.
            fx_log_err!("Failed to serve Watcher service");
        })
        .detach();
    }
}

/// Play a bluetooth earcons sound.
async fn play_bluetooth_sound(
    common_earcons_params: CommonEarconsParams,
    publisher: Publisher,
    sound_type: BluetoothSoundType,
) {
    // Connect to the SoundPlayer if not already connected.
    connect_to_sound_player(
        publisher,
        common_earcons_params.service_context.clone(),
        common_earcons_params.sound_player_connection.clone(),
    )
    .await;

    let sound_player_connection = common_earcons_params.sound_player_connection.clone();
    let sound_player_connection_lock = sound_player_connection.lock().await;
    let sound_player_added_files = common_earcons_params.sound_player_added_files.clone();

    if let Some(sound_player_proxy) = sound_player_connection_lock.as_ref() {
        match sound_type {
            BluetoothSoundType::CONNECTED => {
                if play_sound(
                    &sound_player_proxy,
                    BLUETOOTH_CONNECTED_FILE_PATH,
                    BLUETOOTH_CONNECTED_SOUND_ID,
                    sound_player_added_files.clone(),
                )
                .await
                .is_err()
                {
                    fx_log_err!("[bluetooth_earcons_handler] failed to play bluetooth earcon connection sound");
                }
            }
            BluetoothSoundType::DISCONNECTED => {
                if play_sound(
                    &sound_player_proxy,
                    BLUETOOTH_DISCONNECTED_FILE_PATH,
                    BLUETOOTH_DISCONNECTED_SOUND_ID,
                    sound_player_added_files.clone(),
                )
                .await
                .is_err()
                {
                    fx_log_err!("[bluetooth_earcons_handler] failed to play bluetooth earcon disconnection sound");
                }
            }
        };
    } else {
        fx_log_err!("[bluetooth_earcons_handler] failed to play bluetooth earcon sound: no sound player connection");
    }
}
