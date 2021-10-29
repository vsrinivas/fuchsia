// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        mocks::{
            factory_reset_mock::FactoryResetMock,
            pointer_injector_mock::PointerInjectorMock,
            sound_player_mock::{SoundPlayerBehavior, SoundPlayerMock, SoundPlayerRequestName},
        },
        packaged_component::PackagedComponent,
        traits::realm_builder_ext::RealmBuilderExt as _,
    },
    fidl_fuchsia_ui_pointerinjector as pointerinjector,
    fuchsia_component_test::{builder::RealmBuilder, Moniker, RealmInstance},
    futures::StreamExt,
    input_synthesis::{modern_backend, synthesizer},
};

/// Creates a test realm with
/// a) routes from the given mocks to the input pipeline, and
/// b) all other capabilities routed from hermetically instantiated packages where possible, and
/// c) non-hermetic capabilities routed in from above the test realm.
async fn assemble_realm(
    sound_player_mock: SoundPlayerMock,
    pointer_injector_mock: PointerInjectorMock,
    factory_reset_mock: FactoryResetMock,
) -> RealmInstance {
    let mut b = RealmBuilder::new().await.expect("Failed to create RealmBuilder");

    // Declare packaged components.
    let scenic = PackagedComponent::new_from_legacy_url(
        Moniker::from("scenic"),
        "fuchsia-pkg://fuchsia.com/factory-reset-handler-test#meta/scenic.cmx",
    );
    let display_provider = PackagedComponent::new_from_modern_url(
        Moniker::from("fake_display_provider"),
        "#meta/hdcp.cm",
    );
    let cobalt = PackagedComponent::new_from_modern_url(
        Moniker::from("mock_cobalt"),
        "#meta/mock_cobalt.cm",
    );
    let input_pipeline = PackagedComponent::new_from_legacy_url(
        Moniker::from("input_pipeline"),
        "fuchsia-pkg://fuchsia.com/factory-reset-handler-test#meta/input-pipeline.cmx",
    );

    // Add packaged components and mocks to the test realm.
    b.add(&scenic).await;
    b.add(&display_provider).await;
    b.add(&cobalt).await;
    b.add(&input_pipeline).await;
    b.add(&sound_player_mock).await;
    b.add(&pointer_injector_mock).await;
    b.add(&factory_reset_mock).await;

    // Allow Scenic to access the capabilities it needs. Capabilities that can't
    // be run hermetically are routed from the parent realm. The remainder are
    // routed from peers.
    b.route_from_parent::<fidl_fuchsia_tracing_provider::RegistryMarker>(&scenic);
    b.route_from_parent::<fidl_fuchsia_sysmem::AllocatorMarker>(&scenic);
    b.route_from_parent::<fidl_fuchsia_vulkan_loader::LoaderMarker>(&scenic);
    b.route_from_parent::<fidl_fuchsia_scheduler::ProfileProviderMarker>(&scenic);
    b.route_to_peer::<fidl_fuchsia_hardware_display::ProviderMarker>(&display_provider, &scenic);
    b.route_to_peer::<fidl_fuchsia_cobalt::LoggerFactoryMarker>(&cobalt, &scenic);

    // Allow the display provider to access the capabilities it needs. None of these
    // capabilities can be run hermetically, so they are all routed from the parent
    // realm.
    b.route_from_parent::<fidl_fuchsia_tracing_provider::RegistryMarker>(&display_provider);
    b.route_from_parent::<fidl_fuchsia_sysmem::AllocatorMarker>(&display_provider);

    // Allow the input pipeline to access the capabilities it needs. All of these
    // capabilities are run hermetically, so they are all routed from peers.
    b.route_to_peer::<fidl_fuchsia_ui_scenic::ScenicMarker>(&scenic, &input_pipeline);
    b.route_to_peer::<fidl_fuchsia_ui_pointerinjector::RegistryMarker>(&scenic, &input_pipeline);
    b.route_to_peer::<fidl_fuchsia_media_sounds::PlayerMarker>(&sound_player_mock, &input_pipeline);
    b.route_to_peer::<fidl_fuchsia_ui_pointerinjector_configuration::SetupMarker>(
        &pointer_injector_mock,
        &input_pipeline,
    );
    b.route_to_peer::<fidl_fuchsia_recovery::FactoryResetMarker>(
        &factory_reset_mock,
        &input_pipeline,
    );

    // Allow tests to inject input reports into the input pipeline.
    b.route_to_parent::<fidl_fuchsia_input_injection::InputDeviceRegistryMarker>(&input_pipeline);

    // Create the test realm.
    b.build().create().await.expect("Failed to create realm")
}

async fn perform_factory_reset(realm: &RealmInstance) {
    let injection_registry = realm.root
        .connect_to_protocol_at_exposed_dir::<fidl_fuchsia_input_injection::InputDeviceRegistryMarker>()
        .expect("Failed to connect to InputDeviceRegistry");
    let mut device_registry = modern_backend::InputDeviceRegistry::new(injection_registry);
    synthesizer::media_button_event([synthesizer::MediaButton::FactoryReset], &mut device_registry)
        .await
        .expect("Failed to inject reset event");
}

const DEFAULT_VIEWPORT: pointerinjector::Viewport = pointerinjector::Viewport {
    extents: Some([[0.0, 0.0], [100.0, 100.0]]),
    viewport_to_context_transform: Some([1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0]),
    ..pointerinjector::Viewport::EMPTY
};

const SOUND_PLAYER_MONIKER: &'static str = "mock_sound_player";
const POINTER_INJECTOR_MONIKER: &'static str = "mock_pointer_injector";
const FACTORY_RESET_MONIKER: &'static str = "mock_factory_reset";

#[fuchsia::test]
async fn sound_is_played_during_factory_reset() {
    let (sound_request_relay_write_end, mut sound_request_relay_read_end) =
        futures::channel::mpsc::unbounded();
    let (reset_request_relay_write_end, mut reset_request_relay_read_end) =
        futures::channel::mpsc::unbounded();
    let sound_player_mock = SoundPlayerMock::new(
        SOUND_PLAYER_MONIKER,
        SoundPlayerBehavior::Succeed,
        Some(sound_request_relay_write_end),
    );
    let pointer_injector_mock =
        PointerInjectorMock::new(POINTER_INJECTOR_MONIKER, DEFAULT_VIEWPORT);
    let factory_reset_mock =
        FactoryResetMock::new(FACTORY_RESET_MONIKER, reset_request_relay_write_end);
    let realm = assemble_realm(sound_player_mock, pointer_injector_mock, factory_reset_mock).await;

    // Press buttons for factory reset, and verify that `factory_reset_mock`
    // received the reset request.
    perform_factory_reset(&realm).await;
    reset_request_relay_read_end.next().await;

    // Verify that sound was played.
    assert_eq!(
        sound_request_relay_read_end.next().await.unwrap(),
        SoundPlayerRequestName::AddSoundFromFile
    );
    assert_eq!(
        sound_request_relay_read_end.next().await.unwrap(),
        SoundPlayerRequestName::PlaySound
    );

    // Shut down input pipeline before dropping mocks, so that input pipeline doesn't
    // log errors about channels being closed.
    realm.destroy().await.unwrap();
}

#[fuchsia::test]
async fn failure_to_load_sound_doesnt_block_factory_reset() {
    let (reset_request_relay_write_end, mut reset_request_relay_read_end) =
        futures::channel::mpsc::unbounded();
    let sound_player_mock =
        SoundPlayerMock::new(SOUND_PLAYER_MONIKER, SoundPlayerBehavior::FailAddSound, None);
    let pointer_injector_mock =
        PointerInjectorMock::new(POINTER_INJECTOR_MONIKER, DEFAULT_VIEWPORT);
    let factory_reset_mock =
        FactoryResetMock::new(FACTORY_RESET_MONIKER, reset_request_relay_write_end);
    let realm = assemble_realm(sound_player_mock, pointer_injector_mock, factory_reset_mock).await;

    // Press buttons for factory reset, and verify that `factory_reset_mock`
    // received the reset request.
    perform_factory_reset(&realm).await;
    reset_request_relay_read_end.next().await;

    // Shut down input pipeline before dropping mocks, so that input pipeline doesn't
    // log errors about channels being closed.
    realm.destroy().await.unwrap();
}

#[fuchsia::test]
async fn failure_to_play_sound_doesnt_block_factory_reset() {
    let (reset_request_relay_write_end, mut reset_request_relay_read_end) =
        futures::channel::mpsc::unbounded();
    let sound_player_mock =
        SoundPlayerMock::new(SOUND_PLAYER_MONIKER, SoundPlayerBehavior::FailPlaySound, None);
    let pointer_injector_mock =
        PointerInjectorMock::new(POINTER_INJECTOR_MONIKER, DEFAULT_VIEWPORT);
    let factory_reset_mock =
        FactoryResetMock::new(FACTORY_RESET_MONIKER, reset_request_relay_write_end);
    let realm = assemble_realm(sound_player_mock, pointer_injector_mock, factory_reset_mock).await;

    // Press buttons for factory reset, and verify that `factory_reset_mock`
    // received the reset request.
    perform_factory_reset(&realm).await;
    reset_request_relay_read_end.next().await;

    // Shut down input pipeline before dropping mocks, so that input pipeline doesn't
    // log errors about channels being closed.
    realm.destroy().await.unwrap();
}
