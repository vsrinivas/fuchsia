// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::mock_audio_core_service::audio_core_service_mock;
use crate::mock_discovery::{discovery_service_mock, remove_session, update_session};
use crate::mock_input_device_registry::{input_device_registry_mock, ButtonEventSender};
use crate::mock_sound_player_service::{
    sound_player_service_mock, SoundEventReceiver, SoundEventSender,
};
use anyhow::Error;
use fidl::endpoints::DiscoverableProtocolMarker;
use fidl_fuchsia_media::{AudioCoreMarker, AudioRenderUsage};
use fidl_fuchsia_media_sessions2::{DiscoveryMarker, SessionsWatcherProxy};
use fidl_fuchsia_media_sounds::PlayerMarker;
use fidl_fuchsia_settings::{AudioMarker, AudioProxy, AudioSettings, AudioStreamSettings};
use fidl_fuchsia_ui_policy::DeviceListenerRegistryMarker;
use fuchsia_component_test::{
    Capability, ChildOptions, LocalComponentHandles, RealmBuilder, RealmInstance, Ref, Route,
};
use futures::channel::mpsc::{Receiver, Sender};
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
    /// The test realm the integration tests will use.
    realm: RealmInstance,

    /// The audio proxy to use for testing. Additional connections can be made with
    /// [VolumeChangeEarconsTest::connect_to_audio_marker] if needed.
    audio_proxy: AudioProxy,

    /// Represents the number of times the sound has been played in total. First u32 represents the
    /// sound id, and the second u32 is the number of times.
    play_counts: Arc<Mutex<HashMap<u32, u32>>>,

    /// Watchers on the discovery service that have called WatchSession.
    discovery_watchers: Arc<Mutex<Vec<SessionsWatcherProxy>>>,

    /// The listeners to notify that a sound was played.
    sound_played_listeners: Arc<Mutex<Vec<SoundEventSender>>>,

    /// Receiver that receives an event when the audio_core usage control is bound.
    usage_control_bound_receiver: Receiver<()>,

    /// Receiver that receives an event when a bluetooth discovery request watcher is registered.
    discovery_watcher_receiver: Receiver<()>,
}

impl VolumeChangeEarconsTest {
    /// Creates a test realm and waits for the initial bind request on audio core.
    pub async fn create_realm_and_init() -> Result<Self, Error> {
        VolumeChangeEarconsTest::create_realm_with_input_and_init(None).await
    }

    /// Creates a test realm that may include the input device registry and waits for the initial
    /// bind request on audio core.
    ///
    /// If `button_event_sender` is provided, the input device registry mock will be started.
    pub async fn create_realm_with_input_and_init(
        button_event_sender: Option<ButtonEventSender>,
    ) -> Result<Self, Error> {
        let (usage_control_bound_sender, usage_control_bound_receiver) =
            futures::channel::mpsc::channel::<()>(0);
        let (discovery_watcher_sender, discovery_watcher_receiver) =
            futures::channel::mpsc::channel::<()>(0);

        let play_counts = Arc::new(Mutex::new(HashMap::new()));
        let discovery_watchers = Arc::new(Mutex::new(Vec::new()));
        let sound_played_listeners = Arc::new(Mutex::new(Vec::new()));
        let realm_instance = VolumeChangeEarconsTest::create_realm(
            Arc::clone(&play_counts),
            Arc::clone(&discovery_watchers),
            Arc::clone(&sound_played_listeners),
            usage_control_bound_sender,
            discovery_watcher_sender,
            button_event_sender,
        )
        .await?;

        let audio_proxy = VolumeChangeEarconsTest::connect_to_audio_marker(&realm_instance);

        let mut test_instance = Self {
            realm: realm_instance,
            audio_proxy,
            play_counts,
            discovery_watchers,
            sound_played_listeners,
            usage_control_bound_receiver,
            discovery_watcher_receiver,
        };

        // Verify that audio core receives the initial request on start.
        let _ = test_instance.usage_control_bound_receiver.next().await;

        Ok(test_instance)
    }

    pub fn play_counts(&self) -> Arc<Mutex<HashMap<u32, u32>>> {
        Arc::clone(&self.play_counts)
    }

    fn discovery_watchers(&self) -> Arc<Mutex<Vec<SessionsWatcherProxy>>> {
        Arc::clone(&self.discovery_watchers)
    }

    pub fn discovery_watcher_receiver(&mut self) -> &mut Receiver<()> {
        &mut self.discovery_watcher_receiver
    }

    /// Simulates updating a media session.
    pub async fn update_session(&self, id: SessionId, domain: &str) {
        update_session(self.discovery_watchers(), id, domain).await;
    }

    /// Simulates removing a media session.
    pub async fn remove_session(&self, id: SessionId) {
        remove_session(self.discovery_watchers(), id).await;
    }

    async fn create_realm(
        play_counts: Arc<Mutex<HashMap<u32, u32>>>,
        discovery_watchers: Arc<Mutex<Vec<SessionsWatcherProxy>>>,
        sound_played_listeners: Arc<Mutex<Vec<SoundEventSender>>>,
        usage_control_bound_sender: Sender<()>,
        discovery_watcher_sender: Sender<()>,
        button_event_sender: Option<ButtonEventSender>,
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
        let usage_control_bound_sender = usage_control_bound_sender.clone();
        let audio_service = info
            .builder
            .add_local_child(
                "audio_service",
                move |handles: LocalComponentHandles| {
                    let usage_control_bound_sender = usage_control_bound_sender.clone();
                    Box::pin(audio_core_service_mock(handles, usage_control_bound_sender))
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
        let play_counts = Arc::clone(&play_counts);
        let sound_played_listeners = Arc::clone(&sound_played_listeners);
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
        let discovery_watchers = Arc::clone(&discovery_watchers);
        let discovery_service = info
            .builder
            .add_local_child(
                "discovery_service",
                move |handles: LocalComponentHandles| {
                    let discovery_watchers = Arc::clone(&discovery_watchers);
                    let discovery_watcher_sender = discovery_watcher_sender.clone();
                    Box::pin(discovery_service_mock(
                        handles,
                        discovery_watchers,
                        discovery_watcher_sender,
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

        // Add input device registry if the button event sender is provided.
        if let Some(button_event_sender) = button_event_sender {
            let input_device_registry = info
                .builder
                .add_local_child(
                    "input_device_registry",
                    move |handles: LocalComponentHandles| {
                        let button_event_sender = button_event_sender.clone();
                        Box::pin(input_device_registry_mock(handles, button_event_sender))
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
        }

        let instance = info.builder.build().await?;
        Ok(instance)
    }

    fn connect_to_audio_marker(instance: &RealmInstance) -> AudioProxy {
        return instance
            .root
            .connect_to_protocol_at_exposed_dir::<AudioMarker>()
            .expect("connecting to Audio");
    }

    // Creates a listener to notify when a sound is played.
    pub async fn create_sound_played_listener(&self) -> SoundEventReceiver {
        let (sound_played_sender, sound_played_receiver) =
            futures::channel::mpsc::channel::<(u32, AudioRenderUsage)>(0);
        self.sound_played_listeners.lock().await.push(sound_played_sender);

        sound_played_receiver
    }

    pub async fn verify_earcon(
        receiver: &mut SoundEventReceiver,
        id: u32,
        usage: AudioRenderUsage,
    ) {
        assert_eq!(receiver.next().await.unwrap(), (id, usage));
    }

    pub async fn set_volume(&self, streams: Vec<AudioStreamSettings>) {
        let mut audio_settings = AudioSettings::EMPTY;
        audio_settings.streams = Some(streams);
        self.audio_proxy.set(audio_settings).await.expect("set completed").expect("set successful");
    }

    // Verifies that the settings for the given target_usage matches the expected_settings when
    // a watch is performed on the proxy.
    pub async fn verify_volume(
        &self,
        target_usage: AudioRenderUsage,
        expected_settings: AudioStreamSettings,
    ) {
        let audio_settings = self.audio_proxy.watch().await.expect("watch complete");
        let target_stream_res = audio_settings.streams.expect("streams exist");
        let target_stream = target_stream_res
            .iter()
            .find(|stream| stream.stream == Some(target_usage))
            .expect("stream found");
        assert_eq!(target_stream, &expected_settings);
    }

    /// Destroy realm instance after each test to avoid unexpected behavior.
    pub async fn destroy(self) {
        let _ = self.realm.destroy().await;
    }
}
