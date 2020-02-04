// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>

#include <string>

#include "linux_entry.h"
#include "linux_platform_connection_client.h"
#include "magma_util/macros.h"
#include "platform_connection.h"
#include "platform_device_client.h"

namespace magma {

class LinuxPlatformDeviceClient : public PlatformDeviceClient {
 public:
  LinuxPlatformDeviceClient(void* lib_handle, uint32_t device_handle,
                            magma_open_device_t magma_open_device);

  ~LinuxPlatformDeviceClient();

  void* context() { return context_; }

  bool Query(uint64_t query_id, uint64_t* result_out);

  bool QueryReturnsBuffer(uint64_t query_id, magma_handle_t* buffer_out) {
    return DRETF(false, "LinuxPlatformDeviceClient::QueryReturnsBuffer not implemented");
  }

  std::unique_ptr<PlatformConnectionClient> Connect();

 private:
  void* lib_handle_ = nullptr;
  void* method_table_[kMagmaDeviceOrdinalTableSize];
  void* context_ = nullptr;
};

LinuxPlatformDeviceClient::LinuxPlatformDeviceClient(void* lib_handle, uint32_t device_handle,
                                                     magma_open_device_t magma_open_device)
    : lib_handle_(lib_handle) {
  magma_status_t status =
      magma_open_device(device_handle, kMagmaDeviceOrdinalTableSize, method_table_, &context_);
  if (status != MAGMA_STATUS_OK) {
    DMESSAGE("magma_open_device failed: %d", status);
    context_ = nullptr;
  }
}

LinuxPlatformDeviceClient::~LinuxPlatformDeviceClient() {
  if (context_) {
    auto release =
        reinterpret_cast<magma_device_release_t>(method_table_[kMagmaDeviceOrdinalRelease]);
    release(context_);
  }
  if (lib_handle_) {
    dlclose(lib_handle_);
  }
}

bool LinuxPlatformDeviceClient::Query(uint64_t query_id, uint64_t* result_out) {
  if (!context_)
    return DRETF(false, "No context");
  auto query = reinterpret_cast<magma_device_query_t>(method_table_[kMagmaDeviceOrdinalQuery]);
  magma_status_t status = query(context_, query_id, result_out);
  if (status != MAGMA_STATUS_OK)
    return DRETF(false, "query failed: %d", status);
  return true;
}

std::unique_ptr<PlatformConnectionClient> LinuxPlatformDeviceClient::Connect() {
  if (!context_)
    return DRETP(nullptr, "No context");
  auto connect =
      reinterpret_cast<magma_device_connect_t>(method_table_[kMagmaDeviceOrdinalConnect]);

  void* delegate;
  uint64_t client_id = 0;
  magma_status_t status = connect(context_, client_id, &delegate);
  if (status != MAGMA_STATUS_OK)
    return DRETP(nullptr, "connect failed: %d", status);

  return std::make_unique<LinuxPlatformConnectionClient>(
      reinterpret_cast<PlatformConnection::Delegate*>(delegate));
}

// static
std::unique_ptr<PlatformDeviceClient> PlatformDeviceClient::Create(uint32_t device_handle) {
  const char* kLibMsdName = "libmsd.so";

  void* lib_handle = dlopen(kLibMsdName, RTLD_NOW);
  char* error_str = dlerror();
  if (!lib_handle)
    return DRETP(nullptr, "Failed to open: %s: %s", kLibMsdName, error_str);

  auto magma_open_device =
      reinterpret_cast<magma_open_device_t>(dlsym(lib_handle, "magma_open_device"));
  error_str = dlerror();

  if (!magma_open_device) {
    assert(error_str);
    // Copy the error string before we dlclose
    std::string print_str = error_str;
    dlclose(lib_handle);
    return DRETP(nullptr, "Failed to find magma_open_device: %s", print_str.c_str());
  }

  auto client =
      std::make_unique<LinuxPlatformDeviceClient>(lib_handle, device_handle, magma_open_device);
  if (!client->context())
    return DRETP(nullptr, "No valid context");

  return client;
}

}  // namespace magma
