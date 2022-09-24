// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use openthread::prelude::*;

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct otPlatformConfig {
    pub reset_rcp: bool,
}

extern "C" {
    pub fn otSysInit(a_platform_config: *mut otPlatformConfig) -> bool;
    pub fn otSysDeinit();
    pub fn platformRadioProcess(instance: *mut otsys::otInstance);
    pub fn platformInfraIfInit(infra_if_idx: ot::NetifIndex) -> i32;
    pub fn platformInfraIfOnReceiveIcmp6Msg(instance: *mut otsys::otInstance);
}
