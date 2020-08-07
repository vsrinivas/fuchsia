// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_KTRACE_PROVIDER_DEVICE_READER_H_
#define GARNET_BIN_KTRACE_PROVIDER_DEVICE_READER_H_

#include <src/lib/files/unique_fd.h>

#include "garnet/bin/ktrace_provider/reader.h"

namespace ktrace_provider {

class DeviceReader : public Reader {
 public:
  DeviceReader();

 private:
  static constexpr size_t kChunkSize{16 * 4 * 1024};

  void ReadMoreData() override;

  fbl::unique_fd fd_;

  char buffer_[kChunkSize];

  DeviceReader(const DeviceReader&) = delete;
  DeviceReader(DeviceReader&&) = delete;
  DeviceReader& operator=(const DeviceReader&) = delete;
  DeviceReader& operator=(DeviceReader&&) = delete;
};

}  // namespace ktrace_provider

#endif  // GARNET_BIN_KTRACE_PROVIDER_DEVICE_READER_H_
