// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::mock_audio_core_service::audio_core_service_mock;
use anyhow::Error;
use fidl::endpoints::DiscoverableProtocolMarker;
use fidl_fuchsia_media::{AudioCoreMarker, AudioRenderUsage};
use fidl_fuchsia_settings::{
    AudioMarker, AudioProxy, AudioStreamSettingSource, AudioStreamSettings, Volume,
};
use fuchsia_component_test::{Capability, LocalComponentHandles, Ref, Route};
use fuchsia_component_test::{ChildOptions, RealmBuilder, RealmInstance};
use futures::channel::mpsc::Sender;
use parking_lot::RwLock;
use std::collections::HashMap;
use std::sync::Arc;
use utils;

pub(crate) const DEFAULT_VOLUME_LEVEL: f32 = 0.5;
pub(crate) const DEFAULT_VOLUME_MUTED: bool = false;

pub(crate) const DEFAULT_MEDIA_STREAM_SETTINGS: AudioStreamSettings =
    create_default_audio_stream(AudioRenderUsage::Media);

const DEFAULT_STREAMS: [AudioStreamSettings; 5] = [
    create_default_audio_stream(AudioRenderUsage::Background),
    create_default_audio_stream(AudioRenderUsage::Media),
    create_default_audio_stream(AudioRenderUsage::Interruption),
    create_default_audio_stream(AudioRenderUsage::SystemAgent),
    create_default_audio_stream(AudioRenderUsage::Communication),
];

const fn create_default_audio_stream(usage: AudioRenderUsage) -> AudioStreamSettings {
    AudioStreamSettings {
        stream: Some(usage),
        source: Some(AudioStreamSettingSource::User),
        user_volume: Some(Volume {
            level: Some(DEFAULT_VOLUME_LEVEL),
            muted: Some(DEFAULT_VOLUME_MUTED),
            ..Volume::EMPTY
        }),
        ..AudioStreamSettings::EMPTY
    }
}

/// Info about an incoming request emitted by the audio core mock whenever it receives a request.
#[derive(PartialEq, Debug)]
pub(crate) enum AudioCoreRequest {
    SetVolume(AudioRenderUsage, f32),
    SetMute(AudioRenderUsage, bool),
}

const COMPONENT_URL: &str = "#meta/setui_service.cm";

pub(crate) struct AudioTest {
    /// Audio streams shared by the mock audio core service.
    audio_streams: Arc<RwLock<HashMap<AudioRenderUsage, (f32, bool)>>>,
}

impl AudioTest {
    pub(crate) fn create() -> Self {
        let mut streams = HashMap::<AudioRenderUsage, (f32, bool)>::new();
        for stream in DEFAULT_STREAMS {
            let _ = streams.insert(
                stream.stream.expect("stream usage specified"),
                (
                    stream
                        .user_volume
                        .as_ref()
                        .expect("user volume specified")
                        .level
                        .expect("stream level specified"),
                    stream
                        .user_volume
                        .as_ref()
                        .expect("user volume specified")
                        .muted
                        .expect("stream level specified"),
                ),
            );
        }
        Self { audio_streams: Arc::new(RwLock::new(streams)) }
    }

    /// Creates a test realm.
    ///
    /// `audio_core_request_sender` is used to report when requests are processed by the audio core
    /// mock.
    ///
    /// `usages_to_report` is a list of usages to report requests for. Any usages not in this list
    /// will not have `AudioCoreRequest` sent when processed.
    pub(crate) async fn create_realm(
        &self,
        audio_core_request_sender: Sender<AudioCoreRequest>,
        usages_to_report: Vec<AudioRenderUsage>,
    ) -> Result<RealmInstance, Error> {
        let builder = RealmBuilder::new().await?;
        // Add setui_service as child of the realm builder.
        let setui_service =
            builder.add_child("setui_service", COMPONENT_URL, ChildOptions::new()).await?;
        let info = utils::SettingsRealmInfo {
            builder,
            settings: &setui_service,
            has_config_data: true,
            capabilities: vec![AudioMarker::PROTOCOL_NAME],
        };
        // Add basic Settings service realm information.
        utils::create_realm_basic(&info).await?;

        // Add mock audio core service.
        let audio_streams = self.audio_streams.clone();
        let audio_service = info
            .builder
            .add_local_child(
                "audio_service",
                move |handles: LocalComponentHandles| {
                    Box::pin(audio_core_service_mock(
                        handles,
                        audio_streams.clone(),
                        audio_core_request_sender.clone(),
                        usages_to_report.clone(),
                    ))
                },
                ChildOptions::new().eager(),
            )
            .await?;

        // Route the mock audio core service through the parent realm to setui_service.
        info.builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(AudioCoreMarker::PROTOCOL_NAME))
                    .from(&audio_service)
                    .to(Ref::parent())
                    .to(&setui_service),
            )
            .await?;

        // Provide LogSink to print out logs of the audio core component for debugging purposes.
        info.builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&audio_service),
            )
            .await?;

        let instance = info.builder.build().await?;
        Ok(instance)
    }

    pub(crate) fn connect_to_audio_marker(instance: &RealmInstance) -> AudioProxy {
        return instance
            .root
            .connect_to_protocol_at_exposed_dir::<AudioMarker>()
            .expect("connecting to Audio");
    }
}
