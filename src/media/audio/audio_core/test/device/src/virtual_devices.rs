// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude::*;
use futures::Future;

pub trait VirtualAudioDevice: Sized {
    fn connect(env: &Environment) -> Result<Self>;
    fn set_unique_id(&self, id: [u8; 16]) -> Result<()>;
    fn add(&self) -> Result<()>;
    fn remove(&self) -> Result<()>;
}

macro_rules! impl_virtual_audio_device {
    ($device:ty) => {
        impl VirtualAudioDevice for $device {
            fn connect(env: &Environment) -> Result<Self> {
                use fidl::endpoints::Proxy;
                let proxy = env.connect_to_service::<<Self as Proxy>::Service>()?;
                proxy.set_plug_properties(
                    /*plug_time=*/ zx::Time::get(zx::ClockId::Monotonic).into_nanos(),
                    /*plugged=*/ false,
                    /*hardwired=*/ false,
                    /*can_notify=*/ true,
                )?;
                proxy.set_gain_properties(
                    /*min_gain_db=*/ -160., /*max_gain_db=*/ 1.,
                    /*gain_step_db=*/ 1., /*cur_gain_db=*/ -6., /*can_mute=*/ true,
                    /*cur_mute=*/ false, /*can_agc=*/ false, /*cur_agc=*/ false,
                )?;
                Ok(proxy)
            }

            fn set_unique_id(&self, mut id: [u8; 16]) -> Result<()> {
                Ok(self.set_unique_id(&mut id)?)
            }

            fn add(&self) -> Result<()> {
                Ok(self.add()?)
            }

            fn remove(&self) -> Result<()> {
                Ok(self.remove()?)
            }
        }
    };
}

impl_virtual_audio_device!(InputProxy);
impl_virtual_audio_device!(OutputProxy);

pub struct DeviceTestAssets<D> {
    pub env: Environment,
    pub enumerator: AudioDeviceEnumeratorProxy,
    pub device: D,
    pub token: u64,
}

pub async fn with_connected_device<D: VirtualAudioDevice, F: Future<Output = Result<()>>>(
    f: impl FnOnce(DeviceTestAssets<D>) -> F,
) -> Result<()> {
    let env = Environment::new()?;

    let enumerator = env.connect_to_service::<AudioDeviceEnumeratorMarker>()?;

    println!("Connecting to virtual device");

    let device = D::connect(&env)?;
    const EXPECTED_ID: [u8; 16] = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];
    device.set_unique_id(EXPECTED_ID.clone())?;

    println!("Adding virtual device to AudioDeviceEnumerator");
    device.add()?;

    println!("Waiting for add event from AudioDeviceEnumerator");
    // Synchronize device add with enumerator.
    enumerator
        .take_event_stream()
        .try_filter_map(move |e| {
            future::ready(Ok(AudioDeviceEnumeratorEvent::into_on_device_added(e)))
        })
        .try_next()
        .await?;

    println!("Getting devices list from AudioDeviceEnumerator");
    let devices = enumerator.get_devices().await?;
    assert_eq!(devices.len(), 1);

    let expected_id = hex::encode(EXPECTED_ID);
    let device_info = devices
        .into_iter()
        .find(|d| d.unique_id == expected_id)
        .expect("Unwrapping device info for added device");

    println!("Delegating to test function");
    f(DeviceTestAssets { env, enumerator, device, token: device_info.token_id }).await
}

#[fasync::run_singlethreaded]
#[test]
async fn input_device_add() -> Result<()> {
    with_connected_device(|_assets: DeviceTestAssets<InputProxy>| future::ready(Ok(()))).await
}

#[fasync::run_singlethreaded]
#[test]
async fn input_device_remove() -> Result<()> {
    with_connected_device(|assets: DeviceTestAssets<InputProxy>| {
        async move {
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
async fn input_device_remove_plugged() -> Result<()> {
    with_connected_device(|assets: DeviceTestAssets<InputProxy>| {
        async move {
            assets.device.change_plug_state(
                zx::Time::get(zx::ClockId::Monotonic).into_nanos(),
                /*plugged=*/ true,
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
async fn output_device_remove() -> Result<()> {
    with_connected_device(|assets: DeviceTestAssets<OutputProxy>| {
        async move {
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
async fn output_device_remove_plugged() -> Result<()> {
    with_connected_device(|assets: DeviceTestAssets<OutputProxy>| {
        async move {
            assets.device.change_plug_state(
                zx::Time::get(zx::ClockId::Monotonic).into_nanos(),
                /*plugged=*/ true,
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
async fn plug_unplug_durability() -> Result<()> {
    let env = Environment::new()?;

    let enumerator = env.connect_to_service::<AudioDeviceEnumeratorMarker>()?;

    let mut device_adds = enumerator.take_event_stream().try_filter_map(move |e| {
        future::ready(Ok(AudioDeviceEnumeratorEvent::into_on_device_added(e)))
    });

    for _ in 0..20 {
        let mut expected_id1_bytes: [u8; 16] =
            [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];
        let device1 = InputProxy::connect(&env)?;
        device1.set_unique_id(&mut expected_id1_bytes)?;
        device1.add()?;

        let mut expected_id2_bytes: [u8; 16] =
            [1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];
        let device2 = InputProxy::connect(&env)?;
        device2.set_unique_id(&mut expected_id2_bytes)?;
        device2.add()?;

        // Synchronize device adds with enumerator.
        device_adds.try_next().await?;
        device_adds.try_next().await?;

        device1.change_plug_state(
            zx::Time::get(zx::ClockId::Monotonic).into_nanos(),
            /*plugged=*/ true,
        )?;
        drop(device1);

        device2.change_plug_state(
            zx::Time::get(zx::ClockId::Monotonic).into_nanos(),
            /*plugged=*/ true,
        )?;
        drop(device2);
    }

    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
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
