// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::earcons::agent::CommonEarconsParams;
use crate::agent::earcons::sound_ids::{
    BLUETOOTH_CONNECTED_SOUND_ID, BLUETOOTH_DISCONNECTED_SOUND_ID,
};
use crate::agent::earcons::utils::connect_to_sound_player;
use crate::agent::earcons::utils::play_sound;
use crate::audio::types::{AudioSettingSource, AudioStreamType, SetAudioStream};
use crate::base::{SettingInfo, SettingType};
use crate::call;
use crate::event::Publisher;
use crate::handler::base::{Payload, Request};
use crate::message::base::Audience;
use crate::service;
use crate::trace;

use anyhow::{format_err, Context, Error};
use fidl::encoding::Decodable;
use fidl::endpoints::create_request_stream;
use fidl_fuchsia_media_sessions2::{
    DiscoveryMarker, SessionsWatcherRequest, SessionsWatcherRequestStream, WatchOptions,
};
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_warn};
use fuchsia_trace as ftrace;
use futures::stream::TryStreamExt;
use std::collections::HashSet;

/// Type for uniquely identifying bluetooth media sessions.
type SessionId = u64;

/// The file path for the earcon to be played for bluetooth connecting.
const BLUETOOTH_CONNECTED_FILE_PATH: &str = "bluetooth-connected.wav";

/// The file path for the earcon to be played for bluetooth disconnecting.
const BLUETOOTH_DISCONNECTED_FILE_PATH: &str = "bluetooth-disconnected.wav";

pub(crate) const BLUETOOTH_DOMAIN: &str = "Bluetooth";

/// The `BluetoothHandler` takes care of the earcons functionality on bluetooth connection
/// and disconnection.
#[derive(Debug)]
pub(super) struct BluetoothHandler {
    // Parameters common to all earcons handlers.
    common_earcons_params: CommonEarconsParams,
    // The publisher to use for connecting to services.
    publisher: Publisher,
    // The ids of the media sessions that are currently active.
    active_sessions: HashSet<SessionId>,
    // A messenger with which to send a requests via the message hub.
    messenger: service::message::Messenger,
}

/// The type of bluetooth earcons sound.
enum BluetoothSoundType {
    Connected,
    Disconnected,
}

impl BluetoothHandler {
    pub(super) async fn create(
        publisher: Publisher,
        params: CommonEarconsParams,
        messenger: service::message::Messenger,
    ) -> Result<(), Error> {
        let mut handler = Self {
            common_earcons_params: params,
            publisher,
            active_sessions: HashSet::<SessionId>::new(),
            messenger,
        };
        handler.watch_bluetooth_connections().await
    }

    /// Watch for media session changes. The media sessions that have the
    /// Bluetooth mode in their metadata signify a bluetooth connection.
    /// The id of a disconnected device will be received on removal.
    pub(super) async fn watch_bluetooth_connections(&mut self) -> Result<(), Error> {
        // Connect to media session Discovery service.
        let discovery_connection_result = self
            .common_earcons_params
            .service_context
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
        let messenger = self.messenger.clone();

        fasync::Task::spawn(async move {
            loop {
                let maybe_req = watcher_requests.try_next().await;
                match maybe_req {
                    Ok(Some(req)) => {
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
                                let _ = active_sessions_clone.insert(id);

                                let publisher = publisher.clone();
                                let common_earcons_params = common_earcons_params.clone();
                                let messenger = messenger.clone();
                                fasync::Task::spawn(async move {
                                    play_bluetooth_sound(
                                        common_earcons_params,
                                        publisher,
                                        BluetoothSoundType::Connected,
                                        messenger,
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
                                let _ = active_sessions_clone.remove(&session_id);
                                let publisher = publisher.clone();
                                let common_earcons_params = common_earcons_params.clone();
                                let messenger = messenger.clone();
                                fasync::Task::spawn(async move {
                                    play_bluetooth_sound(
                                        common_earcons_params,
                                        publisher,
                                        BluetoothSoundType::Disconnected,
                                        messenger,
                                    )
                                    .await;
                                })
                                .detach();
                            }
                        }
                    },
                    Ok(None) => {
                        fx_log_warn!("stream ended on fuchsia.media.sessions2.SessionsWatcher");
                        break;
                    },
                    Err(e) => {
                        fx_log_err!("failed to watch fuchsia.media.sessions2.SessionsWatcher: {:?}", &e);
                        break;
                    },
                }
            }
        })
        .detach();
    }
}

/// Play a bluetooth earcons sound.
async fn play_bluetooth_sound(
    common_earcons_params: CommonEarconsParams,
    publisher: Publisher,
    sound_type: BluetoothSoundType,
    messenger: service::message::Messenger,
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
        match_background_to_media(messenger).await;
        match sound_type {
            BluetoothSoundType::Connected => {
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
            BluetoothSoundType::Disconnected => {
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

/// Match the background volume to the current media volume before playing the bluetooth earcon.
async fn match_background_to_media(messenger: service::message::Messenger) {
    // Get the current audio info.
    let mut get_receptor = messenger
        .message(
            Payload::Request(Request::Get).into(),
            Audience::Address(service::Address::Handler(SettingType::Audio)),
        )
        .send();

    // Extract media and background volumes.
    let mut media_volume = 0.0;
    let mut background_volume = 0.0;
    if let Ok((Payload::Response(Ok(Some(SettingInfo::Audio(info)))), _)) =
        get_receptor.next_of::<Payload>().await
    {
        info.streams.iter().for_each(|stream| {
            if stream.stream_type == AudioStreamType::Media {
                media_volume = stream.user_volume_level;
            } else if stream.stream_type == AudioStreamType::Background {
                background_volume = stream.user_volume_level;
            }
        })
    } else {
        fx_log_err!("Could not extract background and media volumes")
    };

    // If they are different, set the background volume to match the media volume.
    if media_volume != background_volume {
        let id = ftrace::Id::new();
        trace!(id, "bluetooth_handler set background volume");
        let mut receptor = messenger
            .message(
                Payload::Request(Request::SetVolume(
                    vec![SetAudioStream {
                        stream_type: AudioStreamType::Background,
                        source: AudioSettingSource::System,
                        user_volume_level: Some(media_volume),
                        user_volume_muted: None,
                    }],
                    id,
                ))
                .into(),
                Audience::Address(service::Address::Handler(SettingType::Audio)),
            )
            .send();

        if receptor.next_payload().await.is_err() {
            fx_log_err!(
                "Failed to play bluetooth connection sound after waiting for message response"
            );
        }
    }
}
