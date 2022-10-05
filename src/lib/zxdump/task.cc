// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxdump/task.h>
#include <zircon/assert.h>

namespace zxdump {

constexpr size_t kMaxPropertySize = ZX_MAX_NAME_LEN;

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

fit::result<Error, ByteView> Task::get_info(zx_object_info_topic_t topic, size_t record_size) {
  if (info_.empty()) {
    // Only the superroot has no cached info at all.  It's a special case.
    return GetSuperrootInfo(topic);
  }
  auto found = info_.find(topic);
  if (found == info_.end()) {
    if (!live_) {
      return fit::error(Error{"zx_object_get_info", ZX_ERR_NOT_SUPPORTED});
    }

    // This interface cannot be transparently proxied!  We can always come
    // up with a buffer size that's big enough just by trying bigger sizes.
    // But short of searching the space of sizes empirically with get_info
    // attempts, there is no way to know what the correct exact size was.
    // The call can return a count of the amount of data that's actually
    // available, but only as a count of records, not a count of bytes.
    // The size of each record is just implicit in the topic.
    std::unique_ptr<std::byte[]> buffer;
    zx_status_t status;
    size_t actual = 0, avail = 0;
    size_t size = record_size == 0 ? sizeof(zx_info_handle_basic_t) / 2 : 0;
    do {
      if (record_size != 0 && record_size * avail > size) {
        size = record_size * avail;
      } else {
        size *= 2;
      }
      buffer = std::make_unique<std::byte[]>(size);
      status = live_.get_info(topic, buffer.get(), size, &actual, &avail);
    } while (status == ZX_ERR_BUFFER_TOO_SMALL || actual < avail);
    if (status != ZX_OK) {
      return fit::error(Error{"zx_object_get_info", status});
    }

    auto [it, unique] = info_.emplace(topic, ByteView{buffer.get(), size});
    ZX_DEBUG_ASSERT(unique);
    found = it;

    TakeBuffer(std::move(buffer));
  }
  return fit::ok(found->second);
}

fit::result<Error, ByteView> Task::get_property(uint32_t property) {
  auto found = properties_.find(property);
  if (found == properties_.end()) {
    if (!live_) {
      return fit::error(Error{"zx_object_get_property", ZX_ERR_NOT_SUPPORTED});
    }
    auto buffer = GetBuffer(kMaxPropertySize);
    if (zx_status_t status = live_.get_property(property, buffer, kMaxPropertySize);
        status != ZX_OK) {
      ZX_ASSERT(status != ZX_ERR_BUFFER_TOO_SMALL);
      return fit::error(Error{"zx_object_get_property", status});
    }
    auto [it, unique] = properties_.emplace(property, ByteView{buffer, kMaxPropertySize});
    ZX_DEBUG_ASSERT(unique);
    found = it;
  }
  return fit::ok(found->second);
}

fit::result<Error, ByteView> Thread::read_state(zx_thread_state_topic_t topic) {
  auto found = state_.find(topic);
  if (found == state_.end()) {
    return fit::error(Error{"zx_thread_read_state", ZX_ERR_NOT_SUPPORTED});
  }
  return fit::ok(found->second);
}

}  // namespace zxdump
