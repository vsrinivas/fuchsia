// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::mock_audio_core_service::audio_core_service_mock;
use crate::mock_sound_player_service::{sound_player_service_mock, SoundEventSender};
use anyhow::Error;
use fidl::endpoints::DiscoverableProtocolMarker;
use fidl_fuchsia_media::AudioCoreMarker;
use fidl_fuchsia_media_sounds::PlayerMarker;
use fidl_fuchsia_settings::{AudioMarker, AudioProxy};
use fuchsia_component_test::{Capability, LocalComponentHandles, Ref, Route};
use fuchsia_component_test::{ChildOptions, RealmBuilder, RealmInstance};
use futures::lock::Mutex;
use std::collections::HashMap;
use std::sync::Arc;
use utils;

pub(crate) const DEFAULT_VOLUME_LEVEL: f32 = 0.5;
pub(crate) const DEFAULT_VOLUME_MUTED: bool = false;

const COMPONENT_URL: &str = "#meta/setui_service.cm";

pub(crate) struct VolumeChangeEarconsTest {
    /// Represents the number of times the sound has been played in total. First u32 represents the
    /// sound id, and the second u32 is the number of times.
    play_counts: Arc<Mutex<HashMap<u32, u32>>>,

    /// The listeners to notify that a sound was played.
    sound_played_listeners: Arc<Mutex<Vec<SoundEventSender>>>,
}

impl VolumeChangeEarconsTest {
    pub(crate) fn create() -> Self {
        Self {
            play_counts: Arc::new(Mutex::new(HashMap::new())),
            sound_played_listeners: Arc::new(Mutex::new(Vec::new())),
        }
    }

    pub(crate) fn sound_played_listeners(&self) -> Arc<Mutex<Vec<SoundEventSender>>> {
        self.sound_played_listeners.clone()
    }

    pub(crate) async fn create_realm(&self) -> Result<RealmInstance, Error> {
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
        let audio_service = info
            .builder
            .add_local_child(
                "audio_service",
                move |handles: LocalComponentHandles| Box::pin(audio_core_service_mock(handles)),
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

        // Add mock sound player service.
        let play_counts = self.play_counts.clone();
        let sound_played_listeners = self.sound_played_listeners.clone();
        let sound_player = info
            .builder
            .add_local_child(
                "sound_player",
                move |handles: LocalComponentHandles| {
                    Box::pin(sound_player_service_mock(
                        handles,
                        play_counts.clone(),
                        sound_played_listeners.clone(),
                    ))
                },
                ChildOptions::new().eager(),
            )
            .await?;

        // Route the mock sound player service through the parent realm to setui_service.
        info.builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(PlayerMarker::PROTOCOL_NAME))
                    .from(&sound_player)
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
                    .to(&audio_service)
                    .to(&sound_player),
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
