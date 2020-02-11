// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_SEMANTIC_H_
#define SRC_LIB_FIDL_CODEC_SEMANTIC_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/lib/fidl_codec/printer.h"

namespace fidl_codec {
namespace semantic {

// Holds the information we have about a handle.
// Usually we can associate a type to a handle.
// Depending on the type, we can also associate:
// - a path (for example for directories and files).
// - a file descriptor (for example for sockets).
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

// Holds the handle semantic for one process. That is all the meaningful information we have been
// able to infer for the handles owned by one process.
struct ProcessSemantic {
  // All the handles for which we have some information.
  std::map<zx_handle_t, std::unique_ptr<HandleDescription>> handles;
};

// Object which hold the information we have about handles for all the processes.
class HandleSemantic {
 public:
  HandleSemantic() = default;

  size_t handle_size(zx_koid_t pid) const {
    const auto& process_semantic = process_handles_.find(pid);
    if (process_semantic == process_handles_.end()) {
      return 0;
    }
    return process_semantic->second.handles.size();
  }

  const HandleDescription* GetHandleDescription(zx_koid_t pid, zx_handle_t handle) const {
    const auto& process_semantic = process_handles_.find(pid);
    if (process_semantic == process_handles_.end()) {
      return nullptr;
    }
    const auto& result = process_semantic->second.handles.find(handle);
    if (result == process_semantic->second.handles.end()) {
      return nullptr;
    }
    return result->second.get();
  }

  void AddHandleDescription(zx_koid_t pid, zx_handle_t handle,
                            const HandleDescription& handle_description) {
    process_handles_[pid].handles[handle] = std::make_unique<HandleDescription>(handle_description);
  }

  void AddHandleDescription(zx_koid_t pid, zx_handle_t handle, std::string_view type) {
    process_handles_[pid].handles[handle] = std::make_unique<HandleDescription>(type);
  }

  void AddHandleDescription(zx_koid_t pid, zx_handle_t handle, std::string_view type, int64_t fd) {
    process_handles_[pid].handles[handle] = std::make_unique<HandleDescription>(type, fd);
  }

  void AddHandleDescription(zx_koid_t pid, zx_handle_t handle, std::string_view type,
                            std::string_view path) {
    process_handles_[pid].handles[handle] = std::make_unique<HandleDescription>(type, path);
  }

  void AddHandleDescription(zx_koid_t pid, zx_handle_t handle, uint32_t type) {
    process_handles_[pid].handles[handle] =
        std::make_unique<HandleDescription>(HandleDescription::Convert(type));
  }

 private:
  std::map<zx_koid_t, ProcessSemantic> process_handles_;
};

}  // namespace semantic
}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_SEMANTIC_H_
