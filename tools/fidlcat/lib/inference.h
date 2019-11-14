// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_INFERENCE_H_
#define TOOLS_FIDLCAT_LIB_INFERENCE_H_

#include <zircon/system/public/zircon/processargs.h>
#include <zircon/system/public/zircon/types.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "src/lib/fidl_codec/colors.h"

namespace fidlcat {

class SyscallDecoder;

// Holds the information we have about a handle.
class HandleDescription {
 public:
  HandleDescription() = default;

  explicit HandleDescription(std::string_view type) : type_(type) {}

  HandleDescription(std::string_view type, int64_t fd) : type_(type), fd_(fd) {}

  HandleDescription(std::string_view type, std::string_view path) : type_(type), path_(path) {}

  HandleDescription(std::string_view type, int64_t fd, std::string_view path)
      : type_(type), fd_(fd), path_(path) {}

  const std::string& type() const { return type_; }
  int64_t fd() const { return fd_; }
  const std::string& path() const { return path_; }

  // Convert a handle type (found in zircon/system/public/zircon/processargs.h) into a string.
  static std::string_view Convert(uint32_t type);

  // Display the information we have about a handle.
  void Display(const fidl_codec::Colors& colors, std::ostream& os) const;

 private:
  // Type of the handle. This can be a predefined type (when set by Convert) or
  // any string when it is an applicative type.
  const std::string type_;
  // Numerical value associated with the handle. Mostly used by file descriptors.
  const int64_t fd_ = -1;
  // Path associated with the handle. We can have both fd and path defined at the
  // same time.
  const std::string path_;
};

// Object which hold the information we have about handles.
class Inference {
 public:
  Inference() = default;

  const std::map<zx_handle_t, std::unique_ptr<HandleDescription>>& handles() const {
    return handles_;
  }

  const HandleDescription* GetHandleDescription(zx_handle_t handle) const {
    const auto& result = handles_.find(handle);
    if (result == handles_.end()) {
      return nullptr;
    }
    return result->second.get();
  }

  void AddHandleDescription(zx_handle_t handle, const HandleDescription& handle_description) {
    handles_[handle] = std::make_unique<HandleDescription>(handle_description);
  }

  void AddHandleDescription(zx_handle_t handle, std::string_view type) {
    handles_[handle] = std::make_unique<HandleDescription>(type);
  }

  void AddHandleDescription(zx_handle_t handle, std::string_view type, int64_t fd) {
    handles_[handle] = std::make_unique<HandleDescription>(type, fd);
  }

  void AddHandleDescription(zx_handle_t handle, std::string_view type, std::string_view path) {
    handles_[handle] = std::make_unique<HandleDescription>(type, path);
  }

  void AddHandleDescription(zx_handle_t handle, uint32_t type) {
    handles_[handle] = std::make_unique<HandleDescription>(HandleDescription::Convert(type));
  }

  // Function called when processargs_extract_handles (from libc) is intercepted.
  void ExtractHandles(SyscallDecoder* decoder);

  // Function called when __libc_extensions_init (from libc) is intercepted.
  void LibcExtensionsInit(SyscallDecoder* decoder);

 private:
  // All the handles for which we have some information.
  std::map<zx_handle_t, std::unique_ptr<HandleDescription>> handles_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_INFERENCE_H_
