// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::switchboard::base::{AudioStream, AudioStreamType},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_media::{AudioRenderUsage, Usage},
    fidl_fuchsia_media_audio::{VolumeControlEvent, VolumeControlProxy},
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::TryStreamExt,
};

// Stores an AudioStream and a VolumeControl proxy bound to the AudioCore
// service for |stored_stream|'s stream type. |proxy| is set to None if it
// fails to bind to the AudioCore service.
pub struct StreamVolumeControl {
    pub stored_stream: AudioStream,
    proxy: Option<VolumeControlProxy>,
    audio_service: fidl_fuchsia_media::AudioCoreProxy,
}

// TODO(fxb/37777): Listen for volume changes from Volume Control.
impl StreamVolumeControl {
    pub fn create(audio_service: &fidl_fuchsia_media::AudioCoreProxy, stream: AudioStream) -> Self {
        StreamVolumeControl {
            stored_stream: stream,
            proxy: bind_volume_control(&audio_service, stream.stream_type),
            audio_service: audio_service.clone(),
        }
    }

    pub async fn set_volume(&mut self, stream: AudioStream) {
        assert_eq!(self.stored_stream.stream_type, stream.stream_type);

        // If |proxy| is set to None, then try to create and bind a new VolumeControl. If it
        // fails, log an error and don't set the volume.
        if self.proxy.is_none() {
            self.proxy = bind_volume_control(&self.audio_service, stream.stream_type);
            if self.proxy.is_none() {
                fx_log_err!("failed to bind volume control");
                return;
            }
        }

        let proxy = self.proxy.as_ref().unwrap();

        if self.stored_stream.user_volume_level != stream.user_volume_level {
            proxy.set_volume(stream.user_volume_level).unwrap_or_else(move |e| {
                fx_log_err!("failed to set the volume level, {}", e);
            });
        }

        if self.stored_stream.user_volume_muted != stream.user_volume_muted {
            proxy.set_mute(stream.user_volume_muted).unwrap_or_else(move |e| {
                fx_log_err!("failed to mute the volume, {}", e);
            });
        }

        self.stored_stream = stream;
    }
}

fn bind_volume_control(
    audio_service: &fidl_fuchsia_media::AudioCoreProxy,
    stream_type: AudioStreamType,
) -> Option<VolumeControlProxy> {
    let (vol_control_proxy, server_end) = create_proxy().unwrap();
    let mut usage = Usage::RenderUsage(AudioRenderUsage::from(stream_type));

    if let Err(err) = audio_service.bind_usage_volume_control(&mut usage, server_end) {
        fx_log_err!("failed to bind volume control for usage, {}", err);
        return None;
    }

    // TODO(fxb/37777): Update |stored_stream| in StreamVolumeControl and send a notification
    // when we receive an update.
    let proxy_clone = vol_control_proxy.clone();
    fasync::spawn(async move {
        let mut stream = proxy_clone.take_event_stream();
        while let Some(evt) = stream.try_next().await.unwrap() {
            match evt {
                VolumeControlEvent::OnVolumeMuteChanged { new_volume: _, new_muted: _ } => {
                    proxy_clone.notify_volume_mute_changed_handled().unwrap_or_else(move |e| {
                        fx_log_err!("failed to notify volume mute change handling, {}", e);
                    });
                }
            }
        }
    });

    Some(vol_control_proxy)
}
