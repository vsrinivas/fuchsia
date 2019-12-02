// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::fidl_clone::FIDLClone, crate::input::monitor_mic_mute,
    crate::service_context::ServiceContext,
    crate::tests::fakes::input_device_registry_service::InputDeviceRegistryService,
    crate::tests::fakes::service_registry::ServiceRegistry,
    fidl_fuchsia_ui_input::MediaButtonsEvent, futures::stream::StreamExt, parking_lot::RwLock,
    std::sync::Arc,
};

#[fuchsia_async::run_singlethreaded(test)]
async fn test_input() {
    let service_registry = ServiceRegistry::create();
    let input_device_registry_service = Arc::new(RwLock::new(InputDeviceRegistryService::new()));

    let initial_event = MediaButtonsEvent { volume: Some(1), mic_mute: Some(true) };
    input_device_registry_service.read().send_media_button_event(initial_event.clone());

    service_registry.write().register_service(input_device_registry_service.clone());

    let service_context = Arc::new(RwLock::new(ServiceContext::new(ServiceRegistry::serve(
        service_registry.clone(),
    ))));

    let (input_tx, mut input_rx) = futures::channel::mpsc::unbounded::<MediaButtonsEvent>();
    assert!(monitor_mic_mute(service_context.clone(), input_tx).is_ok());

    if let Some(event) = input_rx.next().await {
        assert_eq!(initial_event, event);
    }

    let second_event = MediaButtonsEvent { volume: Some(0), mic_mute: Some(false) };
    input_device_registry_service.read().send_media_button_event(second_event.clone());

    if let Some(event) = input_rx.next().await {
        assert_eq!(second_event, event);
    }
}
