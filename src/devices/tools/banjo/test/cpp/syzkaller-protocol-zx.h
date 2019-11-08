// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.protocol.zx banjo file

#pragma once

#include <banjo/examples/syzkaller/protocol/zx.h>
#include <ddk/driver.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "zx-internal.h"

// DDK zx-protocol support
//
// :: Proxies ::
//
// ddk::ApiProtocolClient is a simple wrapper around
// api_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::ApiProtocol is a mixin class that simplifies writing DDK drivers
// that implement the api protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_API device.
// class ApiDevice;
// using ApiDeviceType = ddk::Device<ApiDevice, /* ddk mixins */>;
//
// class ApiDevice : public ApiDeviceType,
//                      public ddk::ApiProtocol<ApiDevice> {
//   public:
//     ApiDevice(zx_device_t* parent)
//         : ApiDeviceType(parent) {}
//
//     zx_status_t ApiStatus(zx_status_t st);
//
//     zx_time_t ApiTime(zx_time_t t);
//
//     zx_duration_t ApiDuration(zx_duration_t d);
//
//     zx_clock_t ApiClock(zx_clock_t cid);
//
//     zx_koid_t ApiKoid(zx_koid_t id);
//
//     zx_vaddr_t ApiVaddr(zx_vaddr_t va);
//
//     zx_paddr_t ApiPaddr(zx_paddr_t pa);
//
//     zx_paddr32_t ApiPaddr32(zx_paddr32_t pa32);
//
//     zx_gpaddr_t ApiGpaddr(zx_gpaddr_t gpa);
//
//     zx_off_t ApiOff(zx_off_t o);
//
//     zx_rights_t ApiRights(zx_rights_t r);
//
//     zx_signals_t ApiSignals(zx_signals_t sig);
//
//     zx_vm_option_t ApiVmOption(zx_vm_option_t op);
//
//     ...
// };

namespace ddk {
