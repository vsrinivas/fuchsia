// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DRIVER_FRAMEWORK_DEVCOORDINATOR_BOOT_ARGS_H_
#define SRC_DRIVER_FRAMEWORK_DEVCOORDINATOR_BOOT_ARGS_H_

#include <lib/zx/vmo.h>

#include <map>
#include <string_view>

#include <fbl/vector.h>

namespace devmgr {

// Handle boot arguments contained within a VMO.
class BootArgs {
 public:
  // Create BootArgs from a |vmo| with a given |size|.
  static zx_status_t Create(zx::vmo vmo, size_t size, BootArgs* out);

  BootArgs() = default;
  ~BootArgs();

  BootArgs(const BootArgs&) = delete;
  BootArgs(BootArgs&&) = delete;
  BootArgs& operator=(const BootArgs&) = delete;
  BootArgs& operator=(BootArgs&&) = delete;

  // Get the value of boot argument |name|.
  const char* Get(std::string_view name) const;
  // Get the boolean value of boot argument |name|. If it does not exist,
  // return |default_value|.
  bool GetBool(std::string_view name, bool default_value) const;
  // Collect all boot arguments with |prefix| and add them to |out|.
  void Collect(std::string_view prefix, fbl::Vector<const char*>* out) const;

 private:
  uintptr_t addr_ = 0;
  size_t size_ = 0;
  // TODO(TC-383): This should be std::unordered_map.
  using ArgsMap = std::map<std::string_view, const char*>;
  ArgsMap args_;
};

}  // namespace devmgr

#endif  // SRC_DRIVER_FRAMEWORK_DEVCOORDINATOR_BOOT_ARGS_H_
