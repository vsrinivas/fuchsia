// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxdump/task.h>
#include <zircon/assert.h>

namespace zxdump {

zx_koid_t Task::koid() const {
  if (auto found = info_.find(ZX_INFO_HANDLE_BASIC); found != info_.end()) {
    auto [topic, data] = *found;
    zx_info_handle_basic_t info;
    ZX_ASSERT(data.size() >= sizeof(info));
    memcpy(&info, data.data(), sizeof(info));
    return info.koid;
  }
  // Only the superroot has no cached basic info.  It's a special case.
  return 0;
}

zx_obj_type_t Task::type() const {
  if (auto found = info_.find(ZX_INFO_HANDLE_BASIC); found != info_.end()) {
    auto [topic, data] = *found;
    zx_info_handle_basic_t info;
    ZX_ASSERT(data.size() >= sizeof(info));
    memcpy(&info, data.data(), sizeof(info));
    return info.type;
  }
  // Only the superroot has no cached basic info.  It's a special case.
  return 0;
}

fitx::result<Error, ByteView> Task::get_info(zx_object_info_topic_t topic) {
  if (info_.empty()) {
    // Only the superroot has no cached info at all.  It's a special case.
    return GetSuperrootInfo(topic);
  }
  auto found = info_.find(topic);
  if (found == info_.end()) {
    return fitx::error(Error{"zx_object_get_info", ZX_ERR_NOT_SUPPORTED});
  }
  return fitx::ok(found->second);
}

fitx::result<Error, ByteView> Task::get_property(uint32_t property) {
  auto found = properties_.find(property);
  if (found == properties_.end()) {
    return fitx::error(Error{"zx_object_get_property", ZX_ERR_NOT_SUPPORTED});
  }
  return fitx::ok(found->second);
}

fitx::result<Error, ByteView> Thread::read_state(zx_thread_state_topic_t topic) {
  auto found = state_.find(topic);
  if (found == state_.end()) {
    return fitx::error(Error{"zx_thread_read_state", ZX_ERR_NOT_SUPPORTED});
  }
  return fitx::ok(found->second);
}

}  // namespace zxdump
