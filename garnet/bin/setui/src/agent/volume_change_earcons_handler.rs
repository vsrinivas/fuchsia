// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::earcons_agent::CommonEarconsParams;
use crate::audio::play_sound;
use crate::input::monitor_media_buttons;
use crate::switchboard::base::{
    AudioStreamType, SettingRequest, SettingResponse, SettingResponseResult, SettingType,
    SwitchboardHandle,
};

use fidl_fuchsia_ui_input::MediaButtonsEvent;
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_info};
use futures::lock::Mutex;
use futures::StreamExt;
use std::sync::{atomic::Ordering, Arc};

/// The VolumeChangeEarconsHandler takes care of the earcons functionality on volume change.
#[derive(Debug, Clone)]
pub struct VolumeChangeEarconsHandler {
    common_earcons_params: CommonEarconsParams,
    last_media_user_volume: Arc<Mutex<Option<f32>>>,
    volume_button_event: Arc<Mutex<i8>>,
}

/// The maximum volume level.
const MAX_VOLUME: f32 = 1.0;

/// The file path for the earcon to be played for max sound level.
const VOLUME_MAX_FILE_PATH: &str = "volume-max.wav";

/// The file path for the earcon to be played for volume changes below max volume level.
const VOLUME_CHANGED_FILE_PATH: &str = "volume-changed.wav";

impl VolumeChangeEarconsHandler {
    pub fn new(common_earcons_params: CommonEarconsParams) -> VolumeChangeEarconsHandler {
        Self {
            common_earcons_params,
            last_media_user_volume: Arc::new(Mutex::new(Some(1.0))),
            volume_button_event: Arc::<Mutex<i8>>::new(Mutex::new(0)),
        }
    }

    pub fn watch_last_volume_button_event(&self) {
        let volume_button_event_clone = self.volume_button_event.clone();
        let service_context_clone = self.common_earcons_params.service_context.clone();
        let (input_tx, mut input_rx) = futures::channel::mpsc::unbounded::<MediaButtonsEvent>();
        let input_tx_clone = input_tx.clone();

        fasync::spawn(async move {
            match monitor_media_buttons(service_context_clone, input_tx_clone).await {
                Ok(_) => {}
                Err(err) => {
                    fx_log_err!("Failed to monitor media buttons for volume changes: {}", err)
                }
            };
        });

        fasync::spawn(async move {
            while let Some(event) = input_rx.next().await {
                if let Some(volume) = event.volume {
                    *volume_button_event_clone.lock().await = volume;
                }
            }
        });
    }

    pub fn get_volume_info(&self, switchboard: SwitchboardHandle, setting_type: SettingType) {
        get_volume_info(
            switchboard,
            setting_type,
            self.last_media_user_volume.clone(),
            self.volume_button_event.clone(),
            self.common_earcons_params.clone(),
        );
    }
}

/// Send a request to the switchboard to get the audio info for the volume change.
///
/// Intended to be sent after getting a response from the listen command on the switchboard.
pub fn get_volume_info(
    switchboard: SwitchboardHandle,
    setting_type: SettingType,
    last_media_user_volume: Arc<Mutex<Option<f32>>>,
    volume_button_event: Arc<Mutex<i8>>,
    common_earcons_params: CommonEarconsParams,
) {
    let (response_tx, response_rx) = futures::channel::oneshot::channel::<SettingResponseResult>();
    let switchboard_clone = switchboard.clone();
    fasync::spawn(async move {
        match switchboard_clone.lock().await.request(setting_type, SettingRequest::Get, response_tx)
        {
            Ok(_) => {
                match response_rx.await {
                    Ok(Ok(response)) => {
                        handle_volume_request(
                            response,
                            last_media_user_volume,
                            volume_button_event,
                            common_earcons_params,
                        )
                        .await;
                    }
                    _ => {
                        fx_log_err!("[earcons_agent] Failed to extract volume state response from switchboard");
                    }
                }
            }
            Err(err) => {
                fx_log_err!(
                    "[earcons_agent] Failed to get volume state from switchboard: {:?}",
                    err
                );
            }
        }
    });
}

/// Handle the volume state acquired from the switchboard, and determine whether
/// the sounds should be played.
async fn handle_volume_request(
    audio: Option<SettingResponse>,
    last_media_user_volume: Arc<Mutex<Option<f32>>>,
    volume_button_event: Arc<Mutex<i8>>,
    common_earcons_params: CommonEarconsParams,
) {
    if let Some(SettingResponse::Audio(audio_info)) = audio {
        let changed_streams = match audio_info.changed_streams {
            Some(streams) => streams,
            None => Vec::new(),
        };
        fx_log_info!("[earcons_agent] changed streams: {:?}", changed_streams);

        let new_media_user_volume: Option<f32> =
            match changed_streams.iter().find(|&&x| x.stream_type == AudioStreamType::Media) {
                Some(stream) => Some(stream.user_volume_level),
                None => None,
            };
        let volume_up_max_pressed =
            new_media_user_volume == Some(MAX_VOLUME) && *volume_button_event.lock().await == 1;
        let stream_is_media =
            changed_streams.iter().find(|&&x| x.stream_type == AudioStreamType::Media).is_some();
        let mut last_media_user_volume_lock = last_media_user_volume.lock().await;

        // Logging for debugging volume changes
        fx_log_info!("[earcons_agent] Is media stream: {}", stream_is_media);
        fx_log_info!("[earcons_agent] Volume up pressed while max: {}", volume_up_max_pressed);
        fx_log_info!("[earcons_agent] New media user volume: {:?}", new_media_user_volume);
        fx_log_info!("[earcons_agent] Last media user volume: {:?}", *last_media_user_volume_lock);

        if ((*last_media_user_volume_lock != new_media_user_volume) || volume_up_max_pressed)
            && stream_is_media
        {
            if *last_media_user_volume_lock != None {
                // On restore, the last media user volume is set for the first time, and registers
                // as different from the last seen volume, because it is initially None. Don't play
                // the earcons sound on that set.
                fasync::spawn(async move {
                    play_media_volume_sound(new_media_user_volume, common_earcons_params).await;
                });
            }
            *last_media_user_volume_lock = new_media_user_volume;
        }
    }
}

/// Play the earcons sound given the changed volume streams.
///
/// The parameters are packaged together. See [VolumeChangeParams].
async fn play_media_volume_sound(volume: Option<f32>, common_earcons_params: CommonEarconsParams) {
    let sound_player_connection_clone = common_earcons_params.sound_player_connection.clone();
    let sound_player_connection = sound_player_connection_clone.lock().await;
    let priority_stream_playing = common_earcons_params.priority_stream_playing;
    let sound_player_added_files = common_earcons_params.sound_player_added_files;

    if let (Some(sound_player_proxy), Some(volume_level)) =
        (sound_player_connection.as_ref(), volume)
    {
        if priority_stream_playing.load(Ordering::SeqCst) {
            fx_log_info!("Detected a stream already playing, not playing earcons sound");
            return;
        }

        if volume_level >= 1.0 {
            play_sound(
                &sound_player_proxy,
                VOLUME_MAX_FILE_PATH,
                0,
                sound_player_added_files.clone(),
            )
            .await
            .ok();
        } else if volume_level > 0.0 {
            play_sound(
                &sound_player_proxy,
                VOLUME_CHANGED_FILE_PATH,
                1,
                sound_player_added_files.clone(),
            )
            .await
            .ok();
        }
    }
}
