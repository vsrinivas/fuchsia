// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_SRC_SYS_DRIVER_MAGMA_SYSTEM_DEVICE_H_
#define SRC_GRAPHICS_LIB_MAGMA_SRC_SYS_DRIVER_MAGMA_SYSTEM_DEVICE_H_

#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "magma_system_connection.h"
#include "msd.h"
#include "platform_connection.h"
#include "platform_event.h"
#include "platform_handle.h"
#include "platform_thread.h"

using msd_device_unique_ptr_t = std::unique_ptr<msd_device_t, decltype(&msd_device_destroy)>;

static inline msd_device_unique_ptr_t MsdDeviceUniquePtr(msd_device_t* msd_dev) {
  return msd_device_unique_ptr_t(msd_dev, &msd_device_destroy);
}

class MagmaSystemBuffer;
class MagmaSystemSemaphore;

class MagmaSystemDevice {
 public:
  static std::unique_ptr<MagmaSystemDevice> Create(msd_device_unique_ptr_t msd_device) {
    return std::make_unique<MagmaSystemDevice>(std::move(msd_device));
  }

  MagmaSystemDevice(msd_device_unique_ptr_t msd_dev) : msd_dev_(std::move(msd_dev)) {
    connection_map_ = std::make_unique<std::unordered_map<std::thread::id, Connection>>();
  }

  // Opens a connection to the device. On success, returns the connection handle
  // to be passed to the client. A scheduler profile may be passed to
  // |thread_profile| to apply to the connection handler or nullptr to use the
  // default profile.
  static std::shared_ptr<magma::PlatformConnection> Open(
      std::shared_ptr<MagmaSystemDevice>, msd_client_id_t client_id,
      std::unique_ptr<magma::PlatformHandle> thread_profile);

  msd_device_t* msd_dev() { return msd_dev_.get(); }

  // Returns the device id. 0 is invalid.
  uint32_t GetDeviceId();

  // Called on driver thread
  void Shutdown();

  // Called on driver thread
  void StartConnectionThread(std::shared_ptr<magma::PlatformConnection> platform_connection);

  // Called on connection thread
  void ConnectionClosed(std::thread::id thread_id);

  void DumpStatus(uint32_t dump_type) { msd_device_dump_status(msd_dev(), dump_type); }

  magma::Status Query(uint64_t id, uint64_t* value_out);

  magma::Status QueryReturnsBuffer(uint64_t id, uint32_t* buffer_out) {
    return msd_device_query_returns_buffer(msd_dev(), id, buffer_out);
  }

  void set_perf_count_access_token_id(uint64_t id) { perf_count_access_token_id_ = id; }
  uint64_t perf_count_access_token_id() const { return perf_count_access_token_id_; }

 private:
  msd_device_unique_ptr_t msd_dev_;
  uint64_t perf_count_access_token_id_ = 0u;

  struct Connection {
    std::thread thread;
    std::shared_ptr<magma::PlatformEvent> shutdown_event;
  };

  std::unique_ptr<std::unordered_map<std::thread::id, Connection>> connection_map_;
  std::mutex connection_list_mutex_;
};

#endif  // SRC_GRAPHICS_LIB_MAGMA_SRC_SYS_DRIVER_MAGMA_SYSTEM_DEVICE_H_
