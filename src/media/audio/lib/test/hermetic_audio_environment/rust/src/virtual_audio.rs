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
