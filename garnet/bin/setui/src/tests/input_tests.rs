// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingType;
use crate::handler::base::{Payload as HandlerPayload, Request};
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::ingress::fidl::Interface;
use crate::input::common::MediaButtonsEventBuilder;
use crate::input::input_device_configuration::{
    InputConfiguration, InputDeviceConfiguration, SourceState,
};
use crate::input::monitor_media_buttons;
use crate::input::types::{
    DeviceState, DeviceStateSource, InputCategory, InputDeviceType, InputInfoSources, InputState,
};
use crate::message::base::{filter, Attribution, Message, MessageType, MessengerType};
use crate::message::receptor::Receptor;
use crate::service::message::Delegate;
use crate::service::{Address, Payload, Role};
use crate::service_context::ServiceContext;
use crate::tests::fakes::input_device_registry_service::InputDeviceRegistryService;
use crate::tests::fakes::service_registry::ServiceRegistry;
use crate::tests::input_test_environment::{TestInputEnvironment, TestInputEnvironmentBuilder};
use crate::tests::test_failure_utils::create_test_env_with_failures;
use fidl::Error::ClientChannelClosed;
use fidl_fuchsia_settings::{
    DeviceState as FidlDeviceState, DeviceType, InputDeviceSettings, InputMarker, InputProxy,
    InputSettings, InputState as FidlInputState, Microphone as FidlMicrophone, ToggleStateFlags,
};
use fidl_fuchsia_ui_input::MediaButtonsEvent;
use fuchsia_async::TestExecutor;
use fuchsia_zircon::Status;
use futures::lock::Mutex;
use futures::pin_mut;
use futures::stream::StreamExt;
use futures::task::Poll;
use matches::assert_matches;
use std::collections::HashMap;
use std::sync::Arc;

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
    storage_factory: Arc<InMemoryStorageFactory>,
) -> InputProxy {
    create_test_env_with_failures(storage_factory, ENV_NAME, Interface::Input, SettingType::Input)
        .await
        .connect_to_protocol::<InputMarker>()
        .unwrap()
}

// Creates an environment with an executor for moving forward execution and
// a configuration for the input devices.
fn create_env_and_executor_with_config(
    config: InputConfiguration,
    // The pre-populated data to insert into the store before spawning
    // the environment.
    initial_input_info: Option<InputInfoSources>,
) -> (TestExecutor, TestInputEnvironment) {
    let mut executor = TestExecutor::new_with_fake_time().expect("Failed to create executor");
    let env_future = if let Some(initial_info) = initial_input_info {
        TestInputEnvironmentBuilder::new()
            .set_input_device_config(config)
            .set_starting_input_info_sources(initial_info)
            .build()
    } else {
        TestInputEnvironmentBuilder::new().set_input_device_config(config).build()
    };
    pin_mut!(env_future);
    let env = match executor.run_until_stalled(&mut env_future) {
        Poll::Ready(env) => env,
        _ => panic!("Failed to create environment"),
    };

    // Return order matters here. Executor must be in first position to avoid
    // it being torn down before env.
    (executor, env)
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
    env.input_button_service.lock().await.send_media_button_event(buttons_event.clone()).await;
}

// Switch the hardware camera disable to disabled = [disabled].
async fn switch_hardware_camera_disable(env: &TestInputEnvironment, disabled: bool) {
    let buttons_event =
        MediaButtonsEventBuilder::new().set_volume(1).set_camera_disable(disabled).build();
    env.input_button_service.lock().await.send_media_button_event(buttons_event.clone()).await;
}

// Perform a watch and watch2 and check that the mic mute state matches [expected_muted_state].
async fn get_and_check_mic_mute(input_proxy: &InputProxy, expected_muted_state: bool) {
    // TODO(fxb/65686): remove when deprecated watch is removed.
    let settings = input_proxy.watch().await.expect("watch completed");

    let settings2 = input_proxy.watch2().await.expect("watch2 completed");

    assert_eq!(settings.microphone.unwrap().muted, Some(expected_muted_state));
    verify_muted_state(&settings2, expected_muted_state, DeviceType::Microphone);
}

// Perform a watch2 and check that the camera disabled state matches [expected_camera_disabled_state].
async fn get_and_check_camera_disable(
    input_proxy: &InputProxy,
    expected_camera_disabled_state: bool,
) {
    let settings2 = input_proxy.watch2().await.expect("watch2 completed");
    verify_muted_state(&settings2, expected_camera_disabled_state, DeviceType::Camera);
}

// Creates a broker to listen in on media buttons events.
fn create_broker(
    executor: &mut TestExecutor,
    delegate: Delegate,
) -> Receptor<Payload, Address, Role> {
    let message_hub_future = delegate.create(MessengerType::Broker(Some(filter::Builder::single(
        filter::Condition::Custom(Arc::new(move |message| {
            // The first condition indicates that it is a response to a set request.
            if let Payload::Setting(HandlerPayload::Response(Ok(None))) = message.payload() {
                is_attr_onbutton(message)
            } else {
                false
            }
        })),
    ))));
    pin_mut!(message_hub_future);
    match executor.run_until_stalled(&mut message_hub_future) {
        Poll::Ready(Ok((_, receptor))) => receptor,
        _ => panic!("Could not create broker on service message hub"),
    }
}

// Waits for the media buttons receptor to receive an update, so that
// following code can be sure that the media buttons event was handled
// before continuing.
async fn wait_for_media_button_event(
    media_buttons_receptor: &mut Receptor<Payload, Address, Role>,
) {
    let event = media_buttons_receptor.next_payload().await.expect("payload should exist");

    let (payload, message_client) = event;
    message_client.propagate(payload);
}

// Returns true if the given attribution `message`'s payload is an OnButton event.
fn is_attr_onbutton(message: &Message<Payload, Address, Role>) -> bool {
    // Find the corresponding message from the message's attribution.
    let attr_msg =
        if let Attribution::Source(MessageType::Reply(message)) = message.get_attribution() {
            message
        } else {
            return false;
        };

    // Filter by the attribution message's payload. It should be an OnButton request.
    matches!(attr_msg.payload(), Payload::Setting(HandlerPayload::Request(Request::OnButton(_))))
}

// Perform a watch2 and check that the mic mute state matches [expected_mic_mute_state]
// and the camera disabled state matches [expected_camera_disabled_state].
async fn get_and_check_state(
    input_proxy: &InputProxy,
    expected_mic_mute_state: bool,
    expected_camera_disabled_state: bool,
) {
    let settings2 = input_proxy.watch2().await.expect("watch2 completed");
    verify_muted_state(&settings2, expected_camera_disabled_state, DeviceType::Camera);
    verify_muted_state(&settings2, expected_mic_mute_state, DeviceType::Microphone);
}

// Helper for checking the returned muted state for a given
// device type in the watch2 results.
fn verify_muted_state(
    settings: &InputSettings,
    expected_muted_state: bool,
    device_type: DeviceType,
) {
    assert_eq!(
        settings
            .devices
            .as_ref()
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

// Macro to run async calls with an executor, handling the Ready status
// and panicking with a custom message if it does not succeed.
#[macro_export]
macro_rules! run_code_with_executor {
    (
        $executor:ident,
        $async_code:expr,
        $panic_msg:expr
    ) => {{
        let mut future = Box::pin(async { $async_code });
        match $executor.run_until_stalled(&mut future) {
            Poll::Ready(res) => res,
            _ => panic!("{}", $panic_msg),
        }
    }};
}

// Run the provided `future` via the `executor`.
fn move_executor_forward(
    executor: &mut TestExecutor,
    future: impl futures::Future<Output = ()>,
    panic_msg: &str,
) {
    pin_mut!(future);
    match executor.run_until_stalled(&mut future) {
        Poll::Ready(res) => res,
        _ => panic!("{}", panic_msg),
    }
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
#[test]
fn test_set_watch_mic_mute() {
    let (mut executor, env) = create_env_and_executor_with_config(default_mic_config_muted(), None);
    let input_proxy = env.input_service.clone();
    let mut media_buttons_receptor = create_broker(&mut executor, env.delegate.clone());

    // SW muted, HW unmuted.
    move_executor_forward(
        &mut executor,
        async {
            set_mic_mute_input2(&input_proxy, true).await;
        },
        "Failed to set mic mute",
    );
    move_executor_forward(
        &mut executor,
        async {
            get_and_check_mic_mute(&input_proxy, true).await;
        },
        "Failed to watch mic mute",
    );

    // SW unmuted, HW muted.
    move_executor_forward(
        &mut executor,
        async {
            switch_hardware_mic_mute(&env, true).await;
            set_mic_mute_input2(&input_proxy, false).await;
            wait_for_media_button_event(&mut media_buttons_receptor).await;
        },
        "Failed to switch mic mute state",
    );
    move_executor_forward(
        &mut executor,
        async {
            get_and_check_mic_mute(&input_proxy, true).await;
        },
        "Failed to watch mic mute",
    );

    // SW unmuted, HW unmuted.
    move_executor_forward(
        &mut executor,
        async {
            switch_hardware_mic_mute(&env, false).await;
            wait_for_media_button_event(&mut media_buttons_receptor).await;
        },
        "Failed to switch hardware mic mute",
    );
    move_executor_forward(
        &mut executor,
        async {
            get_and_check_mic_mute(&input_proxy, false).await;
        },
        "Failed to watch mic mute",
    );
}

// Test that a set then watch for the camera is executed correctly.
#[test]
fn test_set_watch_camera_disable() {
    let (mut executor, env) = create_env_and_executor_with_config(default_mic_cam_config(), None);
    let input_proxy = env.input_service.clone();
    let mut media_buttons_receptor = create_broker(&mut executor, env.delegate.clone());

    // SW muted, HW unmuted.
    move_executor_forward(
        &mut executor,
        async {
            set_camera_disable(&input_proxy, true).await;
            get_and_check_camera_disable(&input_proxy, true).await;
        },
        "Failed to switch camera state",
    );

    // SW unmuted, HW muted.
    move_executor_forward(
        &mut executor,
        async {
            switch_hardware_camera_disable(&env, true).await;
            set_camera_disable(&input_proxy, false).await;
            wait_for_media_button_event(&mut media_buttons_receptor).await;
        },
        "Failed to switch camera state",
    );
    move_executor_forward(
        &mut executor,
        async {
            get_and_check_camera_disable(&input_proxy, true).await;
        },
        "Failed to get camera mute state",
    );

    // SW unmuted, HW unmuted.
    move_executor_forward(
        &mut executor,
        async {
            switch_hardware_camera_disable(&env, false).await;
            wait_for_media_button_event(&mut media_buttons_receptor).await;
        },
        "Failed to switch camera mute state",
    );
    move_executor_forward(
        &mut executor,
        async {
            get_and_check_camera_disable(&input_proxy, false).await;
        },
        "Failed to get camera mute state",
    );
}

// Test to ensure mic input change events are received.
#[test]
fn test_mic_input() {
    let (mut executor, env) = create_env_and_executor_with_config(default_mic_config(), None);
    let input_proxy = env.input_service.clone();
    let mut media_buttons_receptor = create_broker(&mut executor, env.delegate.clone());

    move_executor_forward(
        &mut executor,
        async {
            switch_hardware_mic_mute(&env, true).await;
            wait_for_media_button_event(&mut media_buttons_receptor).await;
        },
        "Failed to switch hardware mic mute",
    );
    move_executor_forward(
        &mut executor,
        async {
            get_and_check_mic_mute(&input_proxy, true).await;
        },
        "Failed to watch mic mute",
    );
}

// Test to ensure camera input change events are received.
#[test]
fn test_camera_input() {
    let (mut executor, env) = create_env_and_executor_with_config(default_mic_cam_config(), None);
    let input_proxy = env.input_service.clone();
    let mut media_buttons_receptor = create_broker(&mut executor, env.delegate.clone());

    move_executor_forward(
        &mut executor,
        async {
            get_and_check_camera_disable(&input_proxy, false).await;
            switch_hardware_camera_disable(&env, true).await;
            wait_for_media_button_event(&mut media_buttons_receptor).await;
        },
        "Failed to get and switch hardware camera mute state",
    );

    move_executor_forward(
        &mut executor,
        async {
            get_and_check_camera_disable(&input_proxy, true).await;
        },
        "Failed to get camera mute state",
    );
}

// Test to ensure camera sw state is not changed on camera3 api
// when the hw state is changed.
#[test]
fn test_camera3_hw_change() {
    let (mut executor, env) = create_env_and_executor_with_config(default_mic_cam_config(), None);
    let mut media_buttons_receptor = create_broker(&mut executor, env.delegate.clone());

    move_executor_forward(
        &mut executor,
        async {
            switch_hardware_camera_disable(&env, true).await;
            wait_for_media_button_event(&mut media_buttons_receptor).await;
        },
        "Failed to switch camera hw state",
    );

    let camera3_service_future = env.camera3_service.lock();
    pin_mut!(camera3_service_future);
    let camera3_proxy = match executor.run_until_stalled(&mut camera3_service_future) {
        Poll::Ready(service) => service,
        _ => panic!("Could not acquire camera3 service lock"),
    };

    #[allow(clippy::bool_assert_comparison)]
    {
        assert_eq!(camera3_proxy.camera_sw_muted(), false);
    }
}

// Test to ensure camera sw state is changed on camera3 api
// when the sw state is changed.
#[fuchsia_async::run_until_stalled(test)]
async fn test_camera3_sw_change() {
    let env = TestInputEnvironmentBuilder::new()
        .set_input_device_config(default_mic_cam_config())
        .build()
        .await;
    let input_proxy = env.input_service.clone();

    set_camera_disable(&input_proxy, true).await;
    assert!(env.camera3_service.lock().await.camera_sw_muted());
}

// Test that when either hardware or software is muted, the service
// reports the microphone as muted.
#[test]
fn test_mic_mute_combinations() {
    let (mut executor, env) = create_env_and_executor_with_config(default_mic_config(), None);
    let input_proxy = env.input_service.clone();
    let mut media_buttons_receptor = create_broker(&mut executor, env.delegate.clone());

    // Hardware muted, software unmuted.
    move_executor_forward(
        &mut executor,
        async {
            switch_hardware_mic_mute(&env, true).await;
            set_mic_mute_input2(&input_proxy, false).await;
            wait_for_media_button_event(&mut media_buttons_receptor).await;
        },
        "Failed to switch mic mute state",
    );
    move_executor_forward(
        &mut executor,
        async {
            get_and_check_mic_mute(&input_proxy, true).await;
        },
        "Failed to get mic mute state",
    );

    // Hardware muted, software muted.
    move_executor_forward(
        &mut executor,
        async {
            set_mic_mute_input2(&input_proxy, true).await;
            get_and_check_mic_mute(&input_proxy, true).await;
        },
        "Failed to switch and check mic mute state",
    );

    // Hardware unmuted, software muted.
    move_executor_forward(
        &mut executor,
        async {
            switch_hardware_mic_mute(&env, false).await;
            wait_for_media_button_event(&mut media_buttons_receptor).await;
        },
        "Failed to switch hardware mic mute state",
    );
    move_executor_forward(
        &mut executor,
        async {
            get_and_check_mic_mute(&input_proxy, true).await;
        },
        "Failed to watch mic mute state",
    );

    // Hardware unmuted, software unmuted.
    move_executor_forward(
        &mut executor,
        async {
            switch_hardware_mic_mute(&env, false).await;
            set_mic_mute_input2(&input_proxy, false).await;
            wait_for_media_button_event(&mut media_buttons_receptor).await;
        },
        "Failed to switch mic mute state",
    );
    move_executor_forward(
        &mut executor,
        async {
            get_and_check_mic_mute(&input_proxy, false).await;
        },
        "Failed to watch mic mute state",
    );
}

// Test that when either hardware or software is disabled, the service
// reports the camera as disabled.
#[test]
fn test_camera_disable_combinations() {
    let (mut executor, env) = create_env_and_executor_with_config(default_mic_cam_config(), None);
    let input_proxy = env.input_service.clone();
    let mut media_buttons_receptor = create_broker(&mut executor, env.delegate.clone());

    // Hardware disabled, software enabled.
    move_executor_forward(
        &mut executor,
        async {
            switch_hardware_camera_disable(&env, true).await;
            set_camera_disable(&input_proxy, false).await;
            wait_for_media_button_event(&mut media_buttons_receptor).await;
        },
        "Failed to switch camera mute state",
    );
    move_executor_forward(
        &mut executor,
        async {
            get_and_check_camera_disable(&input_proxy, true).await;
        },
        "Failed to get camera mute state",
    );

    // Hardware disabled, software disabled
    move_executor_forward(
        &mut executor,
        async {
            set_camera_disable(&input_proxy, true).await;
            get_and_check_camera_disable(&input_proxy, true).await;
        },
        "Failed to switch camera mute state",
    );

    // Hardware enabled, software disabled.
    move_executor_forward(
        &mut executor,
        async {
            switch_hardware_camera_disable(&env, false).await;
            wait_for_media_button_event(&mut media_buttons_receptor).await;
        },
        "Failed to switch hardware camera mute state",
    );
    move_executor_forward(
        &mut executor,
        async {
            get_and_check_camera_disable(&input_proxy, true).await;
        },
        "Failed to watch camera mute state",
    );

    // Hardware enabled, software enabled.
    move_executor_forward(
        &mut executor,
        async {
            switch_hardware_camera_disable(&env, false).await;
            set_camera_disable(&input_proxy, false).await;
            wait_for_media_button_event(&mut media_buttons_receptor).await;
        },
        "Failed to switch camera mute state",
    );
    move_executor_forward(
        &mut executor,
        async {
            get_and_check_camera_disable(&input_proxy, false).await;
        },
        "Failed to watch camera mute state",
    );
}

// Test that the input settings are restored correctly.
#[fuchsia_async::run_until_stalled(test)]
async fn test_restore() {
    let mut stored_info = create_default_input_info().clone();
    stored_info.input_device_state = default_mic_cam_config_cam_disabled().into();
    let env = TestInputEnvironmentBuilder::new()
        .set_starting_input_info_sources(stored_info)
        .set_input_device_config(default_mic_config_muted())
        .build()
        .await;

    get_and_check_state(&env.input_service, true, true).await;
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
    let mut test_input_info = InputInfoSources {
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
    let env = TestInputEnvironmentBuilder::new()
        .set_starting_input_info_sources(test_input_info)
        .set_input_device_config(default_mic_cam_config())
        .build()
        .await;

    get_and_check_state(&env.input_service, true, true).await;
}

// Test that a failure results in the correct epitaph.
#[fuchsia_async::run_until_stalled(test)]
async fn test_channel_failure_watch() {
    let input_proxy =
        create_input_test_env_with_failures(Arc::new(InMemoryStorageFactory::new())).await;
    let result = input_proxy.watch().await;
    assert_matches!(result, Err(ClientChannelClosed { status: Status::UNAVAILABLE, .. }));
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_media_buttons() {
    let service_registry = ServiceRegistry::create();
    let input_device_registry_service = Arc::new(Mutex::new(InputDeviceRegistryService::new()));

    let initial_event = MediaButtonsEventBuilder::new().set_volume(1).set_mic_mute(true).build();
    input_device_registry_service.lock().await.send_media_button_event(initial_event.clone()).await;

    service_registry.lock().await.register_service(input_device_registry_service.clone());

    let service_context =
        Arc::new(ServiceContext::new(Some(ServiceRegistry::serve(service_registry.clone())), None));

    let (input_tx, mut input_rx) = futures::channel::mpsc::unbounded::<MediaButtonsEvent>();
    assert!(monitor_media_buttons(service_context, input_tx).await.is_ok());

    // Listener receives an event immediately upon listening.
    if let Some(event) = input_rx.next().await {
        assert_eq!(initial_event, event);
    }

    // Disable the camera.
    let second_event =
        MediaButtonsEventBuilder::new().set_volume(1).set_camera_disable(true).build();
    input_device_registry_service.lock().await.send_media_button_event(second_event.clone()).await;

    // Listener receives the camera disable event.
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

    input_device_registry_service.lock().await.send_media_button_event(initial_event.clone()).await;

    service_registry.lock().await.register_service(input_device_registry_service.clone());

    let service_context =
        Arc::new(ServiceContext::new(Some(ServiceRegistry::serve(service_registry.clone())), None));

    let (input_tx, _input_rx) = futures::channel::mpsc::unbounded::<MediaButtonsEvent>();
    #[allow(clippy::bool_assert_comparison)]
    {
        assert_eq!(monitor_media_buttons(service_context, input_tx).await.is_ok(), false);
    }
}
