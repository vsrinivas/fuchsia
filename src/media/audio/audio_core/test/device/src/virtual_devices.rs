// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use hermetic_audio_environment::prelude::*;
use hermetic_audio_environment::virtual_audio::{
    with_connected_device, DeviceTestAssets, VirtualAudioDevice,
};

#[fasync::run_singlethreaded]
#[test]
async fn input_device_add() -> Result<()> {
    with_connected_device(|_assets: DeviceTestAssets<InputProxy>| future::ready(Ok(()))).await
}

#[fasync::run_singlethreaded]
#[test]
async fn input_device_remove() -> Result<()> {
    with_connected_device(|assets: DeviceTestAssets<InputProxy>| async move {
        assets.device.remove()?;
        assert_matches!(
            assets
                .enumerator
                .take_event_stream()
                .try_next()
                .await?
                .map(AudioDeviceEnumeratorEvent::into_on_device_removed),
            Some(_)
        );

        Ok(())
    })
    .await
}

#[fasync::run_singlethreaded]
#[test]
async fn input_device_remove_unplugged() -> Result<()> {
    with_connected_device(|assets: DeviceTestAssets<InputProxy>| {
        async move {
            assets.device.change_plug_state(
                zx::Time::get(zx::ClockId::Monotonic).into_nanos(),
                /*plugged=*/ false,
            )?;

            assets.device.remove()?;
            assert_matches!(
                assets
                    .enumerator
                    .take_event_stream()
                    .try_next()
                    .await?
                    .map(AudioDeviceEnumeratorEvent::into_on_device_removed),
                Some(_)
            );

            Ok(())
        }
    })
    .await
}

#[fasync::run_singlethreaded]
#[test]
async fn output_device_add() -> Result<()> {
    with_connected_device(|_assets: DeviceTestAssets<OutputProxy>| future::ready(Ok(()))).await
}

#[fasync::run_singlethreaded]
#[test]
async fn output_device_initialize_gain() -> Result<()> {
    let env = Environment::new()?;

    let device = OutputProxy::connect(&env)?;
    device.add()?;

    let (mute, _, gain_db) = device
        .take_event_stream()
        .try_filter_map(move |e| future::ready(Ok(OutputEvent::into_on_set_gain(e))))
        .try_next()
        .await?
        .expect("Waiting for a set gain event");
    assert!(!mute);
    assert_eq!(gain_db, 0.0);

    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
async fn output_device_remove() -> Result<()> {
    with_connected_device(|assets: DeviceTestAssets<OutputProxy>| async move {
        assets.device.remove()?;
        assert_matches!(
            assets
                .enumerator
                .take_event_stream()
                .try_next()
                .await?
                .map(AudioDeviceEnumeratorEvent::into_on_device_removed),
            Some(_)
        );

        Ok(())
    })
    .await
}

#[fasync::run_singlethreaded]
#[test]
async fn output_device_remove_unplugged() -> Result<()> {
    with_connected_device(|assets: DeviceTestAssets<OutputProxy>| {
        async move {
            assets.device.change_plug_state(
                zx::Time::get(zx::ClockId::Monotonic).into_nanos(),
                /*plugged=*/ false,
            )?;

            assets.device.remove()?;
            assert_matches!(
                assets
                    .enumerator
                    .take_event_stream()
                    .try_next()
                    .await?
                    .map(AudioDeviceEnumeratorEvent::into_on_device_removed),
                Some(_)
            );

            Ok(())
        }
    })
    .await
}

#[fasync::run_singlethreaded]
#[test]
// TODO(51204): Fix flake
#[ignore]
async fn plug_unplug_durability() -> Result<()> {
    let env = Environment::new()?;

    // Two enumerators, so we can create separate event streams for Adds and DefaultChanges
    let mut enumerator_events =
        env.connect_to_service::<AudioDeviceEnumeratorMarker>()?.take_event_stream();

    // Configure and add an input device that can notify audio_core when un/plugged
    let mut expected_id1_bytes: [u8; 16] = [0, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];
    let device1 = InputProxy::connect(&env)?;
    device1.set_unique_id(&mut expected_id1_bytes)?;
    device1.set_plug_properties(
        /*plug_change_time=*/ 0, /*plugged=*/ true, /*hardwired=*/ false,
        /*can_notify=*/ true,
    )?;

    // Wait for device1 to be added and signaled.
    device1.add()?;
    let device1_info = enumerator_events
        .try_next()
        .await?
        .map(AudioDeviceEnumeratorEvent::into_on_device_added)
        .expect("Unexpected end of event stream encountered")
        .expect("Unexpected event received");

    // Wait for device1 to become the new default.
    let (old_token, new_token) = enumerator_events
        .try_next()
        .await?
        .map(AudioDeviceEnumeratorEvent::into_on_default_device_changed)
        .expect("Unexpected end of event stream encountered")
        .expect("Unexpected event received");
    assert_eq!(old_token, 0);
    assert_eq!(new_token, device1_info.token_id);

    // Configure/add another (unique) input device that notifies audio_core when un/plugged
    let mut expected_id2_bytes: [u8; 16] = [1, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];
    let device2 = InputProxy::connect(&env)?;
    device2.set_unique_id(&mut expected_id2_bytes)?;
    device2.set_plug_properties(0, true, false, true)?;

    // Wait for device2 to be added and signaled.
    device2.add()?;
    let device2_info = enumerator_events
        .try_next()
        .await?
        .map(AudioDeviceEnumeratorEvent::into_on_device_added)
        .expect("Unexpected end of event stream encountered")
        .expect("Unexpected event received");

    // Wait for device2 to become the new default.
    let (old_token, new_token) = enumerator_events
        .try_next()
        .await?
        .map(AudioDeviceEnumeratorEvent::into_on_default_device_changed)
        .expect("Unexpected end of event stream encountered")
        .expect("Unexpected event received");
    assert_eq!(old_token, device1_info.token_id);
    assert_eq!(new_token, device2_info.token_id);

    // Repeat the plug-unplug cycle 20 times. If no notification, hang. If disconnect, error.
    for _ in 0..20 {
        // After device2 unplug, wait for signal that default has moved device2 => device1.
        device2.change_plug_state(zx::Time::get(zx::ClockId::Monotonic).into_nanos(), false)?;
        let (old_token, new_token) = enumerator_events
            .try_next()
            .await?
            .map(AudioDeviceEnumeratorEvent::into_on_default_device_changed)
            .expect("Unexpected end of event stream encountered")
            .expect("Unexpected event received");
        assert_eq!(old_token, device2_info.token_id);
        assert_eq!(new_token, device1_info.token_id);

        // After device1 unplug, wait for signal that default has moved device1 => 0.
        device1.change_plug_state(zx::Time::get(zx::ClockId::Monotonic).into_nanos(), false)?;
        let (old_token, new_token) = enumerator_events
            .try_next()
            .await?
            .map(AudioDeviceEnumeratorEvent::into_on_default_device_changed)
            .expect("Unexpected end of event stream encountered")
            .expect("Unexpected event received");
        assert_eq!(old_token, device1_info.token_id);
        assert_eq!(new_token, 0);

        // After device1 plug, wait for signal that default has moved 0 => device1.
        device1.change_plug_state(zx::Time::get(zx::ClockId::Monotonic).into_nanos(), true)?;
        let (old_token, new_token) = enumerator_events
            .try_next()
            .await?
            .map(AudioDeviceEnumeratorEvent::into_on_default_device_changed)
            .expect("Unexpected end of event stream encountered")
            .expect("Unexpected event received");
        assert_eq!(old_token, 0);
        assert_eq!(new_token, device1_info.token_id);

        // After device2 plug, wait for signal that default has moved device1 => device2.
        device2.change_plug_state(zx::Time::get(zx::ClockId::Monotonic).into_nanos(), true)?;
        let (old_token, new_token) = enumerator_events
            .try_next()
            .await?
            .map(AudioDeviceEnumeratorEvent::into_on_default_device_changed)
            .expect("Unexpected end of event stream encountered")
            .expect("Unexpected event received");
        assert_eq!(old_token, device1_info.token_id);
        assert_eq!(new_token, device2_info.token_id);
    }

    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
async fn default_change_upon_remove_input() -> Result<()> {
    let env = Environment::new()?;

    // Enumerator for on_device_added event stream.
    let enumerator = env.connect_to_service::<AudioDeviceEnumeratorMarker>()?;
    let mut device_adds = enumerator.take_event_stream().try_filter_map(move |e| {
        future::ready(Ok(AudioDeviceEnumeratorEvent::into_on_device_added(e)))
    });

    // Enumerator for on_default_device_changed event stream.
    let enumerator2 = env.connect_to_service::<AudioDeviceEnumeratorMarker>()?;
    let mut default_changes = enumerator2.take_event_stream().try_filter_map(move |e| {
        future::ready(Ok(AudioDeviceEnumeratorEvent::into_on_default_device_changed(e)))
    });

    // Configure and add a unique input device that can notify audio_core when un/plugged
    let mut expected_id_bytes: [u8; 16] = [2, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];
    let virtual_device = InputProxy::connect(&env)?;
    virtual_device.set_unique_id(&mut expected_id_bytes)?;
    let now = zx::Time::get(zx::ClockId::Monotonic).into_nanos();
    virtual_device.set_plug_properties(now, true, false, true)?;

    // Wait for device to be added and signaled.
    virtual_device.add()?;
    let device = device_adds.try_next().await?.expect("Waiting for DeviceAdded");
    assert!(device.is_input);
    let expected_id = hex::encode(expected_id_bytes);
    assert_eq!(device.unique_id, expected_id);
    assert!(!device.is_default);

    // Wait for device to become the new default.
    let (old_default_token, new_default_token) =
        default_changes.try_next().await?.expect("Waiting for DefaultChanged");
    assert_eq!(old_default_token, 0);
    assert_eq!(new_default_token, device.token_id);

    drop(virtual_device);

    // After device drop, wait for signal that default has moved device => 0.
    let (old_default_token, new_default_token) =
        default_changes.try_next().await?.expect("Waiting for DefaultChanged #2");
    assert_eq!(old_default_token, device.token_id);
    assert_eq!(new_default_token, 0);

    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
async fn default_change_upon_remove_output() -> Result<()> {
    let env = Environment::new()?;

    // Enumerator for on_device_added event stream.
    let enumerator = env.connect_to_service::<AudioDeviceEnumeratorMarker>()?;
    let mut device_adds = enumerator.take_event_stream().try_filter_map(move |e| {
        future::ready(Ok(AudioDeviceEnumeratorEvent::into_on_device_added(e)))
    });

    // Enumerator for on_default_device_changed event stream.
    let enumerator2 = env.connect_to_service::<AudioDeviceEnumeratorMarker>()?;
    let mut default_changes = enumerator2.take_event_stream().try_filter_map(move |e| {
        future::ready(Ok(AudioDeviceEnumeratorEvent::into_on_default_device_changed(e)))
    });

    // Configure and add a unique output device that can notify audio_core when un/plugged
    let mut expected_id_bytes: [u8; 16] = [3, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];
    let virtual_device = OutputProxy::connect(&env)?;
    virtual_device.set_unique_id(&mut expected_id_bytes)?;
    let now = zx::Time::get(zx::ClockId::Monotonic).into_nanos();
    virtual_device.set_plug_properties(now, true, false, true)?;

    virtual_device.add()?;
    // Wait for device to be added and signaled.
    let device = device_adds.try_next().await?.expect("Waiting for DeviceAdded");
    assert!(!device.is_input);
    let expected_id = hex::encode(expected_id_bytes);
    assert_eq!(device.unique_id, expected_id);
    assert!(!device.is_default);

    // Wait for device to become the new default.
    let (old_default_token, new_default_token) =
        default_changes.try_next().await?.expect("Waiting for DefaultChanged");
    assert_eq!(old_default_token, 0);
    assert_eq!(new_default_token, device.token_id);

    drop(virtual_device);

    // After device drop, wait for signal that default has moved device => 0.
    let (old_default_token, new_default_token) =
        default_changes.try_next().await?.expect("Waiting for DefaultChanged #2");
    assert_eq!(old_default_token, device.token_id);
    assert_eq!(new_default_token, 0);

    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
#[ignore]
async fn many_devices() -> Result<()> {
    use std::collections::{HashMap, HashSet};

    let env = Environment::new()?;

    let enumerator = env.connect_to_service::<AudioDeviceEnumeratorMarker>()?;

    let mut device_tokens = HashSet::new();
    let mut devices = HashMap::new();
    for i in 0..100 {
        let device = InputProxy::connect(&env)?;
        let expected_id_bytes: [u8; 16] = [i, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];
        device.set_unique_id(&mut expected_id_bytes.clone())?;
        device.add()?;
        device.change_plug_state(
            zx::Time::get(zx::ClockId::Monotonic).into_nanos(),
            /*plugged=*/ true,
        )?;

        // Synchronize device add with enumerator.
        enumerator
            .take_event_stream()
            .try_filter_map(move |e| {
                future::ready(Ok(AudioDeviceEnumeratorEvent::into_on_device_added(e)))
            })
            .try_next()
            .await?;

        let enumerated_devices = enumerator.get_devices().await?;

        let expected_id = hex::encode(expected_id_bytes);
        let device_info = enumerated_devices
            .into_iter()
            .find(|d| d.unique_id == expected_id)
            .expect("Unwrapping device info for added device");

        device_tokens.insert(device_info.token_id);
        devices.insert(i, device);
    }

    drop(devices);
    let mut events = enumerator.take_event_stream().try_filter_map(move |e| {
        future::ready(Ok(AudioDeviceEnumeratorEvent::into_on_device_removed(e)))
    });

    while let Some(removed_device_token) = events.try_next().await? {
        let was_removed = device_tokens.remove(&removed_device_token);
        assert!(was_removed);
        if device_tokens.is_empty() {
            break;
        }
    }

    assert!(device_tokens.is_empty());

    Ok(())
}
