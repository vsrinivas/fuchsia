// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::call,
    crate::handler::setting_handler::ControllerError,
    crate::internal::event::{Event, Publisher},
    crate::service_context::ExternalServiceProxy,
    crate::switchboard::base::{AudioStream, SettingType},
    fidl::{self, endpoints::create_proxy},
    fidl_fuchsia_media::{AudioRenderUsage, Usage},
    fidl_fuchsia_media_audio::VolumeControlProxy,
    fuchsia_async as fasync,
    futures::channel::mpsc::UnboundedSender,
    futures::stream::StreamExt,
    futures::TryStreamExt,
};

const PUBLISHER_EVENT_NAME: &str = "volume_control_events";
const CONTROLLER_ERROR_DEPENDENCY: &str = "fuchsia.media.audio";

// Stores an AudioStream and a VolumeControl proxy bound to the AudioCore
// service for |stored_stream|'s stream type. |proxy| is set to None if it
// fails to bind to the AudioCore service.
// TODO(fxb/57705): Replace UnboundedSender with a oneshot channel.
pub struct StreamVolumeControl {
    pub stored_stream: AudioStream,
    proxy: Option<VolumeControlProxy>,
    audio_service: ExternalServiceProxy<fidl_fuchsia_media::AudioCoreProxy>,
    publisher: Option<Publisher>,
    listen_exit_tx: Option<UnboundedSender<()>>,
}

impl Drop for StreamVolumeControl {
    fn drop(&mut self) {
        if let Some(exit_tx) = self.listen_exit_tx.take() {
            exit_tx.unbounded_send(()).ok();
        }
    }
}

// TODO(fxb/37777): Listen for volume changes from Volume Control.
impl StreamVolumeControl {
    pub async fn create(
        audio_service: &ExternalServiceProxy<fidl_fuchsia_media::AudioCoreProxy>,
        stream: AudioStream,
        publisher: Option<Publisher>,
    ) -> Result<Self, ControllerError> {
        let mut control = StreamVolumeControl {
            stored_stream: stream,
            proxy: None,
            audio_service: audio_service.clone(),
            publisher: publisher,
            listen_exit_tx: None,
        };

        control.bind_volume_control().await?;
        Ok(control)
    }

    pub async fn set_volume(&mut self, stream: AudioStream) -> Result<(), ControllerError> {
        assert_eq!(self.stored_stream.stream_type, stream.stream_type);

        // Try to create and bind a new VolumeControl.
        if self.proxy.is_none() {
            self.bind_volume_control().await?;
        }

        // Round to 1%.
        let mut new_stream_value = stream.clone();
        new_stream_value.user_volume_level = (stream.user_volume_level * 100.0).floor() / 100.0;

        let proxy = self.proxy.as_ref().unwrap();

        if self.stored_stream.user_volume_level != new_stream_value.user_volume_level {
            if proxy.set_volume(new_stream_value.user_volume_level).is_err() {
                self.stored_stream = new_stream_value;
                return Err(ControllerError::ExternalFailure(
                    SettingType::Audio,
                    CONTROLLER_ERROR_DEPENDENCY.into(),
                    "set volume".into(),
                ));
            }
        }

        if self.stored_stream.user_volume_muted != new_stream_value.user_volume_muted {
            if proxy.set_mute(stream.user_volume_muted).is_err() {
                self.stored_stream = new_stream_value;
                return Err(ControllerError::ExternalFailure(
                    SettingType::Audio,
                    CONTROLLER_ERROR_DEPENDENCY.into(),
                    "set mute".into(),
                ));
            }
        }

        self.stored_stream = new_stream_value;
        Ok(())
    }

    async fn bind_volume_control(&mut self) -> Result<(), ControllerError> {
        if self.proxy.is_some() {
            return Ok(());
        }

        let (vol_control_proxy, server_end) = create_proxy().unwrap();
        let stream_type = self.stored_stream.stream_type;
        let mut usage = Usage::RenderUsage(AudioRenderUsage::from(stream_type));

        if call!(self.audio_service => bind_usage_volume_control(&mut usage, server_end)).is_err() {
            return Err(ControllerError::ExternalFailure(
                SettingType::Audio,
                CONTROLLER_ERROR_DEPENDENCY.into(),
                format!("bind_usage_volume_control for audio_core {:?}", usage).into(),
            ));
        }

        // Once the volume control is bound, apply the persisted audio settings to it.
        if vol_control_proxy.set_volume(self.stored_stream.user_volume_level).is_err() {
            return Err(ControllerError::ExternalFailure(
                SettingType::Audio,
                CONTROLLER_ERROR_DEPENDENCY.into(),
                format!("set_volume for vol_control {:?}", stream_type).into(),
            ));
        }

        if vol_control_proxy.set_mute(self.stored_stream.user_volume_muted).is_err() {
            return Err(ControllerError::ExternalFailure(
                SettingType::Audio,
                CONTROLLER_ERROR_DEPENDENCY.into(),
                "set_mute for vol_control".into(),
            ));
        }

        if let Some(exit_tx) = self.listen_exit_tx.take() {
            exit_tx.unbounded_send(()).ok();
        }

        // TODO(fxb/37777): Update |stored_stream| in StreamVolumeControl and send a notification
        // when we receive an update.
        let (exit_tx, mut exit_rx) = futures::channel::mpsc::unbounded::<()>();
        let publisher_clone = self.publisher.as_ref().map_or(None, |p| Some(p.clone()));
        let mut volume_events = vol_control_proxy.take_event_stream();
        fasync::Task::spawn(async move {
            loop {
                futures::select! {
                    exit = exit_rx.next() => {
                        if let Some(publisher) = publisher_clone {
                            publisher.send_event(Event::Closed(PUBLISHER_EVENT_NAME));
                        }
                        return;
                    }
                    // TODO(fxb/58181): Handle the case where volume_events fails.
                    volume_event = volume_events.try_next() => {}

                }
            }
        })
        .detach();

        self.listen_exit_tx = Some(exit_tx);
        self.proxy = Some(vol_control_proxy);
        Ok(())
    }
}
