// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "linux_entry.h"

#include "garnet/lib/magma/src/sys_driver/magma_driver.h"
#include "garnet/lib/magma/src/sys_driver/magma_system_device.h"
#include "linux_platform_connection.h"

class LinuxDeviceProtocol {
 public:
  bool Init(uint32_t device_handle);

  static magma_status_t Query(void* context, uint64_t query_id, uint64_t* result_out) {
    switch (query_id) {
      case MAGMA_QUERY_IS_TEST_RESTART_SUPPORTED:
        *result_out = 0;
        return MAGMA_STATUS_OK;
    }
    return device_proto(context)->device()->Query(query_id, result_out).get();
  }

  static magma_status_t Connect(void* context, uint64_t client_id, void** delegate_out) {
    DASSERT(device_proto(context)->device());
    DASSERT(!device_proto(context)->platform_connection_);

    auto platform_connection = MagmaSystemDevice::Open(device_proto(context)->device(), client_id,
                                                       /*thread_profile*/ nullptr);
    if (!platform_connection)
      return DRET(MAGMA_STATUS_INTERNAL_ERROR);

    auto connection = static_cast<magma::LinuxPlatformConnection*>(platform_connection.get());
    *delegate_out = connection->delegate();

    device_proto(context)->platform_connection_ = std::move(platform_connection);

    return MAGMA_STATUS_OK;
  }

  static void Release(void* context) { delete device_proto(context); }

  static LinuxDeviceProtocol* device_proto(void* context) {
    return reinterpret_cast<LinuxDeviceProtocol*>(context);
  }

  std::shared_ptr<MagmaSystemDevice> device() { return device_; }

 private:
  std::unique_ptr<MagmaDriver> driver_;
  std::shared_ptr<MagmaSystemDevice> device_;
  std::shared_ptr<magma::PlatformConnection> platform_connection_;
};

bool LinuxDeviceProtocol::Init(uint32_t device_handle) {
  driver_ = MagmaDriver::Create();
  if (!driver_)
    return DRETF(false, "MagmaDriver::Create failed");

  device_ = driver_->CreateDevice(reinterpret_cast<void*>(static_cast<uintptr_t>(device_handle)));
  if (!device_)
    return DRETF(false, "CreateDevice failed");

  return true;
}

extern "C" {

// This is called from the client driver.
__attribute__((__visibility__("default"))) magma_status_t magma_open_device(
    uint32_t device_handle, uint32_t table_size, void* method_table_out[], void** context_out) {
  if (table_size != kMagmaDeviceOrdinalTableSize)
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Bad table_size: %d", table_size);

  auto device = std::make_unique<LinuxDeviceProtocol>();
  if (!device->Init(device_handle))
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Init failed");

  *context_out = device.release();

  method_table_out[kMagmaDeviceOrdinalQuery] = reinterpret_cast<void*>(&LinuxDeviceProtocol::Query);
  method_table_out[kMagmaDeviceOrdinalConnect] =
      reinterpret_cast<void*>(&LinuxDeviceProtocol::Connect);
  method_table_out[kMagmaDeviceOrdinalRelease] =
      reinterpret_cast<void*>(&LinuxDeviceProtocol::Release);

  return MAGMA_STATUS_OK;
}
}
