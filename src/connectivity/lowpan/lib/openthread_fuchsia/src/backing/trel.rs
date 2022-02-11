// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use openthread_sys::*;

impl PlatformBacking {
    fn on_trel_enable(&self, _instance: &ot::Instance) -> u16 {
        todo!("TREL platform interface not yet implemented")
    }

    fn on_trel_disable(&self, _instance: &ot::Instance) {
        todo!("TREL platform interface not yet implemented")
    }

    fn on_trel_register_service(&self, _instance: &ot::Instance, _port: u16, _txt: &[u8]) {
        todo!("TREL platform interface not yet implemented")
    }

    fn on_trel_send(&self, _instance: &ot::Instance, _payload: &[u8], _sockaddr: &ot::SockAddr) {
        todo!("TREL platform interface not yet implemented")
    }
}

#[no_mangle]
unsafe extern "C" fn otPlatTrelEnable(instance: *mut otInstance, port: *mut u16) {
    *port = PlatformBacking::on_trel_enable(
        // SAFETY: Must only be called from OpenThread thread,
        PlatformBacking::as_ref(),
        // SAFETY: `instance` must be a pointer to a valid `otInstance`,
        //         which is guaranteed by the caller.
        ot::Instance::ref_from_ot_ptr(instance).unwrap(),
    );
}

#[no_mangle]
unsafe extern "C" fn otPlatTrelDisable(instance: *mut otInstance) {
    PlatformBacking::on_trel_disable(
        // SAFETY: Must only be called from OpenThread thread,
        PlatformBacking::as_ref(),
        // SAFETY: `instance` must be a pointer to a valid `otInstance`,
        //         which is guaranteed by the caller.
        ot::Instance::ref_from_ot_ptr(instance).unwrap(),
    );
}

#[no_mangle]
unsafe extern "C" fn otPlatTrelRegisterService(
    instance: *mut otInstance,
    port: u16,
    txt_data: *const u8,
    txt_len: u8,
) {
    PlatformBacking::on_trel_register_service(
        // SAFETY: Must only be called from OpenThread thread,
        PlatformBacking::as_ref(),
        // SAFETY: `instance` must be a pointer to a valid `otInstance`,
        //         which is guaranteed by the caller.
        ot::Instance::ref_from_ot_ptr(instance).unwrap(),
        port,
        std::slice::from_raw_parts(txt_data, txt_len.into()),
    );
}

#[no_mangle]
unsafe extern "C" fn otPlatTrelSend(
    instance: *mut otInstance,
    payload_data: *const u8,
    payload_len: u16,
    dest: *const otSockAddr,
) {
    PlatformBacking::on_trel_send(
        // SAFETY: Must only be called from OpenThread thread,
        PlatformBacking::as_ref(),
        // SAFETY: `instance` must be a pointer to a valid `otInstance`,
        //         which is guaranteed by the caller.
        ot::Instance::ref_from_ot_ptr(instance).unwrap(),
        std::slice::from_raw_parts(payload_data, payload_len.into()),
        ot::SockAddr::ref_from_ot_ptr(dest).unwrap(),
    );
}
