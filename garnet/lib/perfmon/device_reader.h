// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PERFMON_DEVICE_READER_H_
#define GARNET_LIB_PERFMON_DEVICE_READER_H_

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>
#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/memory/weak_ptr.h>
#include <zircon/types.h>

#include "garnet/lib/perfmon/controller.h"
#include "garnet/lib/perfmon/properties.h"
#include "garnet/lib/perfmon/reader.h"

namespace perfmon {
namespace internal {

class DeviceReader final : public Reader {
 public:
  static bool Create(fxl::WeakPtr<Controller> controller,
                     uint32_t buffer_size_in_pages,
                     std::unique_ptr<Reader>* out_reader);

  ~DeviceReader();

 private:
  DeviceReader(fxl::WeakPtr<Controller> controller,
               uint32_t buffer_size_in_pages, zx::vmar vmar);

  bool MapBuffer(const std::string& name, uint32_t trace_num) override;
  bool UnmapBuffer() override;

  fxl::WeakPtr<Controller> controller_;
  const size_t buffer_size_;
  zx::vmar vmar_;

  const void* buffer_contents_ = nullptr;
  zx::vmo current_vmo_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeviceReader);
};

}  // namespace internal
}  // namespace perfmon

#endif  // GARNET_LIB_PERFMON_DEVICE_READER_H_
