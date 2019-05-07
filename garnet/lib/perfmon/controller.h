// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PERFMON_CONTROLLER_H_
#define GARNET_LIB_PERFMON_CONTROLLER_H_

#include <cstdint>
#include <memory>

#include "src/lib/files/unique_fd.h"
#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>

#include "garnet/lib/perfmon/config.h"
#include "garnet/lib/perfmon/device_reader.h"
#include "garnet/lib/perfmon/properties.h"

namespace perfmon {

class Controller {
 public:
  // The protcol specifies buffer sizes in 4K pages.
  static constexpr uint32_t kLog2PageSize = 12;
  static constexpr uint32_t kPageSize = 1 << kLog2PageSize;
  // The protocol restricts buffer sizes to 256MB.
  static constexpr uint32_t kMaxBufferSizeInPages =
      (256 * 1024 * 1024) / kPageSize;

  // Return true if perfmon is supported on this device.
  static bool IsSupported();

  // Fetch the properties of this device.
  static bool GetProperties(Properties* props);

  static bool Create(uint32_t buffer_size_in_pages, const Config& config,
                     std::unique_ptr<Controller>* out_controller);

  ~Controller();

  bool Start();
  // It is ok to call this while stopped.
  void Stop();

  bool started() const { return started_; }

  CollectionMode collection_mode() const { return collection_mode_; }

  uint32_t num_traces() const { return num_traces_; }

  std::unique_ptr<DeviceReader> GetReader();

 private:
  static bool Alloc(int fd, uint32_t num_traces, uint32_t buffer_size_in_pages);
  Controller(fxl::UniqueFD fd, CollectionMode collection_mode,
             uint32_t num_traces, uint32_t buffer_size,
             const perfmon_ioctl_config_t& config);
  bool Stage();
  void Free();
  void Reset();

  fxl::UniqueFD fd_;
  const CollectionMode collection_mode_;
  // The number of traces we will collect (== #cpus for now).
  uint32_t num_traces_;
  // This is the actual buffer size we use, in pages.
  const uint32_t buffer_size_in_pages_;
  const perfmon_ioctl_config_t config_;
  bool started_ = false;
};

}  // namespace perfmon

#endif  // GARNET_LIB_PERFMON_CONTROLLER_H_
