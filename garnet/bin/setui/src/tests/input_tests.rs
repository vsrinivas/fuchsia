// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::agent::restore_agent,
    crate::fidl_clone::FIDLClone,
    crate::handler::device_storage::testing::*,
    crate::handler::device_storage::DeviceStorage,
    crate::input::monitor_media_buttons,
    crate::service_context::ServiceContext,
    crate::switchboard::base::{InputInfo, Microphone, SettingType},
    crate::tests::fakes::input_device_registry_service::InputDeviceRegistryService,
    crate::tests::fakes::service_registry::ServiceRegistry,
    crate::tests::test_failure_utils::create_test_env_with_failures,
    crate::EnvironmentBuilder,
    fidl::Error::ClientChannelClosed,
    fidl_fuchsia_settings::{
        InputDeviceSettings, InputMarker, InputProxy, Microphone as FidlMicrophone,
    },
    fidl_fuchsia_ui_input::MediaButtonsEvent,
    fuchsia_component::server::NestedEnvironment,
    fuchsia_zircon::Status,
    futures::lock::Mutex,
    futures::stream::StreamExt,
    matches::assert_matches,
    std::sync::Arc,
};

const DEFAULT_INPUT_INFO: InputInfo = InputInfo { microphone: Microphone { muted: false } };
const DEFAULT_MIC_STATE: bool = false;
const ENV_NAME: &str = "settings_service_input_test_environment";
const CONTEXT_ID: u64 = 0;

// Creates an environment that will fail on a get request.
async fn create_input_test_env_with_failures(
    storage_factory: Arc<Mutex<InMemoryStorageFactory>>,
) -> InputProxy {
    create_test_env_with_failures(storage_factory, ENV_NAME, SettingType::Input)
        .await
        .connect_to_service::<InputMarker>()
        .unwrap()
}

// Used to store fake services for mocking dependencies and checking input/outputs.
// To add a new fake to these tests, add here, in create_services, and then use
// in your test.
struct FakeServices {
    input_device_registry: Arc<Mutex<InputDeviceRegistryService>>,
}

// Set the software mic mute state to muted = [mic_muted].
async fn set_mic_mute(proxy: &InputProxy, mic_muted: bool) {
    let mut input_settings = InputDeviceSettings::empty();
    let mut microphone = FidlMicrophone::empty();

    microphone.muted = Some(mic_muted);
    input_settings.microphone = Some(microphone);
    proxy.set(input_settings).await.expect("set completed").expect("set successful");
}

// Switch the hardware mic state to muted = [muted].
async fn switch_hardware_mic_mute(fake_services: &FakeServices, muted: bool) {
    let buttons_event =
        MediaButtonsEvent { volume: Some(1), mic_mute: Some(muted), pause: Some(false) };
    fake_services.input_device_registry.lock().await.send_media_button_event(buttons_event.clone());
}

// Perform a watch and check that the mic mute state matches [expected_muted_state].
async fn get_and_check_mic_mute(input_proxy: &InputProxy, expected_muted_state: bool) {
    let settings = input_proxy.watch().await.expect("watch completed");
    assert_eq!(settings.microphone.unwrap().muted, Some(expected_muted_state));
}

// Gets the store from |factory| and populate it with default values.
async fn create_storage(
    factory: Arc<Mutex<InMemoryStorageFactory>>,
) -> Arc<Mutex<DeviceStorage<InputInfo>>> {
    let store = factory
        .lock()
        .await
        .get_device_storage::<InputInfo>(StorageAccessContext::Test, CONTEXT_ID);
    {
        let mut store_lock = store.lock().await;
        let input_info = DEFAULT_INPUT_INFO;
        store_lock.write(&input_info, false).await.unwrap();
    }
    store
}

// Returns a registry and input related services with which it is populated.
async fn create_services() -> (Arc<Mutex<ServiceRegistry>>, FakeServices) {
    let service_registry = ServiceRegistry::create();

    let input_device_registry_service_handle =
        Arc::new(Mutex::new(InputDeviceRegistryService::new()));
    service_registry.lock().await.register_service(input_device_registry_service_handle.clone());

    (service_registry, FakeServices { input_device_registry: input_device_registry_service_handle })
}

// Creates the environment.
async fn create_environment(
    service_registry: Arc<Mutex<ServiceRegistry>>,
) -> (NestedEnvironment, Arc<Mutex<DeviceStorage<InputInfo>>>) {
    let storage_factory = InMemoryStorageFactory::create();
    let store = create_storage(storage_factory.clone()).await;

    let env = EnvironmentBuilder::new(storage_factory)
        .service(ServiceRegistry::serve(service_registry))
        .agents(&[restore_agent::blueprint::create()])
        .settings(&[SettingType::Input])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    (env, store)
}

// Test that a watch is executed correctly.
#[fuchsia_async::run_until_stalled(test)]
async fn test_watch() {
    let (service_registry, _) = create_services().await;
    let (env, _) = create_environment(service_registry).await;
    let input_proxy = env.connect_to_service::<InputMarker>().unwrap();

    get_and_check_mic_mute(&input_proxy, false).await;
}

// Test that a set then watch is executed correctly.
#[fuchsia_async::run_until_stalled(test)]
async fn test_set_watch() {
    let (service_registry, fake_services) = create_services().await;
    let (env, _) = create_environment(service_registry).await;
    let input_proxy = env.connect_to_service::<InputMarker>().unwrap();

    set_mic_mute(&input_proxy, true).await;
    get_and_check_mic_mute(&input_proxy, true).await;

    switch_hardware_mic_mute(&fake_services, true).await;
    set_mic_mute(&input_proxy, false).await;
    get_and_check_mic_mute(&input_proxy, true).await;

    switch_hardware_mic_mute(&fake_services, false).await;
    get_and_check_mic_mute(&input_proxy, false).await;
}

// Test to ensure mic input change events are received.
#[fuchsia_async::run_until_stalled(test)]
async fn test_input() {
    let (service_registry, fake_services) = create_services().await;
    let (env, _) = create_environment(service_registry).await;
    let input_proxy = env.connect_to_service::<InputMarker>().unwrap();

    switch_hardware_mic_mute(&fake_services, true).await;
    get_and_check_mic_mute(&input_proxy, true).await;
}

// Test that when either hardware or software is muted, the service
// reports as muted.
#[fuchsia_async::run_until_stalled(test)]
async fn test_mute_combinations() {
    let (service_registry, fake_services) = create_services().await;
    let (env, _) = create_environment(service_registry).await;
    let input_proxy = env.connect_to_service::<InputMarker>().unwrap();

    // Hardware muted, software unmuted.
    switch_hardware_mic_mute(&fake_services, true).await;
    set_mic_mute(&input_proxy, false).await;
    get_and_check_mic_mute(&input_proxy, true).await;

    // Hardware muted, software muted.
    set_mic_mute(&input_proxy, true).await;
    get_and_check_mic_mute(&input_proxy, true).await;

    // Hardware unmuted, software muted.
    switch_hardware_mic_mute(&fake_services, false).await;
    get_and_check_mic_mute(&input_proxy, true).await;

    // Hardware unmuted, software unmuted.
    switch_hardware_mic_mute(&fake_services, false).await;
    set_mic_mute(&input_proxy, false).await;
    get_and_check_mic_mute(&input_proxy, false).await;
}

// Test that the input settings are restored correctly.
#[fuchsia_async::run_until_stalled(test)]
async fn test_restore() {
    let (service_registry, _) = create_services().await;
    let storage_factory = InMemoryStorageFactory::create();
    {
        let store = storage_factory
            .lock()
            .await
            .get_device_storage::<InputInfo>(StorageAccessContext::Test, CONTEXT_ID);
        let mut stored_info = DEFAULT_INPUT_INFO.clone();
        stored_info.microphone.muted = true;
        assert!(store.lock().await.write(&stored_info, false).await.is_ok());
    }

    let env = EnvironmentBuilder::new(storage_factory)
        .service(Box::new(ServiceRegistry::serve(service_registry)))
        .agents(&[restore_agent::blueprint::create()])
        .settings(&[SettingType::Input])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .expect("Environment should be created successfully");

    let input_proxy = env.connect_to_service::<InputMarker>().unwrap();
    get_and_check_mic_mute(&input_proxy, true).await;
}

// Test to ensure mic input change events are received.
#[fuchsia_async::run_until_stalled(test)]
async fn test_bringup_without_input_registry() {
    let service_registry = ServiceRegistry::create();
    let (env, _) = create_environment(service_registry).await;

    // At this point we should not crash.
    let input_proxy = env.connect_to_service::<InputMarker>().expect("Connected to service");

    get_and_check_mic_mute(&input_proxy, DEFAULT_MIC_STATE).await;
}

// Test that cloning works.
#[test]
fn test_input_info_copy() {
    let input_info = DEFAULT_INPUT_INFO;
    let copy_input_info = input_info.clone();
    assert_eq!(input_info, copy_input_info);
}

// Test that the values in the persistent store are restored at the start.
#[fuchsia_async::run_until_stalled(test)]
async fn test_persisted_values_applied_at_start() {
    let (service_registry, _) = create_services().await;
    let storage_factory = InMemoryStorageFactory::create();
    let store = create_storage(storage_factory.clone()).await;

    let test_input_info = InputInfo { microphone: Microphone { muted: true } };

    // Write values in the store.
    {
        let mut store_lock = store.lock().await;
        store_lock.write(&test_input_info, false).await.expect("write input info in store");
    }

    let env = EnvironmentBuilder::new(storage_factory)
        .service(ServiceRegistry::serve(service_registry))
        .agents(&[restore_agent::blueprint::create()])
        .settings(&[SettingType::Input])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let input_proxy = env.connect_to_service::<InputMarker>().unwrap();
    get_and_check_mic_mute(&input_proxy, true).await;
}

// Test that a failure results in the correct epitaph.
#[fuchsia_async::run_until_stalled(test)]
async fn test_channel_failure_watch() {
    let input_proxy = create_input_test_env_with_failures(InMemoryStorageFactory::create()).await;
    let result = input_proxy.watch().await;
    assert_matches!(result, Err(ClientChannelClosed { status: Status::INTERNAL, .. }));
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_media_buttons() {
    let service_registry = ServiceRegistry::create();
    let input_device_registry_service = Arc::new(Mutex::new(InputDeviceRegistryService::new()));

    let initial_event =
        MediaButtonsEvent { volume: Some(1), mic_mute: Some(true), pause: Some(false) };
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
        MediaButtonsEvent { volume: Some(0), mic_mute: Some(false), pause: Some(false) };
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

    let initial_event =
        MediaButtonsEvent { volume: Some(1), mic_mute: Some(true), pause: Some(false) };
    input_device_registry_service.lock().await.send_media_button_event(initial_event.clone());

    service_registry.lock().await.register_service(input_device_registry_service.clone());

    let service_context =
        ServiceContext::create(Some(ServiceRegistry::serve(service_registry.clone())), None);

    let (input_tx, _input_rx) = futures::channel::mpsc::unbounded::<MediaButtonsEvent>();
    assert!(!monitor_media_buttons(service_context.clone(), input_tx).await.is_ok());
}
