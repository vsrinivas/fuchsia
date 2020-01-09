// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use hermetic_audio_environment::prelude::*;
use hermetic_audio_environment::virtual_audio::{with_connected_device, DeviceTestAssets};

const NULL_TOKEN: u64 = zx::sys::ZX_KOID_INVALID as u64;
const INVALID_TOKEN: u64 = 33;

async fn get_gain_does_not_disconnect(token: u64) -> Result<()> {
    let env = Environment::new()?;

    let enumerator = env.connect_to_service::<AudioDeviceEnumeratorMarker>()?;
    let (actual_token, _) = enumerator.get_device_gain(token).await?;
    assert_eq!(actual_token, NULL_TOKEN);

    Ok(())
}

async fn set_gain_does_not_disconnect(token: u64) -> Result<()> {
    let env = Environment::new()?;

    let enumerator = env.connect_to_service::<AudioDeviceEnumeratorMarker>()?;
    enumerator.set_device_gain(
        token,
        &mut AudioGainInfo { gain_db: -30.0, flags: 0 },
        /*flags=*/ 0,
    )?;

    // Ensure the channel does not close.
    let _ = enumerator.get_devices().await?;

    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
async fn get_gain_null_token_does_not_disconnect_and_returns_invalid_token() -> Result<()> {
    get_gain_does_not_disconnect(NULL_TOKEN).await
}

#[fasync::run_singlethreaded]
#[test]
async fn set_gain_null_token_does_not_disconnect() -> Result<()> {
    set_gain_does_not_disconnect(NULL_TOKEN).await
}

#[fasync::run_singlethreaded]
#[test]
async fn get_gain_invalid_token_does_not_disconnect_and_returns_invalid_token() -> Result<()> {
    get_gain_does_not_disconnect(INVALID_TOKEN).await
}

#[fasync::run_singlethreaded]
#[test]
async fn set_gain_invalid_token_does_not_disconnect() -> Result<()> {
    set_gain_does_not_disconnect(INVALID_TOKEN).await
}

#[fasync::run_singlethreaded]
#[test]
async fn on_device_gain_changed_ignores_invalid_tokens_in_sets() -> Result<()> {
    let env = Environment::new()?;

    let enumerator = env.connect_to_service::<AudioDeviceEnumeratorMarker>()?;
    enumerator.set_device_gain(
        NULL_TOKEN,
        &mut AudioGainInfo { gain_db: -30.0, flags: 0 },
        /*flags=*/ 0,
    )?;

    enumerator.set_device_gain(
        INVALID_TOKEN,
        &mut AudioGainInfo { gain_db: -30.0, flags: 0 },
        /*flags=*/ 0,
    )?;

    // Synchronize with enumerator.
    let _ = enumerator.get_devices().await?;
    let mut gain_change_events = enumerator.take_event_stream().try_filter_map(move |e| {
        future::ready(Ok(AudioDeviceEnumeratorEvent::into_on_device_gain_changed(e)))
    });
    let mut on_device_gain_changed = gain_change_events.try_next().fuse();
    let mut get_devices = enumerator.get_devices().fuse();

    // Assert no event is generated for the two invalid set gains by ensuring another
    // channel message queued later resolves first.
    futures::select! {
        _ = on_device_gain_changed => {
            panic!("Got on device gain changed event for set gain with bad token")
        },
        _ = get_devices => {}
    };

    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
async fn set_input_device_gain() -> Result<()> {
    with_connected_device(|assets: DeviceTestAssets<InputProxy>| async move {
        assets.enumerator.set_device_gain(
            assets.token,
            &mut AudioGainInfo { gain_db: -30.0, flags: 0 },
            SET_AUDIO_GAIN_FLAG_GAIN_VALID | SET_AUDIO_GAIN_FLAG_MUTE_VALID,
        )?;

        let (changed_token, gain_info) = assets
            .enumerator
            .take_event_stream()
            .try_filter_map(move |e| {
                future::ready(Ok(AudioDeviceEnumeratorEvent::into_on_device_gain_changed(e)))
            })
            .try_next()
            .await?
            .expect("Waiting for a gain info event");

        assert_eq!(changed_token, assets.token);
        assert_eq!(gain_info.flags, 0);
        assert!((gain_info.gain_db - -30.0).abs() < std::f32::EPSILON);

        Ok(())
    })
    .await
}

#[fasync::run_singlethreaded]
#[test]
async fn set_output_device_gain() -> Result<()> {
    with_connected_device(|assets: DeviceTestAssets<OutputProxy>| async move {
        assets.enumerator.set_device_gain(
            assets.token,
            &mut AudioGainInfo { gain_db: -30.0, flags: 0 },
            SET_AUDIO_GAIN_FLAG_GAIN_VALID | SET_AUDIO_GAIN_FLAG_MUTE_VALID,
        )?;

        let (changed_token, gain_info) = assets
            .enumerator
            .take_event_stream()
            .try_filter_map(move |e| {
                future::ready(Ok(AudioDeviceEnumeratorEvent::into_on_device_gain_changed(e)))
            })
            .try_next()
            .await?
            .expect("Waiting for a gain info event");

        assert_eq!(changed_token, assets.token);
        assert_eq!(gain_info.flags, 0);
        assert!((gain_info.gain_db - -30.0).abs() < std::f32::EPSILON);

        Ok(())
    })
    .await
}

#[fasync::run_singlethreaded]
#[test]
async fn output_devices_initialize_to_unity_gain() -> Result<()> {
    with_connected_device(|assets: DeviceTestAssets<OutputProxy>| async move {
        let (_, gain_info) = assets.enumerator.get_device_gain(assets.token).await?;
        assert!((0. - gain_info.gain_db).abs() < std::f32::EPSILON);
        Ok(())
    })
    .await
}

#[fasync::run_singlethreaded]
#[test]
async fn input_devices_initialize_to_unity_gain() -> Result<()> {
    with_connected_device(|assets: DeviceTestAssets<InputProxy>| async move {
        let (_, gain_info) = assets.enumerator.get_device_gain(assets.token).await?;
        assert!((0. - gain_info.gain_db).abs() < std::f32::EPSILON);
        Ok(())
    })
    .await
}
