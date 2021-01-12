// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::base::SettingType,
    crate::handler::device_storage::testing::*,
    crate::input::common::MediaButtonsEventBuilder,
    crate::input::input_device_configuration::{
        InputConfiguration, InputDeviceConfiguration, SourceState,
    },
    crate::input::monitor_media_buttons,
    crate::service_context::ServiceContext,
    crate::switchboard::base::{InputInfoSources, Microphone},
    crate::switchboard::input_types::{
        DeviceState, DeviceStateSource, InputCategory, InputDeviceType, InputState,
    },
    crate::tests::fakes::input_device_registry_service::InputDeviceRegistryService,
    crate::tests::fakes::service_registry::ServiceRegistry,
    crate::tests::input_test_environment::{TestInputEnvironment, TestInputEnvironmentBuilder},
    crate::tests::test_failure_utils::create_test_env_with_failures,
    fidl::Error::ClientChannelClosed,
    fidl_fuchsia_settings::{
        DeviceState as FidlDeviceState, DeviceType, InputDeviceSettings, InputMarker, InputProxy,
        InputSettings, InputState as FidlInputState, Microphone as FidlMicrophone,
        ToggleStateFlags,
    },
    fidl_fuchsia_ui_input::MediaButtonsEvent,
    fuchsia_zircon::Status,
    futures::lock::Mutex,
    futures::stream::StreamExt,
    matches::assert_matches,
    std::collections::HashMap,
    std::sync::Arc,
};

const DEFAULT_MIC_STATE: bool = false;
const DEFAULT_CAMERA_STATE: bool = false;
const MUTED_BITS: u64 = 4;
const AVAILABLE_BITS: u64 = 1;
const MUTED_DISABLED_BITS: u64 = 12;
const DEFAULT_MIC_NAME: &str = "microphone";
const DEFAULT_CAMERA_NAME: &str = "camera";
const ENV_NAME: &str = "settings_service_input_test_environment";

fn create_default_input_info() -> InputInfoSources {
    InputInfoSources {
        hw_microphone: Microphone { muted: false },
        sw_microphone: Microphone { muted: false },
        input_device_state: InputState {
            input_categories: HashMap::<InputDeviceType, InputCategory>::new(),
        },
    }
}

// An InputConfiguration with an available microphone.
fn default_mic_config() -> InputConfiguration {
    InputConfiguration {
        devices: vec![InputDeviceConfiguration {
            device_name: DEFAULT_MIC_NAME.to_string(),
            device_type: InputDeviceType::MICROPHONE,
            source_states: vec![
                SourceState { source: DeviceStateSource::HARDWARE, state: AVAILABLE_BITS },
                SourceState { source: DeviceStateSource::SOFTWARE, state: AVAILABLE_BITS },
            ],
            mutable_toggle_state: MUTED_DISABLED_BITS,
        }],
    }
}

// An InputConfiguration with a muted microphone.
fn default_mic_config_muted() -> InputConfiguration {
    InputConfiguration {
        devices: vec![InputDeviceConfiguration {
            device_name: DEFAULT_MIC_NAME.to_string(),
            device_type: InputDeviceType::MICROPHONE,
            source_states: vec![
                SourceState { source: DeviceStateSource::HARDWARE, state: AVAILABLE_BITS },
                SourceState { source: DeviceStateSource::SOFTWARE, state: MUTED_BITS },
            ],
            mutable_toggle_state: MUTED_DISABLED_BITS,
        }],
    }
}

// An InputConfiguration with an available microphone and camera.
fn default_mic_cam_config() -> InputConfiguration {
    InputConfiguration {
        devices: vec![
            InputDeviceConfiguration {
                device_name: DEFAULT_MIC_NAME.to_string(),
                device_type: InputDeviceType::MICROPHONE,
                source_states: vec![
                    SourceState { source: DeviceStateSource::HARDWARE, state: AVAILABLE_BITS },
                    SourceState { source: DeviceStateSource::SOFTWARE, state: AVAILABLE_BITS },
                ],
                mutable_toggle_state: MUTED_DISABLED_BITS,
            },
            InputDeviceConfiguration {
                device_name: DEFAULT_CAMERA_NAME.to_string(),
                device_type: InputDeviceType::CAMERA,
                source_states: vec![
                    SourceState { source: DeviceStateSource::HARDWARE, state: AVAILABLE_BITS },
                    SourceState { source: DeviceStateSource::SOFTWARE, state: AVAILABLE_BITS },
                ],
                mutable_toggle_state: MUTED_DISABLED_BITS,
            },
        ],
    }
}

// An InputConfiguration with a muted microphone and disabled camera.
fn default_mic_cam_config_cam_disabled() -> InputConfiguration {
    InputConfiguration {
        devices: vec![
            InputDeviceConfiguration {
                device_name: DEFAULT_MIC_NAME.to_string(),
                device_type: InputDeviceType::MICROPHONE,
                source_states: vec![
                    SourceState { source: DeviceStateSource::HARDWARE, state: AVAILABLE_BITS },
                    SourceState { source: DeviceStateSource::SOFTWARE, state: MUTED_BITS },
                ],
                mutable_toggle_state: MUTED_DISABLED_BITS,
            },
            InputDeviceConfiguration {
                device_name: DEFAULT_CAMERA_NAME.to_string(),
                device_type: InputDeviceType::CAMERA,
                source_states: vec![
                    SourceState { source: DeviceStateSource::HARDWARE, state: MUTED_BITS },
                    SourceState { source: DeviceStateSource::SOFTWARE, state: AVAILABLE_BITS },
                ],
                mutable_toggle_state: MUTED_DISABLED_BITS,
            },
        ],
    }
}

// Creates an environment that will fail on a get request.
async fn create_input_test_env_with_failures(
    storage_factory: Arc<Mutex<InMemoryStorageFactory>>,
) -> InputProxy {
    create_test_env_with_failures(storage_factory, ENV_NAME, SettingType::Input)
        .await
        .connect_to_service::<InputMarker>()
        .unwrap()
}

// Set the software mic mute state to muted = [mic_muted].
async fn set_mic_mute(proxy: &InputProxy, mic_muted: bool) {
    let mut input_device_settings = InputDeviceSettings::EMPTY;
    let mut microphone = FidlMicrophone::EMPTY;

    microphone.muted = Some(mic_muted);
    input_device_settings.microphone = Some(microphone);
    proxy.set(input_device_settings).await.expect("set completed").expect("set successful");
}

// Set the software mic mute state to muted = [mic_muted] for input2.
async fn set_mic_mute_input2(proxy: &InputProxy, mic_muted: bool) {
    // TODO(fxbug.dev/65686): Remove once clients are ported to input2.
    set_mic_mute(proxy, mic_muted).await;

    set_device_muted(proxy, mic_muted, DEFAULT_MIC_NAME, DeviceType::Microphone).await;
}

// Set the software camera disabled state to disabled = [disabled].
async fn set_camera_disable(proxy: &InputProxy, disabled: bool) {
    set_device_muted(proxy, disabled, DEFAULT_CAMERA_NAME, DeviceType::Camera).await;
}

// Helper to set a device's muted state via the input proxy.
async fn set_device_muted(
    proxy: &InputProxy,
    muted: bool,
    device_name: &str,
    device_type: DeviceType,
) {
    let mut input_state = FidlInputState::EMPTY;
    let mut states = Vec::new();

    input_state.name = Some(device_name.to_string());
    input_state.device_type = Some(device_type);
    input_state.state = Some(FidlDeviceState {
        toggle_flags: ToggleStateFlags::from_bits(if muted { MUTED_BITS } else { AVAILABLE_BITS }),
        ..FidlDeviceState::EMPTY
    });

    states.push(input_state);
    proxy
        .set_states(&mut states.into_iter())
        .await
        .expect("set completed")
        .expect("set successful");
}

// Switch the hardware mic state to muted = [muted] for input2.
async fn switch_hardware_mic_mute(env: &TestInputEnvironment, muted: bool) {
    let buttons_event = MediaButtonsEventBuilder::new().set_volume(1).set_mic_mute(muted).build();
    env.input_button_service.lock().await.send_media_button_event(buttons_event.clone());
}

// Switch the hardware camera disable to disabled = [disabled].
async fn switch_hardware_camera_disable(env: &TestInputEnvironment, disabled: bool) {
    let buttons_event =
        MediaButtonsEventBuilder::new().set_volume(1).set_camera_disable(disabled).build();
    env.input_button_service.lock().await.send_media_button_event(buttons_event.clone());
}

// Perform a watch and watch2 and check that the mic mute state matches [expected_muted_state].
async fn get_and_check_mic_mute(input_proxy: &InputProxy, expected_muted_state: bool) {
    let settings = input_proxy.watch().await.expect("watch completed");
    let settings2 = input_proxy.watch2().await.expect("watch2 completed");

    assert_eq!(settings.microphone.unwrap().muted, Some(expected_muted_state));
    verify_muted_state(settings2, expected_muted_state, DeviceType::Microphone);
}

// Perform a watch2 and check that the camera disabled state matches [expected_camera_disabled_state].
async fn get_and_check_camera_disable(
    input_proxy: &InputProxy,
    expected_camera_disabled_state: bool,
) {
    let settings2 = input_proxy.watch2().await.expect("watch2 completed");
    verify_muted_state(settings2, expected_camera_disabled_state, DeviceType::Camera);
}

// Perform a watch2 and check that the mic mute state matches [expected_mic_mute_state]
// and the camera disabled state matches [expected_camera_disabled_state].
async fn get_and_check_state(
    input_proxy: &InputProxy,
    expected_mic_mute_state: bool,
    expected_camera_disabled_state: bool,
) {
    let settings2 = input_proxy.watch2().await.expect("watch2 completed");
    verify_muted_state(settings2.clone(), expected_camera_disabled_state, DeviceType::Camera);
    verify_muted_state(settings2.clone(), expected_mic_mute_state, DeviceType::Microphone);
}

// Helper for checking the returned muted state for a given
// device type in the watch2 results.
fn verify_muted_state(
    settings: InputSettings,
    expected_muted_state: bool,
    device_type: DeviceType,
) {
    assert_eq!(
        settings
            .devices
            .unwrap()
            .iter()
            .find(|x| x.device_type == Some(device_type))
            .expect("Device not found in results")
            .state,
        if expected_muted_state {
            Some(FidlDeviceState {
                toggle_flags: ToggleStateFlags::from_bits(MUTED_BITS),
                ..FidlDeviceState::EMPTY
            })
        } else {
            Some(FidlDeviceState {
                toggle_flags: ToggleStateFlags::from_bits(AVAILABLE_BITS),
                ..FidlDeviceState::EMPTY
            })
        },
    );
}

// Test that a watch is executed correctly.
#[fuchsia_async::run_until_stalled(test)]
async fn test_watch() {
    let env = TestInputEnvironmentBuilder::new()
        .set_input_device_config(default_mic_config())
        .build()
        .await;
    let input_proxy = env.input_service.clone();

    get_and_check_mic_mute(&input_proxy, false).await;
}

// Test that a set then watch for the mic is executed correctly.
#[fuchsia_async::run_until_stalled(test)]
async fn test_set_watch_mic_mute() {
    let env = TestInputEnvironmentBuilder::new()
        .set_input_device_config(default_mic_config_muted())
        .build()
        .await;
    let input_proxy = env.input_service.clone();

    // SW muted, HW unmuted.
    set_mic_mute_input2(&input_proxy, true).await;
    get_and_check_mic_mute(&input_proxy, true).await;

    // SW unmuted, HW muted.
    switch_hardware_mic_mute(&env, true).await;
    set_mic_mute_input2(&input_proxy, false).await;
    get_and_check_mic_mute(&input_proxy, true).await;

    // SW unmuted, HW unmuted.
    switch_hardware_mic_mute(&env, false).await;

    // TODO(fxb/66313): We make a watch call here in order to force the previous
    // operation (button change) through the setting service. The correct fix
    // is to have an executor that we can loop until idle instead.
    let _ = input_proxy.watch().await.expect("watch completed");
    get_and_check_mic_mute(&input_proxy, false).await;
}

// Test that a set then watch for the camera is executed correctly.
#[fuchsia_async::run_until_stalled(test)]
async fn test_set_watch_camera_disable() {
    let env = TestInputEnvironmentBuilder::new()
        .set_input_device_config(default_mic_cam_config())
        .build()
        .await;
    let input_proxy = env.input_service.clone();

    // SW muted, HW unmuted.
    set_camera_disable(&input_proxy, true).await;
    get_and_check_camera_disable(&input_proxy, true).await;

    // SW unmuted, HW muted.
    switch_hardware_camera_disable(&env, true).await;
    set_camera_disable(&input_proxy, false).await;
    get_and_check_camera_disable(&input_proxy, true).await;

    // SW unmuted, HW unmuted.
    switch_hardware_camera_disable(&env, false).await;

    // TODO(fxb/66313): We make a watch call here in order to force the previous
    // operation (button change) through the setting service. The correct fix
    // is to have an executor that we can loop until idle instead.
    let _ = input_proxy.watch().await.expect("watch completed");
    get_and_check_camera_disable(&input_proxy, false).await;
}

// Test to ensure mic input change events are received.
#[fuchsia_async::run_until_stalled(test)]
async fn test_mic_input() {
    let env = TestInputEnvironmentBuilder::new()
        .set_input_device_config(default_mic_config())
        .build()
        .await;
    let input_proxy = env.input_service.clone();

    switch_hardware_mic_mute(&env, true).await;
    get_and_check_mic_mute(&input_proxy, true).await;
}

// Test to ensure camera input change events are received.
#[fuchsia_async::run_until_stalled(test)]
async fn test_camera_input() {
    let env = TestInputEnvironmentBuilder::new()
        .set_input_device_config(default_mic_cam_config())
        .build()
        .await;
    let input_proxy = env.input_service.clone();

    switch_hardware_camera_disable(&env, true).await;
    get_and_check_camera_disable(&input_proxy, true).await;
}

// Test that when either hardware or software is muted, the service
// reports the microphone as muted.
#[fuchsia_async::run_until_stalled(test)]
async fn test_mic_mute_combinations() {
    let env = TestInputEnvironmentBuilder::new()
        .set_input_device_config(default_mic_config())
        .build()
        .await;
    let input_proxy = env.input_service.clone();

    // Hardware muted, software unmuted.
    switch_hardware_mic_mute(&env, true).await;
    set_mic_mute_input2(&input_proxy, false).await;
    get_and_check_mic_mute(&input_proxy, true).await;

    // Hardware muted, software muted.
    set_mic_mute_input2(&input_proxy, true).await;
    get_and_check_mic_mute(&input_proxy, true).await;

    // Hardware unmuted, software muted.
    switch_hardware_mic_mute(&env, false).await;
    get_and_check_mic_mute(&input_proxy, true).await;

    // Hardware unmuted, software unmuted.
    switch_hardware_mic_mute(&env, false).await;
    set_mic_mute_input2(&input_proxy, false).await;
    get_and_check_mic_mute(&input_proxy, false).await;
}

// Test that when either hardware or software is disabled, the service
// reports the camera as disabled.
#[fuchsia_async::run_until_stalled(test)]
async fn test_camera_disable_combinations() {
    let env = TestInputEnvironmentBuilder::new()
        .set_input_device_config(default_mic_cam_config())
        .build()
        .await;
    let input_proxy = env.input_service.clone();

    // Hardware disabled, software enabled.
    switch_hardware_camera_disable(&env, true).await;
    set_camera_disable(&input_proxy, false).await;
    get_and_check_camera_disable(&input_proxy, true).await;

    // Hardware disabled, software disabled.
    set_camera_disable(&input_proxy, true).await;
    get_and_check_camera_disable(&input_proxy, true).await;

    // Hardware enabled, software disabled.
    switch_hardware_camera_disable(&env, false).await;
    get_and_check_camera_disable(&input_proxy, true).await;

    // Hardware enabled, software enabled.
    switch_hardware_camera_disable(&env, false).await;
    set_camera_disable(&input_proxy, false).await;
    get_and_check_camera_disable(&input_proxy, false).await;
}

// Test that the input settings are restored correctly.
#[fuchsia_async::run_until_stalled(test)]
async fn test_restore() {
    let env = TestInputEnvironmentBuilder::new()
        .set_input_device_config(default_mic_config_muted())
        .build()
        .await;
    let input_proxy = env.input_service.clone();
    let store = env.store.clone();
    {
        let mut stored_info = create_default_input_info().clone();
        stored_info.sw_microphone.muted = true;
        stored_info.input_device_state = default_mic_cam_config_cam_disabled().into();
        assert!(store.lock().await.write(&stored_info, false).await.is_ok());
    }

    get_and_check_state(&input_proxy, true, true).await;
}

// Test to ensure mic input change events are received.
#[fuchsia_async::run_until_stalled(test)]
async fn test_bringup_without_input_registry() {
    let env = TestInputEnvironmentBuilder::new()
        .set_input_device_config(default_mic_cam_config())
        .build()
        .await;
    let input_proxy = env.input_service.clone();

    get_and_check_state(&input_proxy, DEFAULT_MIC_STATE, DEFAULT_CAMERA_STATE).await;
}

// Test that cloning works.
#[test]
fn test_input_info_copy() {
    let input_info = create_default_input_info();
    let copy_input_info = input_info.clone();
    assert_eq!(input_info, copy_input_info);
}

// Test that the values in the persistent store are restored at the start.
#[fuchsia_async::run_until_stalled(test)]
async fn test_persisted_values_applied_at_start() {
    let env = TestInputEnvironmentBuilder::new()
        .set_input_device_config(default_mic_cam_config())
        .build()
        .await;
    let input_proxy = env.input_service.clone();
    let store = env.store.clone();

    let mut test_input_info = InputInfoSources {
        hw_microphone: Microphone { muted: false },
        sw_microphone: Microphone { muted: true },
        input_device_state: InputState {
            input_categories: HashMap::<InputDeviceType, InputCategory>::new(),
        },
    };

    test_input_info.input_device_state.set_source_state(
        InputDeviceType::MICROPHONE,
        DEFAULT_MIC_NAME.to_string(),
        DeviceStateSource::SOFTWARE,
        DeviceState::from_bits(MUTED_BITS).unwrap(),
    );
    test_input_info.input_device_state.set_source_state(
        InputDeviceType::MICROPHONE,
        DEFAULT_MIC_NAME.to_string(),
        DeviceStateSource::HARDWARE,
        DeviceState::from_bits(AVAILABLE_BITS).unwrap(),
    );
    test_input_info.input_device_state.set_source_state(
        InputDeviceType::CAMERA,
        DEFAULT_CAMERA_NAME.to_string(),
        DeviceStateSource::SOFTWARE,
        DeviceState::from_bits(AVAILABLE_BITS).unwrap(),
    );
    test_input_info.input_device_state.set_source_state(
        InputDeviceType::CAMERA,
        DEFAULT_CAMERA_NAME.to_string(),
        DeviceStateSource::HARDWARE,
        DeviceState::from_bits(MUTED_BITS).unwrap(),
    );

    // Write values in the store.
    {
        let mut store_lock = store.lock().await;
        store_lock.write(&test_input_info, false).await.expect("write input info in store");
    }

    get_and_check_state(&input_proxy, true, true).await;
}

// Test that a failure results in the correct epitaph.
#[fuchsia_async::run_until_stalled(test)]
async fn test_channel_failure_watch() {
    let input_proxy = create_input_test_env_with_failures(InMemoryStorageFactory::create()).await;
    let result = input_proxy.watch().await;
    assert_matches!(result, Err(ClientChannelClosed { status: Status::UNAVAILABLE, .. }));
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_media_buttons() {
    let service_registry = ServiceRegistry::create();
    let input_device_registry_service = Arc::new(Mutex::new(InputDeviceRegistryService::new()));

    let initial_event = MediaButtonsEventBuilder::new().set_volume(1).set_mic_mute(true).build();
    input_device_registry_service.lock().await.send_media_button_event(initial_event.clone());

    service_registry.lock().await.register_service(input_device_registry_service.clone());

    let service_context =
        ServiceContext::create(Some(ServiceRegistry::serve(service_registry.clone())), None);

    let (input_tx, mut input_rx) = futures::channel::mpsc::unbounded::<MediaButtonsEvent>();
    assert!(monitor_media_buttons(service_context.clone(), input_tx).await.is_ok());

    if let Some(event) = input_rx.next().await {
        assert_eq!(initial_event, event);
    }

    let second_event =
        MediaButtonsEventBuilder::new().set_volume(1).set_camera_disable(true).build();
    input_device_registry_service.lock().await.send_media_button_event(second_event.clone());

    if let Some(event) = input_rx.next().await {
        assert_eq!(second_event, event);
    }
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_device_listener_failure() {
    let service_registry = ServiceRegistry::create();
    let input_device_registry_service = Arc::new(Mutex::new(InputDeviceRegistryService::new()));
    input_device_registry_service.lock().await.set_fail(true);

    let initial_event = MediaButtonsEventBuilder::new().set_volume(1).set_mic_mute(true).build();

    input_device_registry_service.lock().await.send_media_button_event(initial_event.clone());

    service_registry.lock().await.register_service(input_device_registry_service.clone());

    let service_context =
        ServiceContext::create(Some(ServiceRegistry::serve(service_registry.clone())), None);

    let (input_tx, _input_rx) = futures::channel::mpsc::unbounded::<MediaButtonsEvent>();
    assert!(!monitor_media_buttons(service_context.clone(), input_tx).await.is_ok());
}
