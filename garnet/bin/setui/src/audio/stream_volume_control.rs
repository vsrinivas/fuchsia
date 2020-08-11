// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::call,
    crate::registry::setting_handler::ControllerError,
    crate::service_context::ExternalServiceProxy,
    crate::switchboard::base::{AudioStream, AudioStreamType, SettingType},
    fidl::{self, endpoints::create_proxy},
    fidl_fuchsia_media::{AudioRenderUsage, Usage},
    fidl_fuchsia_media_audio::VolumeControlProxy,
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::{FutureExt, TryFutureExt, TryStreamExt},
};

const CONTROLLER_ERROR_DEPENDENCY: &str = "fuchsia.media.audio";

// Stores an AudioStream and a VolumeControl proxy bound to the AudioCore
// service for |stored_stream|'s stream type. |proxy| is set to None if it
// fails to bind to the AudioCore service.
pub struct StreamVolumeControl {
    pub stored_stream: AudioStream,
    proxy: Option<VolumeControlProxy>,
    audio_service: ExternalServiceProxy<fidl_fuchsia_media::AudioCoreProxy>,
}

// TODO(fxb/37777): Listen for volume changes from Volume Control.
impl StreamVolumeControl {
    pub fn create(
        audio_service: &ExternalServiceProxy<fidl_fuchsia_media::AudioCoreProxy>,
        stream: AudioStream,
    ) -> Result<Self, ControllerError> {
        Ok(StreamVolumeControl {
            stored_stream: stream,
            proxy: Some(bind_volume_control(&audio_service, stream.stream_type, stream)?),
            audio_service: audio_service.clone(),
        })
    }

    pub async fn set_volume(&mut self, stream: AudioStream) -> Result<(), ControllerError> {
        assert_eq!(self.stored_stream.stream_type, stream.stream_type);

        // Try to create and bind a new VolumeControl.
        if self.proxy.is_none() {
            self.proxy = Some(bind_volume_control(
                &self.audio_service,
                stream.stream_type,
                self.stored_stream,
            )?);
        }

        // Round to 1%.
        let mut new_stream_value = stream.clone();
        new_stream_value.user_volume_level = (stream.user_volume_level * 100.0).floor() / 100.0;
        self.stored_stream = new_stream_value;

        let proxy = self.proxy.as_ref().unwrap();
        if self.stored_stream.user_volume_level != new_stream_value.user_volume_level {
            if proxy.set_volume(new_stream_value.user_volume_level).is_err() {
                return Err(ControllerError::ExternalFailure(
                    SettingType::Audio,
                    CONTROLLER_ERROR_DEPENDENCY.into(),
                    "set volume".into(),
                ));
            }
        }

        if self.stored_stream.user_volume_muted != new_stream_value.user_volume_muted {
            if proxy.set_mute(stream.user_volume_muted).is_err() {
                return Err(ControllerError::ExternalFailure(
                    SettingType::Audio,
                    CONTROLLER_ERROR_DEPENDENCY.into(),
                    "set mute".into(),
                ));
            }
        }

        Ok(())
    }
}

fn bind_volume_control(
    audio_service: &ExternalServiceProxy<fidl_fuchsia_media::AudioCoreProxy>,
    stream_type: AudioStreamType,
    stored_stream: AudioStream,
) -> Result<VolumeControlProxy, ControllerError> {
    let (vol_control_proxy, server_end) = create_proxy().unwrap();
    let mut usage = Usage::RenderUsage(AudioRenderUsage::from(stream_type));

    if call!(audio_service => bind_usage_volume_control(&mut usage, server_end)).is_err() {
        return Err(ControllerError::ExternalFailure(
            SettingType::Audio,
            CONTROLLER_ERROR_DEPENDENCY.into(),
            format!("bind_usage_volume_control for audio_core {:?}", usage).into(),
        ));
    }

    // Once the volume control is bound, apply the persisted audio settings to it.
    if vol_control_proxy.set_volume(stored_stream.user_volume_level).is_err() {
        return Err(ControllerError::ExternalFailure(
            SettingType::Audio,
            CONTROLLER_ERROR_DEPENDENCY.into(),
            format!("set_volume for vol_control {:?}", stream_type).into(),
        ));
    }

    if vol_control_proxy.set_mute(stored_stream.user_volume_muted).is_err() {
        return Err(ControllerError::ExternalFailure(
            SettingType::Audio,
            CONTROLLER_ERROR_DEPENDENCY.into(),
            "set_mute for vol_control".into(),
        ));
    }

    // TODO(fxb/37777): Update |stored_stream| in StreamVolumeControl and send a notification
    // when we receive an update.
    let proxy_clone = vol_control_proxy.clone();
    let consume_volume_events = async move {
        let mut volume_events = proxy_clone.take_event_stream();
        while let Some(_) = volume_events.try_next().await? {}

        Ok(())
    };
    fasync::Task::spawn(
        consume_volume_events
            .map_err(|e: fidl::Error| fx_log_err!("Volume event stream failed: {}", e))
            .map(|_| ()),
    )
    .detach();

    Ok(vol_control_proxy)
}
