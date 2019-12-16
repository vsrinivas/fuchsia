// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_HANDLE_H
#define PLATFORM_HANDLE_H

#include <memory>

namespace magma {

class PlatformHandle {
 public:
  PlatformHandle() = default;
  virtual ~PlatformHandle() = default;

  virtual bool GetCount(uint32_t* count_out) = 0;
  virtual uint32_t release() = 0;

  static bool duplicate_handle(uint32_t handle_in, uint32_t* handle_out);

  static std::unique_ptr<PlatformHandle> Create(uint32_t handle);

  static bool SupportsGetCount();

 private:
  PlatformHandle(const PlatformHandle&) = delete;
  PlatformHandle& operator=(const PlatformHandle&) = delete;
};

}  // namespace magma

#endif  // PLATFORM_HANDLE_H
