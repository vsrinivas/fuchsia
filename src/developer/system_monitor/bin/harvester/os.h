// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_OS_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_OS_H_

#include <zircon/status.h>

namespace harvester {

const size_t kNumExtraSlop = 10;

class OS {
 public:
  virtual ~OS() = default;

  // Thin wrappers around OS calls. Allows for mocking.

  virtual zx_status_t GetInfo(zx_handle_t parent, int children_kind,
                              void* out_buffer, size_t buffer_size,
                              size_t* actual, size_t* avail) = 0;

  // Convenience methods.

  // Wrapper around GetInfo for fetching vectors of children.
  template <typename T = zx_koid_t>
  zx_status_t GetChildren(zx_handle_t parent, zx_koid_t parent_koid,
                          int children_kind, const char* kind_name,
                          std::vector<T>& children) {
    zx_status_t status;

    // Fetch the number of children available.
    size_t num_children;
    status = GetInfo(
        parent, children_kind, nullptr, 0, nullptr, &num_children);

    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "zx_object_get_info(" << parent_koid << ", "
                     << kind_name << ", ...) failed: "
                     << zx_status_get_string(status) << " (" << status << ")";
      return status;
    }

    // This is inherently racy (TOCTTOU race condition). Add a bit of slop space
    // in case children have been added.
    children.reserve(num_children + kNumExtraSlop);

    // Fetch the actual child objects.
    size_t actual = 0;
    size_t available = 0;
    status = GetInfo(parent, children_kind, children.data(),
                     children.capacity() * sizeof(T), &actual, &available);

    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "zx_object_get_info(" << parent_koid << ", "
                     << kind_name << ", ...) failed: "
                     << zx_status_get_string(status) << " (" << status << ")";
      // On error, empty children so we don't pass through invalid information.
      children.clear();
      return status;
    }

    // If we're still too small at least warn the user.
    if (actual < available) {
      FX_LOGS(WARNING) <<  "zx_object_get_info(" << parent_koid << ", "
                       << kind_name << ", ...) truncated " << (available - actual)
                       << "/" << available << " results";
    }

    children.resize(actual);

    return ZX_OK;
  }
};

class OSImpl : public OS {
 public:
  ~OSImpl() = default;

  virtual zx_status_t GetInfo(zx_handle_t parent, int children_kind,
                              void* out_buffer, size_t buffer_size,
                              size_t* actual, size_t* avail) override {
    return zx_object_get_info(parent, children_kind, out_buffer, buffer_size,
                              actual, avail);
  }
};

}  // harvester

#endif  // SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_OS_H_

