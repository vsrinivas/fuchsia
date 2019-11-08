// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.syzkaller.protocol.resources banjo file

#pragma once

#include <banjo/examples/syzkaller/protocol/resources.h>
#include <ddk/driver.h>
#include <ddktl/device-internal.h>
#include <lib/zx/bti.h>
#include <lib/zx/channel.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/event.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/fifo.h>
#include <lib/zx/guest.h>
#include <lib/zx/handle.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/iommu.h>
#include <lib/zx/job.h>
#include <lib/zx/log.h>
#include <lib/zx/pager.h>
#include <lib/zx/pmt.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/profile.h>
#include <lib/zx/resource.h>
#include <lib/zx/socket.h>
#include <lib/zx/thread.h>
#include <lib/zx/timer.h>
#include <lib/zx/vcpu.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "resources-internal.h"

// DDK resources-protocol support
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
//     zx_status_t ApiProducer(uint32_t options, zx::handle* out_out);
//
//     zx_status_t ApiConsumer(zx::handle h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::process* out_out);
//
//     zx_status_t ApiConsumer(zx::process h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::thread* out_out);
//
//     zx_status_t ApiConsumer(zx::thread h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::vmo* out_out);
//
//     zx_status_t ApiConsumer(zx::vmo h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::channel* out_out);
//
//     zx_status_t ApiConsumer(zx::channel h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::event* out_out);
//
//     zx_status_t ApiConsumer(zx::event h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::port* out_out);
//
//     zx_status_t ApiConsumer(zx::port h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::interrupt* out_out);
//
//     zx_status_t ApiConsumer(zx::interrupt h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::log* out_out);
//
//     zx_status_t ApiConsumer(zx::log h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::socket* out_out);
//
//     zx_status_t ApiConsumer(zx::socket h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::resource* out_out);
//
//     zx_status_t ApiConsumer(zx::resource h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::eventpair* out_out);
//
//     zx_status_t ApiConsumer(zx::eventpair h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::job* out_out);
//
//     zx_status_t ApiConsumer(zx::job h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::vmar* out_out);
//
//     zx_status_t ApiConsumer(zx::vmar h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::fifo* out_out);
//
//     zx_status_t ApiConsumer(zx::fifo h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::guest* out_out);
//
//     zx_status_t ApiConsumer(zx::guest h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::guest* out_out);
//
//     zx_status_t ApiConsumer(zx::guest h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::timer* out_out);
//
//     zx_status_t ApiConsumer(zx::timer h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::bti* out_out);
//
//     zx_status_t ApiConsumer(zx::bti h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::profile* out_out);
//
//     zx_status_t ApiConsumer(zx::profile h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::debuglog* out_out);
//
//     zx_status_t ApiConsumer(zx::debuglog h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::vcpu* out_out);
//
//     zx_status_t ApiConsumer(zx::vcpu h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::iommu* out_out);
//
//     zx_status_t ApiConsumer(zx::iommu h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::pager* out_out);
//
//     zx_status_t ApiConsumer(zx::pager h);
//
//     zx_status_t ApiProducer(uint32_t options, zx::pmt* out_out);
//
//     zx_status_t ApiConsumer(zx::pmt h);
//
//     zx_time_t ApiProducer(zx::handle h);
//
//     zx_status_t ApiProducer2(zx::handle h, zx_time_t* out_out);
//
//     zx_status_t ApiConsumer(zx_time_t t);
//
//     zx_duration_t ApiProducer(zx::handle h);
//
//     zx_status_t ApiProducer2(zx::handle h, zx_duration_t* out_out);
//
//     zx_status_t ApiConsumer(zx_duration_t d);
//
//     zx_clock_t ApiProducer(zx::handle h);
//
//     zx_status_t ApiProducer2(zx::handle h, zx_clock_t* out_out);
//
//     zx_status_t ApiConsumer(zx_clock_t cid);
//
//     zx_koid_t ApiProducer(zx::handle h);
//
//     zx_status_t ApiProducer2(zx::handle h, zx_koid_t* out_out);
//
//     zx_status_t ApiConsumer(zx_koid_t id);
//
//     zx_vaddr_t ApiProducer(zx::handle h);
//
//     zx_status_t ApiProducer2(zx::handle h, zx_vaddr_t* out_out);
//
//     zx_status_t ApiConsumer(zx_vaddr_t va);
//
//     zx_paddr_t ApiProducer(zx::handle h);
//
//     zx_status_t ApiProducer2(zx::handle h, zx_paddr_t* out_out);
//
//     zx_status_t ApiConsumer(zx_paddr_t pa);
//
//     zx_paddr32_t ApiProducer(zx::handle h);
//
//     zx_status_t ApiProducer2(zx::handle h, zx_paddr32_t* out_out);
//
//     zx_status_t ApiConsumer(zx_paddr32_t pa32);
//
//     zx_gpaddr_t ApiProducer(zx::handle h);
//
//     zx_status_t ApiProducer2(zx::handle h, zx_gpaddr_t* out_out);
//
//     zx_status_t ApiConsumer(zx_gpaddr_t gpa);
//
//     zx_off_t ApiProducer(zx::handle h);
//
//     zx_status_t ApiProducer2(zx::handle h, zx_off_t* out_out);
//
//     zx_status_t ApiConsumer(zx_off_t o);
//
//     zx_rights_t ApiProducer(zx::handle h);
//
//     zx_status_t ApiProducer2(zx::handle h, zx_rights_t* out_out);
//
//     zx_status_t ApiConsumer(zx_rights_t r);
//
//     zx_signals_t ApiProducer(zx::handle h);
//
//     zx_status_t ApiProducer2(zx::handle h, zx_signals_t* out_out);
//
//     zx_status_t ApiConsumer(zx_signals_t s);
//
//     zx_vm_option_t ApiProducer(zx::handle h);
//
//     zx_status_t ApiProducer2(zx::handle h, zx_vm_option_t* out_out);
//
//     zx_status_t ApiConsumer(zx_vm_option_t op);
//
//     ...
// };

namespace ddk {
