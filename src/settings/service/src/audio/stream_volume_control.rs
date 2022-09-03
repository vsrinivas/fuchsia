// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::types::AudioStream;
use crate::audio::utils::round_volume_level;
use crate::base::SettingType;
use crate::clock;
use crate::event::{Event, Publisher};
use crate::handler::setting_handler::ControllerError;
use crate::service_context::{ExternalServiceEvent, ExternalServiceProxy};
use crate::{call, trace, trace_guard};
use fidl::{self, endpoints::create_proxy};
use fidl_fuchsia_media::{AudioRenderUsage, Usage};
use fidl_fuchsia_media_audio::VolumeControlProxy;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_warn;
use fuchsia_trace as ftrace;
use futures::channel::mpsc::UnboundedSender;
use futures::stream::StreamExt;
use futures::TryStreamExt;
use std::sync::Arc;

const PUBLISHER_EVENT_NAME: &str = "volume_control_events";
const CONTROLLER_ERROR_DEPENDENCY: &str = "fuchsia.media.audio";
const UNKNOWN_INSPECT_STRING: &str = "unknown";

/// Closure definition for an action that can be triggered by ActionFuse.
pub(crate) type ExitAction = Arc<dyn Fn() + Send + Sync + 'static>;

// Stores an AudioStream and a VolumeControl proxy bound to the AudioCore
// service for |stored_stream|'s stream type. |proxy| is set to None if it
// fails to bind to the AudioCore service. |early_exit_action| specifies a
// closure to be run if the StreamVolumeControl exits prematurely.
// TODO(fxbug.dev/57705): Replace UnboundedSender with a oneshot channel.
pub struct StreamVolumeControl {
    pub stored_stream: AudioStream,
    proxy: Option<VolumeControlProxy>,
    audio_service: ExternalServiceProxy<fidl_fuchsia_media::AudioCoreProxy>,
    publisher: Option<Publisher>,
    early_exit_action: Option<ExitAction>,
    listen_exit_tx: Option<UnboundedSender<()>>,
}

impl Drop for StreamVolumeControl {
    fn drop(&mut self) {
        if let Some(exit_tx) = self.listen_exit_tx.take() {
            // Do not signal exit if receiver is already closed.
            if exit_tx.is_closed() {
                return;
            }

            // Consider panic! is likely to be abort in the drop method, only log info for
            // unbounded_send failure.
            exit_tx.unbounded_send(()).unwrap_or_else(|_| {
                fx_log_warn!("StreamVolumeControl::drop, exit_tx failed to send exit signal")
            });
        }
    }
}

impl StreamVolumeControl {
    pub(crate) async fn create(
        id: ftrace::Id,
        audio_service: &ExternalServiceProxy<fidl_fuchsia_media::AudioCoreProxy>,
        stream: AudioStream,
        early_exit_action: Option<ExitAction>,
        publisher: Option<Publisher>,
    ) -> Result<Self, ControllerError> {
        // Stream input should be valid. Input comes from restore should be valid
        // and from set request has the validation.
        assert!(stream.has_finite_volume_level());

        trace!(id, "StreamVolumeControl ctor");
        let mut control = StreamVolumeControl {
            stored_stream: stream,
            proxy: None,
            audio_service: audio_service.clone(),
            publisher,
            listen_exit_tx: None,
            early_exit_action,
        };

        control.bind_volume_control(id).await?;
        Ok(control)
    }

    pub(crate) async fn set_volume(
        &mut self,
        id: ftrace::Id,
        stream: AudioStream,
    ) -> Result<(), ControllerError> {
        assert_eq!(self.stored_stream.stream_type, stream.stream_type);
        // Stream input should be valid. Input comes from restore should be valid
        // and from set request has the validation.
        assert!(stream.has_finite_volume_level());

        // Try to create and bind a new VolumeControl.
        if self.proxy.is_none() {
            self.bind_volume_control(id).await?;
        }

        // Round volume level from user input.
        let mut new_stream_value = stream;
        new_stream_value.user_volume_level = round_volume_level(stream.user_volume_level);

        let proxy = self.proxy.as_ref().expect("no volume control proxy");

        if (self.stored_stream.user_volume_level - new_stream_value.user_volume_level).abs()
            > f32::EPSILON
        {
            if let Err(e) = proxy.set_volume(new_stream_value.user_volume_level) {
                self.stored_stream = new_stream_value;
                return Err(ControllerError::ExternalFailure(
                    SettingType::Audio,
                    CONTROLLER_ERROR_DEPENDENCY.into(),
                    "set volume".into(),
                    format!("{e:?}").into(),
                ));
            }
        }

        if self.stored_stream.user_volume_muted != new_stream_value.user_volume_muted {
            if let Err(e) = proxy.set_mute(stream.user_volume_muted) {
                self.stored_stream = new_stream_value;
                return Err(ControllerError::ExternalFailure(
                    SettingType::Audio,
                    CONTROLLER_ERROR_DEPENDENCY.into(),
                    "set mute".into(),
                    format!("{e:?}").into(),
                ));
            }
        }

        self.stored_stream = new_stream_value;
        Ok(())
    }

    async fn bind_volume_control(&mut self, id: ftrace::Id) -> Result<(), ControllerError> {
        trace!(id, "bind volume control");
        if self.proxy.is_some() {
            return Ok(());
        }

        let (vol_control_proxy, server_end) = create_proxy().map_err(|err| {
            ControllerError::UnexpectedError(
                format!("failed to create proxy for volume control: {:?}", err).into(),
            )
        })?;
        let stream_type = self.stored_stream.stream_type;
        let mut usage = Usage::RenderUsage(AudioRenderUsage::from(stream_type));

        let guard = trace_guard!(id, "bind usage volume control");
        if let Err(e) =
            call!(self.audio_service => bind_usage_volume_control(&mut usage, server_end))
        {
            return Err(ControllerError::ExternalFailure(
                SettingType::Audio,
                CONTROLLER_ERROR_DEPENDENCY.into(),
                format!("bind_usage_volume_control for audio_core {:?}", usage).into(),
                format!("{e:?}").into(),
            ));
        }
        drop(guard);

        let guard = trace_guard!(id, "set values");
        // Once the volume control is bound, apply the persisted audio settings to it.
        if let Err(e) = vol_control_proxy.set_volume(self.stored_stream.user_volume_level) {
            return Err(ControllerError::ExternalFailure(
                SettingType::Audio,
                CONTROLLER_ERROR_DEPENDENCY.into(),
                format!("set_volume for vol_control {:?}", stream_type).into(),
                format!("{e:?}").into(),
            ));
        }

        if let Err(e) = vol_control_proxy.set_mute(self.stored_stream.user_volume_muted) {
            return Err(ControllerError::ExternalFailure(
                SettingType::Audio,
                CONTROLLER_ERROR_DEPENDENCY.into(),
                "set_mute for vol_control".into(),
                format!("{e:?}").into(),
            ));
        }
        drop(guard);

        if let Some(exit_tx) = self.listen_exit_tx.take() {
            // exit_rx needs this signal to end leftover spawn.
            exit_tx.unbounded_send(()).expect(
                "StreamVolumeControl::bind_volume_control, listen_exit_tx failed to send exit \
                signal",
            );
        }

        trace!(id, "setup listener");

        let (exit_tx, mut exit_rx) = futures::channel::mpsc::unbounded::<()>();
        let publisher_clone = self.publisher.clone();
        let mut volume_events = vol_control_proxy.take_event_stream();
        let early_exit_action = self.early_exit_action.clone();
        fasync::Task::spawn(async move {
            let id = ftrace::Id::new();
            trace!(id, "bind volume handler");
            loop {
                futures::select! {
                    _ = exit_rx.next() => {
                        trace!(id, "exit");
                        if let Some(publisher) = publisher_clone {
                            // Send UNKNOWN_INSPECT_STRING for request-related args because it
                            // can't be tied back to the event that caused the proxy to close.
                            publisher.send_event(
                                Event::ExternalServiceEvent(
                                    ExternalServiceEvent::Closed(
                                        PUBLISHER_EVENT_NAME,
                                        UNKNOWN_INSPECT_STRING.into(),
                                        UNKNOWN_INSPECT_STRING.into(),
                                        clock::inspect_format_now().into(),
                                    )
                                )
                            );
                        }
                        return;
                    }
                    volume_event = volume_events.try_next() => {
                        trace!(id, "volume_event");
                        if volume_event.is_err() ||
                            volume_event.expect("should not be error").is_none()
                        {
                            if let Some(action) = early_exit_action {
                                (action)();
                            }
                            return;
                        }
                    }

                }
            }
        })
        .detach();

        self.listen_exit_tx = Some(exit_tx);
        self.proxy = Some(vol_control_proxy);
        Ok(())
    }
}
