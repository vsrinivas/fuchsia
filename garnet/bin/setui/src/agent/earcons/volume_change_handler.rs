// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::earcons::agent::CommonEarconsParams;
use crate::agent::earcons::sound_ids::{VOLUME_CHANGED_SOUND_ID, VOLUME_MAX_SOUND_ID};
use crate::agent::earcons::utils::{connect_to_sound_player, play_sound};
use crate::audio::{create_default_modified_timestamps, ModifiedTimestamps};
use crate::input::monitor_media_buttons;
use crate::internal::event::{earcon, Event, Publisher};
use crate::internal::switchboard;
use crate::message::base::Audience;
use crate::message::receptor::extract_payload;
use crate::switchboard::base::{
    AudioInfo, AudioStream, AudioStreamType, SettingRequest, SettingResponse, SettingType,
};

use anyhow::Error;
use fidl::endpoints::create_request_stream;
use fidl_fuchsia_media::{
    AudioRenderUsage,
    Usage::RenderUsage,
    UsageReporterMarker,
    UsageState::{Ducked, Muted},
    UsageWatcherRequest,
    UsageWatcherRequest::OnStateChanged,
};
use fidl_fuchsia_ui_input::MediaButtonsEvent;
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_debug, fx_log_err};
use futures::FutureExt;
use futures::StreamExt;
use std::collections::HashSet;

/// The `VolumeChangeHandler` takes care of the earcons functionality on volume change.
pub struct VolumeChangeHandler {
    // An event MessageHub publisher to notify the rest of the system of events,
    // such as suppressed earcons.
    event_publisher: Publisher,
    priority_stream_playing: bool,
    common_earcons_params: CommonEarconsParams,
    last_media_user_volume: Option<f32>,
    volume_button_event: i8,
    modified_timestamps: ModifiedTimestamps,
    switchboard_messenger: switchboard::message::Messenger,
}

/// The maximum volume level.
const MAX_VOLUME: f32 = 1.0;

/// The file path for the earcon to be played for max sound level.
const VOLUME_MAX_FILE_PATH: &str = "volume-max.wav";

/// The file path for the earcon to be played for volume changes below max volume level.
const VOLUME_CHANGED_FILE_PATH: &str = "volume-changed.wav";

impl VolumeChangeHandler {
    pub async fn create(
        params: CommonEarconsParams,
        event_publisher: Publisher,
        switchboard_messenger: switchboard::message::Messenger,
    ) -> Result<(), Error> {
        // Listen to button presses.
        let (input_tx, mut input_rx) = futures::channel::mpsc::unbounded::<MediaButtonsEvent>();
        monitor_media_buttons(params.service_context.clone(), input_tx).await?;

        // Get initial user media volume level.
        let mut last_media_user_volume = None;

        let mut receptor = switchboard_messenger
            .message(
                switchboard::Payload::Action(switchboard::Action::Request(
                    SettingType::Audio,
                    SettingRequest::Get,
                )),
                Audience::Address(switchboard::Address::Switchboard),
            )
            .send();

        if let Ok((
            switchboard::Payload::Action(switchboard::Action::Response(Ok(Some(
                SettingResponse::Audio(info),
            )))),
            _,
        )) = receptor.next_payload().await
        {
            last_media_user_volume = info
                .streams
                .iter()
                .find(|&&x| x.stream_type == AudioStreamType::Media)
                .map(|stream| stream.user_volume_level);
        }

        let (volume_tx, mut volume_rx) = futures::channel::mpsc::unbounded::<SettingResponse>();

        let usage_reporter_proxy =
            params.service_context.lock().await.connect::<UsageReporterMarker>().await?;

        // Create channel for usage reporter watch results.
        let (usage_tx, mut usage_rx) = create_request_stream()?;

        // Watch for changes in usage.
        usage_reporter_proxy.watch(&mut RenderUsage(AudioRenderUsage::Background), usage_tx)?;

        fasync::spawn(async move {
            let mut handler = Self {
                event_publisher,
                common_earcons_params: params,
                last_media_user_volume,
                volume_button_event: 0,
                priority_stream_playing: false,
                modified_timestamps: create_default_modified_timestamps(),
                switchboard_messenger: switchboard_messenger.clone(),
            };

            let mut listen_receptor = switchboard_messenger
                .message(
                    switchboard::Payload::Listen(switchboard::Listen::Request(SettingType::Audio)),
                    Audience::Address(switchboard::Address::Switchboard),
                )
                .send();
            loop {
                let mut listen_receptor_fuse = listen_receptor.next().fuse();
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
                    background_usage = usage_rx.next() => {
                        if let Some(Ok(request)) = background_usage {
                            handler.on_background_usage(request);
                        }

                    }
                }
            }
        });

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

        let new_media_user_volume: Option<f32> =
            match changed_streams.iter().find(|&&x| x.stream_type == AudioStreamType::Media) {
                Some(stream) => Some(stream.user_volume_level),
                None => None,
            };
        let volume_up_max_pressed =
            new_media_user_volume == Some(MAX_VOLUME) && self.volume_button_event == 1;
        let stream_is_media =
            changed_streams.iter().find(|&&x| x.stream_type == AudioStreamType::Media).is_some();

        if !stream_is_media {
            return;
        }

        // Logging for debugging volume changes.
        fx_log_debug!("[earcons_agent] Volume up pressed while max: {}", volume_up_max_pressed);
        fx_log_debug!(
            "[earcons_agent] New media user volume: {:?}, Last media user volume: {:?}",
            new_media_user_volume,
            self.last_media_user_volume
        );

        if (self.last_media_user_volume != new_media_user_volume) || volume_up_max_pressed {
            if self.last_media_user_volume != None {
                // On restore, the last media user volume is set for the first time, and registers
                // as different from the last seen volume, because it is initially None. Don't play
                // the earcons sound on that set.
                self.play_media_volume_sound(new_media_user_volume);
            }
            self.last_media_user_volume = new_media_user_volume;
        }
    }

    /// Invoked when the background usage changes, determining whether a
    /// priority stream is playing.
    fn on_background_usage(&mut self, usage_request: UsageWatcherRequest) {
        let OnStateChanged { state, responder, .. } = usage_request;
        if responder.send().is_err() {
            fx_log_err!("could not send response for background usage");
        }
        self.priority_stream_playing = match state {
            Muted(_) | Ducked(_) => true,
            _ => false,
        };
    }

    /// Play the earcons sound given the changed volume streams.
    ///
    /// The parameters are packaged together. See [VolumeChangeParams].
    fn play_media_volume_sound(&self, volume: Option<f32>) {
        let common_earcons_params = self.common_earcons_params.clone();
        let priority_stream_playing = self.priority_stream_playing;
        let publisher = self.event_publisher.clone();

        fasync::spawn(async move {
            // Connect to the SoundPlayer if not already connected.
            connect_to_sound_player(
                common_earcons_params.service_context.clone(),
                common_earcons_params.sound_player_connection.clone(),
            )
            .await;

            let sound_player_connection_clone =
                common_earcons_params.sound_player_connection.clone();
            let sound_player_connection = sound_player_connection_clone.lock().await;
            let sound_player_added_files = common_earcons_params.sound_player_added_files;

            if let (Some(sound_player_proxy), Some(volume_level)) =
                (sound_player_connection.as_ref(), volume)
            {
                if priority_stream_playing {
                    fx_log_debug!("Detected a stream already playing, not playing earcons sound");
                    publisher.send_event(Event::Earcon(earcon::Event::Suppressed(
                        earcon::EarconType::Volume,
                    )));
                    return;
                }

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
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::agent::base::Descriptor;
    use crate::audio::default_audio_info;
    use crate::internal::common::default_time;
    use crate::internal::event;
    use crate::message::base::MessengerType;
    use crate::service_context::ServiceContext;
    use crate::switchboard::base::{
        ListenCallback, ListenSession, SettingResponseResult, Switchboard,
    };
    use futures::lock::Mutex;
    use std::sync::Arc;

    struct FakeSwitchboard {}
    struct FakeListenSession {}

    impl ListenSession for FakeListenSession {
        fn close(&mut self) {}
    }

    impl Drop for FakeListenSession {
        fn drop(&mut self) {}
    }

    impl Switchboard for FakeSwitchboard {
        fn request(
            &mut self,
            _setting_type: SettingType,
            _request: SettingRequest,
            _callback: futures::channel::oneshot::Sender<SettingResponseResult>,
        ) -> Result<(), Error> {
            Ok(())
        }

        fn listen(
            &mut self,
            _setting_type: SettingType,
            _listener: ListenCallback,
        ) -> Result<Box<dyn ListenSession + Send + Sync>, Error> {
            Ok(Box::new(FakeListenSession {}))
        }
    }

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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_changed_streams() {
        let (fake_streams, old_timestamps, new_timestamps, expected_changed_streams) =
            fake_values();
        let switchboard_messenger_factory = switchboard::message::create_hub();
        let (messenger, _) =
            switchboard_messenger_factory.create(MessengerType::Unbound).await.unwrap();

        let mut handler = VolumeChangeHandler {
            switchboard_messenger: messenger,
            event_publisher: Publisher::create(
                &event::message::create_hub(),
                event::Address::Agent(Descriptor::Component("Test")),
            )
            .await,
            common_earcons_params: CommonEarconsParams {
                service_context: ServiceContext::create(None),
                sound_player_added_files: Arc::new(Mutex::new(HashSet::new())),
                sound_player_connection: Arc::new(Mutex::new(None)),
            },
            last_media_user_volume: Some(1.0),
            volume_button_event: 0,
            priority_stream_playing: false,
            modified_timestamps: old_timestamps,
        };
        let changed_streams = handler.calculate_changed_streams(fake_streams, new_timestamps);
        assert_eq!(changed_streams, expected_changed_streams);
    }
}
