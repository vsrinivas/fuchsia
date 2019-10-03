// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::switchboard::base::AudioStream,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_media::{AudioRenderUsage, Usage},
    fidl_fuchsia_media_audio::VolumeControlProxy,
    fuchsia_syslog::fx_log_err,
};

pub struct StreamVolumeControl {
    pub stored_stream: AudioStream,
    proxy: VolumeControlProxy,
}

// TODO(fxb/37777): Listen for volume changes from Volume Control.
impl StreamVolumeControl {
    pub fn create(audio_service: &fidl_fuchsia_media::AudioCoreProxy, stream: AudioStream) -> Self {
        let (vol_control_proxy, server_end) = create_proxy().unwrap();

        let mut usage = Usage::RenderUsage(AudioRenderUsage::from(stream.stream_type));

        audio_service
            .bind_usage_volume_control(&mut usage, server_end)
            .expect("bind usage volume control");
        StreamVolumeControl { stored_stream: stream, proxy: vol_control_proxy }
    }

    pub async fn set_volume(&mut self, stream: AudioStream) {
        assert_eq!(self.stored_stream.stream_type, stream.stream_type);

        if self.stored_stream.user_volume_level != stream.user_volume_level {
            self.proxy.set_volume(stream.user_volume_level).unwrap_or_else(move |e| {
                fx_log_err!("failed to set the volume level, {}", e);
            });
        }

        if self.stored_stream.user_volume_muted != stream.user_volume_muted {
            self.proxy.set_mute(stream.user_volume_muted).unwrap_or_else(move |e| {
                fx_log_err!("failed to mute the volume, {}", e);
            });
        }

        self.stored_stream = stream;
    }
}
