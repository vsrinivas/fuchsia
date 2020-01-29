// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_TEST_FAKE_SYSMEM_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_TEST_FAKE_SYSMEM_H_

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/sys/cpp/component_context.h>

#include <ddktl/protocol/sysmem.h>

class FakeSysmem : public ddk::SysmemProtocol<FakeSysmem> {
 public:
  FakeSysmem() : SysmemProtocol(), sysmem_protocol_{&sysmem_protocol_ops_, this} {}

  ddk::SysmemProtocolClient client() { return ddk::SysmemProtocolClient(&sysmem_protocol_); }

  fake_ddk::ProtocolEntry ProtocolEntry() const {
    return {ZX_PROTOCOL_SYSMEM, *reinterpret_cast<const fake_ddk::Protocol*>(&sysmem_protocol_)};
  }

  // |ZX_PROTOCOL_SYSMEM|
  zx_status_t SysmemConnect(zx::channel /*allocator_request*/) { return ZX_OK; }

  zx_status_t SysmemRegisterHeap(uint64_t /*heap*/, zx::channel /*heap_connection*/) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t SysmemRegisterSecureMem(zx::channel /*secure_mem_connection*/) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t SysmemUnregisterSecureMem() { return ZX_ERR_NOT_SUPPORTED; }

 private:
  sysmem_protocol_t sysmem_protocol_;
};

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_TEST_FAKE_SYSMEM_H_
