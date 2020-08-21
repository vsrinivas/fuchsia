// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::earcons::agent::CommonEarconsParams;
use crate::agent::earcons::sound_ids::{VOLUME_CHANGED_SOUND_ID, VOLUME_MAX_SOUND_ID};
use crate::agent::earcons::utils::{connect_to_sound_player, play_sound};
use crate::audio::{create_default_modified_timestamps, ModifiedTimestamps};
use crate::input::monitor_media_buttons_using_publisher;
use crate::internal::event;
use crate::internal::switchboard;
use crate::message::base::Audience;
use crate::message::receptor::extract_payload;
use crate::switchboard::base::{
    AudioInfo, AudioStream, AudioStreamType, SettingRequest, SettingResponse, SettingType,
};

use anyhow::Error;
use fidl_fuchsia_ui_input::MediaButtonsEvent;
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_debug, fx_log_err};
use futures::FutureExt;
use futures::StreamExt;
use std::collections::{HashMap, HashSet};

/// The `VolumeChangeHandler` takes care of the earcons functionality on volume change.
pub struct VolumeChangeHandler {
    common_earcons_params: CommonEarconsParams,
    last_user_volumes: HashMap<AudioStreamType, f32>,
    volume_button_event: i8,
    modified_timestamps: ModifiedTimestamps,
    switchboard_messenger: switchboard::message::Messenger,
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
        // Listen to button presses.
        let (input_tx, mut input_rx) = futures::channel::mpsc::unbounded::<MediaButtonsEvent>();
        monitor_media_buttons_using_publisher(
            publisher.clone(),
            params.service_context.clone(),
            input_tx,
        )
        .await?;

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

        let (volume_tx, mut volume_rx) = futures::channel::mpsc::unbounded::<SettingResponse>();

        fasync::Task::spawn(async move {
            let mut handler = Self {
                common_earcons_params: params,
                last_user_volumes,
                volume_button_event: 0,
                modified_timestamps: create_default_modified_timestamps(),
                switchboard_messenger: switchboard_messenger.clone(),
                publisher,
            };

            let mut listen_receptor = switchboard_messenger
                .message(
                    switchboard::Payload::Listen(switchboard::Listen::Request(SettingType::Audio)),
                    Audience::Address(switchboard::Address::Switchboard),
                )
                .send();
            loop {
                let listen_receptor_fuse = listen_receptor.next().fuse();
                futures::pin_mut!(listen_receptor_fuse);

                futures::select! {
                    event = input_rx.next() => {
                        if let Some(event) = event {
                            handler.on_button_event(event).await;
                        }
                    }
                    volume_change_event = listen_receptor_fuse => {
                        if let Some(switchboard::Payload::Listen(switchboard::Listen::Update(setting))) = extract_payload(volume_change_event) {
                            handler.on_changed_setting(setting, volume_tx.clone()).await;
                        }
                    }
                    volume_response = volume_rx.next() => {
                        if let Some(SettingResponse::Audio(audio_info)) = volume_response {
                            handler.on_audio_info(audio_info).await;
                        }
                    }
                }
            }
        }).detach();

        Ok(())
    }

    /// Called when a new media button input event is available from the
    /// listener. Stores the last event.
    async fn on_button_event(&mut self, event: MediaButtonsEvent) {
        if let Some(volume) = event.volume {
            self.volume_button_event = volume;
        }
    }

    /// Called when a setting `VolumeChangeHandler` has registered as a listener
    /// for indicates there is a new value. Requests the updated value from
    /// the `Switchboard`.
    async fn on_changed_setting(
        &mut self,
        setting_type: SettingType,
        response_sender: futures::channel::mpsc::UnboundedSender<SettingResponse>,
    ) {
        let mut receptor = self
            .switchboard_messenger
            .message(
                switchboard::Payload::Action(switchboard::Action::Request(
                    setting_type,
                    SettingRequest::Get,
                )),
                Audience::Address(switchboard::Address::Switchboard),
            )
            .send();

        if let Ok((
            switchboard::Payload::Action(switchboard::Action::Response(Ok(Some(response)))),
            _,
        )) = receptor.next_payload().await
        {
            response_sender.unbounded_send(response).ok();
        } else {
            fx_log_err!("[earcons_agent] Failed to get volume state from switchboard");
        }
    }

    /// Calculates and returns the streams that were changed based on
    /// their timestamps, updating them in the stored timestamps if
    /// they were changed.
    fn calculate_changed_streams(
        &mut self,
        all_streams: [AudioStream; 5],
        new_modified_timestamps: ModifiedTimestamps,
    ) -> Vec<AudioStream> {
        let mut changed_stream_types = HashSet::new();
        for (stream_type, timestamp) in new_modified_timestamps {
            if self.modified_timestamps.get(&stream_type) != Some(&timestamp) {
                changed_stream_types.insert(stream_type);
                self.modified_timestamps.insert(stream_type, timestamp);
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
        let volume_up_max_pressed = new_user_volume == MAX_VOLUME && self.volume_button_event == 1;
        let last_user_volume = self.last_user_volumes.get(&stream_type);

        // Logging for debugging volume changes.
        fx_log_debug!("[earcons_agent] Volume up pressed while max: {}", volume_up_max_pressed);
        fx_log_debug!(
            "[earcons_agent] New {:?} user volume: {:?}, Last {:?} user volume: {:?}",
            stream_type,
            new_user_volume,
            stream_type,
            last_user_volume,
        );

        if last_user_volume != Some(&new_user_volume) || volume_up_max_pressed {
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
        let changed_streams = if audio_info.modified_timestamps.is_none() {
            Vec::new()
        } else {
            self.calculate_changed_streams(
                audio_info.streams,
                audio_info.modified_timestamps.unwrap(),
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
    use crate::internal::common::default_time;
    use crate::message::base::MessengerType;
    use crate::service_context::ServiceContext;
    use futures::lock::Mutex;
    use std::sync::Arc;

    fn fake_values() -> (
        [AudioStream; 5],   // fake_streams
        ModifiedTimestamps, // old_timestamps
        ModifiedTimestamps, // new_timestamps
        Vec<AudioStream>,   // expected_changed_streams
    ) {
        let fake_streams = default_audio_info().streams;
        let old_timestamps = create_default_modified_timestamps();
        let new_timestamps = [
            (AudioStreamType::Background, default_time().to_string()),
            (AudioStreamType::Media, "2020-05-29 02:25:00.604748666 UTC".to_string()),
            (AudioStreamType::Interruption, default_time().to_string()),
            (AudioStreamType::SystemAgent, "2020-05-29 02:25:00.813529234 UTC".to_string()),
            (AudioStreamType::Communication, "2020-05-29 02:25:01.004947253 UTC".to_string()),
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
        let switchboard_messenger_factory = switchboard::message::create_hub();
        let (messenger, _) =
            switchboard_messenger_factory.create(MessengerType::Unbound).await.unwrap();
        let publisher =
            event::Publisher::create(&event_messenger_factory, MessengerType::Unbound).await;
        let mut last_user_volumes = HashMap::new();
        last_user_volumes.insert(AudioStreamType::Media, 1.0);
        last_user_volumes.insert(AudioStreamType::Interruption, 0.5);

        let mut handler = VolumeChangeHandler {
            switchboard_messenger: messenger,
            common_earcons_params: CommonEarconsParams {
                service_context: ServiceContext::create(None, None),
                sound_player_added_files: Arc::new(Mutex::new(HashSet::new())),
                sound_player_connection: Arc::new(Mutex::new(None)),
            },
            last_user_volumes,
            volume_button_event: 0,
            modified_timestamps: old_timestamps,
            publisher,
        };
        let changed_streams = handler.calculate_changed_streams(fake_streams, new_timestamps);
        assert_eq!(changed_streams, expected_changed_streams);
    }
}
