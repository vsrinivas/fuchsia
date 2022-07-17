// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_TEST_FAKE_SYSMEM_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_TEST_FAKE_SYSMEM_H_

#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/sys/cpp/component_context.h>

class FakeSysmem : public ddk::SysmemProtocol<FakeSysmem> {
 public:
  using ConnectCallback =
      fit::function<zx_status_t(fidl::InterfaceRequest<fuchsia::sysmem::Allocator>)>;
  explicit FakeSysmem(ConnectCallback connect_callback = [](auto) { return ZX_OK; })
      : sysmem_protocol_{&sysmem_protocol_ops_, this},
        connect_callback_{std::move(connect_callback)} {}

  ddk::SysmemProtocolClient client() { return ddk::SysmemProtocolClient(&sysmem_protocol_); }

  fake_ddk::ProtocolEntry ProtocolEntry() const {
    return {ZX_PROTOCOL_SYSMEM, *reinterpret_cast<const fake_ddk::Protocol*>(&sysmem_protocol_)};
  }

  // |ZX_PROTOCOL_SYSMEM|
  zx_status_t SysmemConnect(zx::channel allocator_request) {
    fidl::InterfaceRequest<fuchsia::sysmem::Allocator> request(std::move(allocator_request));
    return connect_callback_(std::move(request));
  }

  zx_status_t SysmemRegisterHeap(uint64_t /*heap*/, zx::channel /*heap_connection*/) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t SysmemRegisterSecureMem(zx::channel /*secure_mem_connection*/) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t SysmemUnregisterSecureMem() { return ZX_ERR_NOT_SUPPORTED; }

 private:
  sysmem_protocol_t sysmem_protocol_;
  ConnectCallback connect_callback_;
};

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_TEST_FAKE_SYSMEM_H_
