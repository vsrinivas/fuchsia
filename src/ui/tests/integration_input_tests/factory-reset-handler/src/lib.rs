// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod mocks;
mod packaged_component;
mod traits;

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
    fuchsia_component_test::{DirectoryContents, RealmBuilder, RealmInstance},
    futures::StreamExt,
    input_synthesis::{modern_backend, synthesizer},
    test_case::test_case,
};

/// Creates a test realm with
/// a) routes from the given mocks to the input pipeline, and
/// b) all other capabilities routed from hermetically instantiated packages where possible, and
/// c) non-hermetic capabilities routed in from above the test realm.
async fn assemble_realm(
    input_owner_url: &str,
    sound_player_mock: SoundPlayerMock,
    pointer_injector_mock: PointerInjectorMock,
    factory_reset_mock: FactoryResetMock,
) -> RealmInstance {
    let b = RealmBuilder::new().await.expect("Failed to create RealmBuilder");

    // Declare packaged components.
    let scenic_test_realm =
        PackagedComponent::new_from_modern_url("scenic-test-realm", "#meta/scenic_only.cm");
    let input_owner = PackagedComponent::new_from_modern_url("input-owner", input_owner_url);

    // Add packaged components and mocks to the test realm.
    b.add(&scenic_test_realm).await;
    b.add(&input_owner).await;
    b.add(&sound_player_mock).await;
    b.add(&pointer_injector_mock).await;
    b.add(&factory_reset_mock).await;

    // Allow Scenic to access the capabilities it needs. Capabilities that can't
    // be run hermetically are routed from the parent realm. The remainder are
    // routed from peers.
    b.route_from_parent::<fidl_fuchsia_tracing_provider::RegistryMarker>(&scenic_test_realm).await;
    b.route_from_parent::<fidl_fuchsia_sysmem::AllocatorMarker>(&scenic_test_realm).await;
    b.route_from_parent::<fidl_fuchsia_vulkan_loader::LoaderMarker>(&scenic_test_realm).await;
    b.route_from_parent::<fidl_fuchsia_scheduler::ProfileProviderMarker>(&scenic_test_realm).await;

    // Allow the input pipeline to access the capabilities it needs. All of these
    // capabilities are run hermetically, so they are all routed from peers.
    b.route_to_peer::<fidl_fuchsia_ui_scenic::ScenicMarker>(&scenic_test_realm, &input_owner).await;
    b.route_to_peer::<fidl_fuchsia_ui_pointerinjector::RegistryMarker>(
        &scenic_test_realm,
        &input_owner,
    )
    .await;
    b.route_to_peer::<fidl_fuchsia_media_sounds::PlayerMarker>(&sound_player_mock, &input_owner)
        .await;
    b.route_to_peer::<fidl_fuchsia_ui_pointerinjector_configuration::SetupMarker>(
        &pointer_injector_mock,
        &input_owner,
    )
    .await;
    b.route_to_peer::<fidl_fuchsia_recovery::FactoryResetMarker>(&factory_reset_mock, &input_owner)
        .await;

    // Allow tests to inject input reports into the input pipeline.
    b.route_to_parent::<fidl_fuchsia_input_injection::InputDeviceRegistryMarker>(&input_owner)
        .await;

    // Route required config files to input pipeline.
    b.route_read_only_directory(
        String::from("config-data"),
        &input_owner,
        DirectoryContents::new()
            .add_file("chirp-start-tone.wav", "")
            .add_file("ignore_real_devices", ""),
    )
    .await;

    // Create the test realm.
    b.build().await.expect("Failed to create realm")
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

const SOUND_PLAYER_NAME: &'static str = "mock_sound_player";
const POINTER_INJECTOR_NAME: &'static str = "mock_pointer_injector";
const FACTORY_RESET_NAME: &'static str = "mock_factory_reset";
const INPUT_PIPELINE_URL: &'static str = "#meta/input-pipeline.cm";
const SCENE_MANAGER_URL: &'static str = "#meta/scene_manager.cm";

#[test_case(INPUT_PIPELINE_URL; "Input Pipeline variant")]
#[test_case(SCENE_MANAGER_URL; "Scene Manager variant")]
#[fuchsia::test]
async fn sound_is_played_during_factory_reset(input_owner_url: &str) {
    let (sound_request_relay_write_end, mut sound_request_relay_read_end) =
        futures::channel::mpsc::unbounded();
    let (reset_request_relay_write_end, mut reset_request_relay_read_end) =
        futures::channel::mpsc::unbounded();
    let sound_player_mock = SoundPlayerMock::new(
        SOUND_PLAYER_NAME,
        SoundPlayerBehavior::Succeed,
        Some(sound_request_relay_write_end),
    );
    let pointer_injector_mock = PointerInjectorMock::new(POINTER_INJECTOR_NAME, DEFAULT_VIEWPORT);
    let factory_reset_mock =
        FactoryResetMock::new(FACTORY_RESET_NAME, reset_request_relay_write_end);
    let realm = assemble_realm(
        input_owner_url,
        sound_player_mock,
        pointer_injector_mock,
        factory_reset_mock,
    )
    .await;

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

#[test_case(INPUT_PIPELINE_URL; "Input Pipeline variant")]
#[test_case(SCENE_MANAGER_URL; "Scene Manager variant")]
#[fuchsia::test]
async fn failure_to_load_sound_doesnt_block_factory_reset(input_owner_url: &str) {
    let (reset_request_relay_write_end, mut reset_request_relay_read_end) =
        futures::channel::mpsc::unbounded();
    let sound_player_mock =
        SoundPlayerMock::new(SOUND_PLAYER_NAME, SoundPlayerBehavior::FailAddSound, None);
    let pointer_injector_mock = PointerInjectorMock::new(POINTER_INJECTOR_NAME, DEFAULT_VIEWPORT);
    let factory_reset_mock =
        FactoryResetMock::new(FACTORY_RESET_NAME, reset_request_relay_write_end);
    let realm = assemble_realm(
        input_owner_url,
        sound_player_mock,
        pointer_injector_mock,
        factory_reset_mock,
    )
    .await;

    // Press buttons for factory reset, and verify that `factory_reset_mock`
    // received the reset request.
    perform_factory_reset(&realm).await;
    reset_request_relay_read_end.next().await;

    // Shut down input pipeline before dropping mocks, so that input pipeline doesn't
    // log errors about channels being closed.
    realm.destroy().await.unwrap();
}

#[test_case(INPUT_PIPELINE_URL; "Input Pipeline variant")]
#[test_case(SCENE_MANAGER_URL; "Scene Manager variant")]
#[fuchsia::test]
async fn failure_to_play_sound_doesnt_block_factory_reset(input_owner_url: &str) {
    let (reset_request_relay_write_end, mut reset_request_relay_read_end) =
        futures::channel::mpsc::unbounded();
    let sound_player_mock =
        SoundPlayerMock::new(SOUND_PLAYER_NAME, SoundPlayerBehavior::FailPlaySound, None);
    let pointer_injector_mock = PointerInjectorMock::new(POINTER_INJECTOR_NAME, DEFAULT_VIEWPORT);
    let factory_reset_mock =
        FactoryResetMock::new(FACTORY_RESET_NAME, reset_request_relay_write_end);
    let realm = assemble_realm(
        input_owner_url,
        sound_player_mock,
        pointer_injector_mock,
        factory_reset_mock,
    )
    .await;

    // Press buttons for factory reset, and verify that `factory_reset_mock`
    // received the reset request.
    perform_factory_reset(&realm).await;
    reset_request_relay_read_end.next().await;

    // Shut down input pipeline before dropping mocks, so that input pipeline doesn't
    // log errors about channels being closed.
    realm.destroy().await.unwrap();
}
