// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    log::error,
    wlan_mlme::{
        buffer::BufferProvider,
        client::{ClientConfig, ClientMlme},
        device::DeviceInterface,
        Mlme, MlmeHandle,
    },
    wlan_span::CSpan,
};

#[no_mangle]
pub extern "C" fn start_client_mlme(
    config: ClientConfig,
    device: DeviceInterface,
    buf_provider: BufferProvider,
) -> *mut MlmeHandle {
    match Mlme::<ClientMlme>::start(config, device, buf_provider) {
        Ok(client_mlme) => Box::into_raw(Box::new(client_mlme)),
        Err(e) => {
            error!("Failed to start Client MLME: {}", e);
            std::ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn start_client_mlme_for_test(
    config: ClientConfig,
    device: DeviceInterface,
    buf_provider: BufferProvider,
) -> *mut MlmeHandle {
    Box::into_raw(Box::new(Mlme::<ClientMlme>::start_test(config, device, buf_provider)))
}

#[no_mangle]
pub extern "C" fn stop_client_mlme(mlme: &mut MlmeHandle) {
    mlme.stop();
}

#[no_mangle]
pub extern "C" fn delete_client_mlme(mlme: *mut MlmeHandle) {
    if !mlme.is_null() {
        let mlme = unsafe { Box::from_raw(mlme) };
        mlme.delete();
    }
}

#[no_mangle]
pub unsafe extern "C" fn client_mlme_queue_eth_frame_tx(mlme: &mut MlmeHandle, frame: CSpan<'_>) {
    let _ = mlme.queue_eth_frame_tx(frame.into());
}

#[no_mangle]
pub unsafe extern "C" fn client_mlme_advance_fake_time(mlme: &mut MlmeHandle, nanos: i64) {
    mlme.advance_fake_time(nanos);
}

#[no_mangle]
pub unsafe extern "C" fn client_mlme_run_until_stalled(mlme: &mut MlmeHandle) {
    mlme.run_until_stalled();
}
