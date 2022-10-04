// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::mock_audio_core_service::audio_core_service_mock;
use crate::mock_discovery::{discovery_service_mock, remove_session, update_session};
use crate::mock_input_device_registry::{
    input_device_registry_mock, ButtonEventReceiver, ButtonEventSender,
};
use crate::mock_sound_player_service::{
    sound_player_service_mock, SoundEventReceiver, SoundEventSender,
};
use anyhow::Error;
use fidl::endpoints::DiscoverableProtocolMarker;
use fidl_fuchsia_media::AudioCoreMarker;
use fidl_fuchsia_media::AudioRenderUsage;
use fidl_fuchsia_media_sessions2::{DiscoveryMarker, SessionsWatcherProxy};
use fidl_fuchsia_media_sounds::PlayerMarker;
use fidl_fuchsia_settings::{AudioMarker, AudioProxy, AudioSettings, AudioStreamSettings};
use fidl_fuchsia_ui_policy::DeviceListenerRegistryMarker;
use fuchsia_component_test::{Capability, LocalComponentHandles, Ref, Route};
use fuchsia_component_test::{ChildOptions, RealmBuilder, RealmInstance};
use futures::channel::mpsc::Sender;
use futures::lock::Mutex;
use futures::StreamExt;
use std::collections::HashMap;
use std::sync::Arc;
use utils;

pub use crate::mock_discovery::SessionId;

mod mock_audio_core_service;
mod mock_discovery;
mod mock_input_device_registry;
mod mock_sound_player_service;

pub const DEFAULT_VOLUME_LEVEL: f32 = 0.5;
pub const DEFAULT_VOLUME_MUTED: bool = false;

const COMPONENT_URL: &str = "#meta/setui_service.cm";

pub struct VolumeChangeEarconsTest {
    /// Represents the number of times the sound has been played in total. First u32 represents the
    /// sound id, and the second u32 is the number of times.
    play_counts: Arc<Mutex<HashMap<u32, u32>>>,

    /// Watchers on the discovery service that have called WatchSession.
    discovery_watchers: Arc<Mutex<Vec<SessionsWatcherProxy>>>,

    /// The listeners to notify that a sound was played.
    sound_played_listeners: Arc<Mutex<Vec<SoundEventSender>>>,

    /// The listeners to notify that a media button was clicked.
    media_button_listener: Option<Arc<Mutex<ButtonEventSender>>>,
}

impl VolumeChangeEarconsTest {
    pub fn create() -> Self {
        Self {
            play_counts: Arc::new(Mutex::new(HashMap::new())),
            discovery_watchers: Arc::new(Mutex::new(Vec::new())),
            sound_played_listeners: Arc::new(Mutex::new(Vec::new())),
            media_button_listener: None,
        }
    }

    pub fn play_counts(&self) -> Arc<Mutex<HashMap<u32, u32>>> {
        Arc::clone(&self.play_counts)
    }

    pub fn discovery_watchers(&self) -> Arc<Mutex<Vec<SessionsWatcherProxy>>> {
        Arc::clone(&self.discovery_watchers)
    }

    pub fn sound_played_listeners(&self) -> Arc<Mutex<Vec<SoundEventSender>>> {
        Arc::clone(&self.sound_played_listeners)
    }

    pub fn media_button_listener(&self) -> Option<Arc<Mutex<ButtonEventSender>>> {
        self.media_button_listener.clone()
    }

    /// Simulates updating a media session.
    pub async fn update_session(&self, id: SessionId, domain: &str) {
        update_session(self.discovery_watchers(), id, domain).await;
    }

    /// Simulates removing a media session.
    pub async fn remove_session(&self, id: SessionId) {
        remove_session(self.discovery_watchers(), id).await;
    }

    async fn add_common_basic(
        &self,
        info: &utils::SettingsRealmInfo<'_>,
        usage_control_bound_sender: Sender<()>,
        discovery_watcher_notifier: Option<Sender<()>>,
    ) -> Result<(), Error> {
        // Add basic Settings service realm information.
        utils::create_realm_basic(&info).await?;

        // Add mock audio core service.
        let audio_service = info
            .builder
            .add_local_child(
                "audio_service",
                move |handles: LocalComponentHandles| {
                    Box::pin(audio_core_service_mock(handles, usage_control_bound_sender.clone()))
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
                    .to(info.settings),
            )
            .await?;

        // Add mock sound player service.
        let play_counts = Arc::clone(&self.play_counts);
        let sound_played_listeners = Arc::clone(&self.sound_played_listeners);
        let sound_player = info
            .builder
            .add_local_child(
                "sound_player",
                move |handles: LocalComponentHandles| {
                    Box::pin(sound_player_service_mock(
                        handles,
                        Arc::clone(&play_counts),
                        Arc::clone(&sound_played_listeners),
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
                    .to(info.settings),
            )
            .await?;

        // Add mock discovery service.
        let discovery_watchers = self.discovery_watchers();
        let discovery_service = info
            .builder
            .add_local_child(
                "discovery_service",
                move |handles: LocalComponentHandles| {
                    let discovery_watchers = Arc::clone(&discovery_watchers);
                    let discovery_watcher_notifier = discovery_watcher_notifier.clone();
                    Box::pin(discovery_service_mock(
                        handles,
                        discovery_watchers,
                        discovery_watcher_notifier,
                    ))
                },
                ChildOptions::new().eager(),
            )
            .await?;

        // Route the mock discovery service through the parent realm to setui_service.
        info.builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(DiscoveryMarker::PROTOCOL_NAME))
                    .from(&discovery_service)
                    .to(Ref::parent())
                    .to(info.settings),
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

        Ok(())
    }

    pub async fn create_realm_without_input_device_registry(
        &self,
        usage_control_bound_sender: Sender<()>,
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

        self.add_common_basic(&info, usage_control_bound_sender, None).await?;

        let instance = info.builder.build().await?;
        Ok(instance)
    }

    pub async fn create_realm_with_real_discovery(
        &self,
        usage_control_bound_sender: Sender<()>,
        discovery_watcher_notifier: Sender<()>,
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

        self.add_common_basic(&info, usage_control_bound_sender, Some(discovery_watcher_notifier))
            .await?;

        let instance = info.builder.build().await?;
        Ok(instance)
    }

    pub async fn create_realm_with_input_device_registry(
        &mut self,
        media_button_sender: ButtonEventSender,
        usage_control_bound_sender: Sender<()>,
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

        self.add_common_basic(&info, usage_control_bound_sender, None).await?;

        self.media_button_listener = Some(Arc::new(Mutex::new(media_button_sender)));
        let media_button_listener = self.media_button_listener.clone().unwrap();
        let input_device_registry = info
            .builder
            .add_local_child(
                "input_device_registry",
                move |handles: LocalComponentHandles| {
                    Box::pin(input_device_registry_mock(
                        handles,
                        Arc::clone(&media_button_listener),
                    ))
                },
                ChildOptions::new().eager(),
            )
            .await?;
        info.builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(
                        DeviceListenerRegistryMarker::PROTOCOL_NAME,
                    ))
                    .from(&input_device_registry)
                    .to(&setui_service),
            )
            .await?;

        // Provide LogSink to print out logs of the audio core component for debugging purposes.
        info.builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&input_device_registry),
            )
            .await?;

        let instance = info.builder.build().await?;
        Ok(instance)
    }

    pub fn connect_to_audio_marker(instance: &RealmInstance) -> AudioProxy {
        return instance
            .root
            .connect_to_protocol_at_exposed_dir::<AudioMarker>()
            .expect("connecting to Audio");
    }

    // Creates a listener to notify when a sound is played.
    pub async fn create_sound_played_listener(
        test_instance: &VolumeChangeEarconsTest,
    ) -> SoundEventReceiver {
        let (sound_played_sender, sound_played_receiver) =
            futures::channel::mpsc::channel::<(u32, AudioRenderUsage)>(0);
        test_instance.sound_played_listeners().lock().await.push(sound_played_sender);

        sound_played_receiver
    }

    pub async fn set_volume(proxy: &AudioProxy, streams: Vec<AudioStreamSettings>) {
        let mut audio_settings = AudioSettings::EMPTY;
        audio_settings.streams = Some(streams);
        proxy.set(audio_settings).await.expect("set completed").expect("set successful");
    }

    pub async fn verify_earcon(
        receiver: &mut SoundEventReceiver,
        id: u32,
        usage: AudioRenderUsage,
    ) {
        assert_eq!(receiver.next().await.unwrap(), (id, usage));
    }

    // Verifies that the settings for the given target_usage matches the expected_settings when
    // a watch is performed on the proxy.
    pub async fn verify_volume(
        proxy: &AudioProxy,
        target_usage: AudioRenderUsage,
        expected_settings: AudioStreamSettings,
    ) {
        let audio_settings = proxy.watch().await.expect("watch complete");
        let target_stream_res = audio_settings.streams.expect("streams exist");
        let target_stream = target_stream_res
            .iter()
            .find(|stream| stream.stream == Some(target_usage))
            .expect("stream found");
        assert_eq!(target_stream, &expected_settings);
    }

    // Creates a listener to notify when a media button is clicked.
    pub async fn create_media_button_listener(
        test_instance: &VolumeChangeEarconsTest,
    ) -> ButtonEventReceiver {
        let (media_button_sender, media_button_receiver) = futures::channel::mpsc::channel(0);
        *test_instance
            .media_button_listener()
            .expect("media_button_listener exists")
            .lock()
            .await = media_button_sender;

        media_button_receiver
    }
}
