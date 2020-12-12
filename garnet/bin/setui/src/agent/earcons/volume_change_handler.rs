// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::earcons::agent::CommonEarconsParams;
use crate::agent::earcons::sound_ids::{VOLUME_CHANGED_SOUND_ID, VOLUME_MAX_SOUND_ID};
use crate::agent::earcons::utils::{connect_to_sound_player, play_sound};
use crate::audio::{create_default_modified_counters, ModifiedCounters};
use crate::base::SettingInfo;
use crate::internal::event;
use crate::internal::switchboard;
use crate::message::base::Audience;
use crate::message::receptor::extract_payload;
use crate::switchboard::base::{
    AudioInfo, AudioStream, AudioStreamType, SettingRequest, SettingResponse, SettingType,
};

use anyhow::Error;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_debug;
use futures::StreamExt;
use std::collections::{HashMap, HashSet};

/// The `VolumeChangeHandler` takes care of the earcons functionality on volume change.
pub struct VolumeChangeHandler {
    common_earcons_params: CommonEarconsParams,
    last_user_volumes: HashMap<AudioStreamType, f32>,
    modified_counters: ModifiedCounters,
    publisher: event::Publisher,
}

/// The maximum volume level.
const MAX_VOLUME: f32 = 1.0;

/// The file path for the earcon to be played for max sound level.
const VOLUME_MAX_FILE_PATH: &str = "volume-max.wav";

/// The file path for the earcon to be played for volume changes below max volume level.
const VOLUME_CHANGED_FILE_PATH: &str = "volume-changed.wav";

impl VolumeChangeHandler {
    pub async fn create(
        publisher: event::Publisher,
        params: CommonEarconsParams,
        switchboard_messenger: switchboard::message::Messenger,
    ) -> Result<(), Error> {
        let mut receptor = switchboard_messenger
            .message(
                switchboard::Payload::Action(switchboard::Action::Request(
                    SettingType::Audio,
                    SettingRequest::Get,
                )),
                Audience::Address(switchboard::Address::Switchboard),
            )
            .send();

        // Get initial user media volume level.
        let last_user_volumes = if let Ok((
            switchboard::Payload::Action(switchboard::Action::Response(Ok(Some(
                SettingResponse::Audio(info),
            )))),
            _,
        )) = receptor.next_payload().await
        {
            // Create map from stream type to user volume levels for each stream.
            info.streams
                .iter()
                .filter(|x| {
                    x.stream_type == AudioStreamType::Media
                        || x.stream_type == AudioStreamType::Interruption
                })
                .map(|stream| (stream.stream_type, stream.user_volume_level))
                .collect()
        } else {
            // Could not extract info from response, default to empty volumes.
            HashMap::new()
        };

        fasync::Task::spawn(async move {
            let mut handler = Self {
                common_earcons_params: params,
                last_user_volumes,
                modified_counters: create_default_modified_counters(),
                publisher,
            };

            let listen_receptor = switchboard_messenger
                .message(
                    switchboard::Payload::Listen(switchboard::Listen::Request(SettingType::Audio)),
                    Audience::Address(switchboard::Address::Switchboard),
                )
                .send()
                .fuse();
            futures::pin_mut!(listen_receptor);

            loop {
                futures::select! {
                    volume_change_event = listen_receptor.next() => {
                        if let Some(
                            switchboard::Payload::Listen(switchboard::Listen::Update(setting, SettingInfo::Audio(audio_info)))
                        ) = extract_payload(volume_change_event) {
                            handler.on_audio_info(audio_info).await;
                        }
                    }
                    complete => break,
                }
            }
        })
        .detach();

        Ok(())
    }

    /// Calculates and returns the streams that were changed based on
    /// their timestamps, updating them in the stored timestamps if
    /// they were changed.
    fn calculate_changed_streams(
        &mut self,
        all_streams: [AudioStream; 5],
        new_modified_counters: ModifiedCounters,
    ) -> Vec<AudioStream> {
        let mut changed_stream_types = HashSet::new();
        for (stream_type, timestamp) in new_modified_counters {
            if self.modified_counters.get(&stream_type) != Some(&timestamp) {
                changed_stream_types.insert(stream_type);
                self.modified_counters.insert(stream_type, timestamp);
            }
        }

        all_streams
            .iter()
            .filter(|stream| changed_stream_types.contains(&stream.stream_type))
            .cloned()
            .collect()
    }

    /// Retrieve a user volume of the specified [stream_type] from the given [changed_streams].
    fn get_user_volume(
        &self,
        changed_streams: Vec<AudioStream>,
        stream_type: AudioStreamType,
    ) -> Option<f32> {
        changed_streams.iter().find(|&&x| x.stream_type == stream_type).map(|x| x.user_volume_level)
    }

    /// Helper for on_audio_info. Handles the changes for a specific AudioStreamType.
    /// Enables separate handling of earcons on different streams.
    async fn on_audio_info_for_stream(
        &mut self,
        new_user_volume: f32,
        stream_type: AudioStreamType,
    ) {
        let volume_is_max = new_user_volume == MAX_VOLUME;
        let last_user_volume = self.last_user_volumes.get(&stream_type);

        // Logging for debugging volume changes.
        fx_log_debug!(
            "[earcons_agent] New {:?} user volume: {:?}, Last {:?} user volume: {:?}",
            stream_type,
            new_user_volume,
            stream_type,
            last_user_volume,
        );

        if last_user_volume != Some(&new_user_volume) || volume_is_max {
            if last_user_volume != None {
                // On restore, the last media user volume is set for the first time, and registers
                // as different from the last seen volume, because it is initially None. Don't play
                // the earcons sound on that set.
                self.play_volume_sound(new_user_volume);
            }
            self.last_user_volumes.insert(stream_type, new_user_volume);
        }
    }

    /// Invoked when a new `AudioInfo` is retrieved. Determines whether an
    /// earcon should be played and plays sound if necessary.
    async fn on_audio_info(&mut self, audio_info: AudioInfo) {
        let changed_streams = if audio_info.modified_counters.is_none() {
            Vec::new()
        } else {
            self.calculate_changed_streams(
                audio_info.streams,
                audio_info.modified_counters.unwrap(),
            )
        };

        let media_user_volume =
            self.get_user_volume(changed_streams.clone(), AudioStreamType::Media);
        let interruption_user_volume =
            self.get_user_volume(changed_streams, AudioStreamType::Interruption);

        if let Some(media_user_volume) = media_user_volume {
            self.on_audio_info_for_stream(media_user_volume, AudioStreamType::Media).await;
        }
        if let Some(interruption_user_volume) = interruption_user_volume {
            self.on_audio_info_for_stream(interruption_user_volume, AudioStreamType::Interruption)
                .await;
        }
    }

    /// Play the earcons sound given the changed volume streams.
    ///
    /// The parameters are packaged together. See [VolumeChangeParams].
    fn play_volume_sound(&self, volume: f32) {
        let common_earcons_params = self.common_earcons_params.clone();

        let publisher = self.publisher.clone();
        fasync::Task::spawn(async move {
            // Connect to the SoundPlayer if not already connected.
            connect_to_sound_player(
                publisher,
                common_earcons_params.service_context.clone(),
                common_earcons_params.sound_player_connection.clone(),
            )
            .await;

            let sound_player_connection_clone =
                common_earcons_params.sound_player_connection.clone();
            let sound_player_connection = sound_player_connection_clone.lock().await;
            let sound_player_added_files = common_earcons_params.sound_player_added_files;

            if let (Some(sound_player_proxy), volume_level) =
                (sound_player_connection.as_ref(), volume)
            {
                if volume_level >= 1.0 {
                    play_sound(
                        &sound_player_proxy,
                        VOLUME_MAX_FILE_PATH,
                        VOLUME_MAX_SOUND_ID,
                        sound_player_added_files.clone(),
                    )
                    .await
                    .ok();
                } else if volume_level > 0.0 {
                    play_sound(
                        &sound_player_proxy,
                        VOLUME_CHANGED_FILE_PATH,
                        VOLUME_CHANGED_SOUND_ID,
                        sound_player_added_files.clone(),
                    )
                    .await
                    .ok();
                }
            }
        })
        .detach();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::audio::default_audio_info;
    use crate::message::base::MessengerType;
    use crate::service_context::ServiceContext;
    use futures::lock::Mutex;
    use std::sync::Arc;

    fn fake_values() -> (
        [AudioStream; 5], // fake_streams
        ModifiedCounters, // old_counters
        ModifiedCounters, // new_counters
        Vec<AudioStream>, // expected_changed_streams
    ) {
        let fake_streams = default_audio_info().streams;
        let old_timestamps = create_default_modified_counters();
        let new_timestamps = [
            (AudioStreamType::Background, 0),
            (AudioStreamType::Media, 1),
            (AudioStreamType::Interruption, 0),
            (AudioStreamType::SystemAgent, 2),
            (AudioStreamType::Communication, 3),
        ]
        .iter()
        .cloned()
        .collect();
        let expected_changed_streams = [fake_streams[1], fake_streams[3], fake_streams[4]].to_vec();
        (fake_streams, old_timestamps, new_timestamps, expected_changed_streams)
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_changed_streams() {
        let (fake_streams, old_timestamps, new_timestamps, expected_changed_streams) =
            fake_values();
        let event_messenger_factory = event::message::create_hub();
        let publisher =
            event::Publisher::create(&event_messenger_factory, MessengerType::Unbound).await;
        let mut last_user_volumes = HashMap::new();
        last_user_volumes.insert(AudioStreamType::Media, 1.0);
        last_user_volumes.insert(AudioStreamType::Interruption, 0.5);

        let mut handler = VolumeChangeHandler {
            common_earcons_params: CommonEarconsParams {
                service_context: ServiceContext::create(None, None),
                sound_player_added_files: Arc::new(Mutex::new(HashSet::new())),
                sound_player_connection: Arc::new(Mutex::new(None)),
            },
            last_user_volumes,
            modified_counters: old_timestamps,
            publisher,
        };
        let changed_streams = handler.calculate_changed_streams(fake_streams, new_timestamps);
        assert_eq!(changed_streams, expected_changed_streams);
    }
}
