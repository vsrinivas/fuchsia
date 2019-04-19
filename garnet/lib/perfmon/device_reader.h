// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PERFMON_DEVICE_READER_H_
#define GARNET_LIB_PERFMON_DEVICE_READER_H_

#include <src/lib/fxl/macros.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>
#include <zircon/types.h>

#include "garnet/lib/perfmon/properties.h"
#include "garnet/lib/perfmon/reader.h"

namespace perfmon {

class DeviceReader final : public Reader {
 public:
  // |fd| is borrowed.
  static bool Create(int fd, uint32_t buffer_size_in_pages,
                     std::unique_ptr<DeviceReader>* out_reader);

  ~DeviceReader();

  bool GetProperties(Properties* props);

  bool GetConfig(perfmon_config_t* config);

 private:
  // |fd| is borrowed.
  DeviceReader(int fd, uint32_t buffer_size_in_pages, zx::vmar vmar);

  bool MapBuffer(const std::string& name, uint32_t trace_num) override;
  bool UnmapBuffer() override;

  const int fd_;  // borrowed
  const size_t buffer_size_;
  zx::vmar vmar_;

  const void* buffer_contents_ = nullptr;
  zx::vmo current_vmo_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeviceReader);
};

}  // namespace perfmon

#endif  // GARNET_LIB_PERFMON_DEVICE_READER_H_
