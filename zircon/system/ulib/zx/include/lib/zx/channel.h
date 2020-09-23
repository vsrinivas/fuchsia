// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_CHANNEL_H_
#define LIB_ZX_CHANNEL_H_

#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#include <lib/zx/time.h>

namespace zx {

class channel final : public object<channel> {
 public:
  static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_CHANNEL;

  constexpr channel() = default;

  explicit channel(zx_handle_t value) : object(value) {}

  explicit channel(handle&& h) : object(h.release()) {}

  channel(channel&& other) : object(other.release()) {}

  channel& operator=(channel&& other) {
    reset(other.release());
    return *this;
  }

  static zx_status_t create(uint32_t flags, channel* endpoint0, channel* endpoint1);

  zx_status_t read(uint32_t flags, void* bytes, zx_handle_t* handles, uint32_t num_bytes,
                   uint32_t num_handles, uint32_t* actual_bytes, uint32_t* actual_handles) const {
    return zx_channel_read(get(), flags, bytes, handles, num_bytes, num_handles, actual_bytes,
                           actual_handles);
  }

  zx_status_t read_etc(uint32_t flags, void* bytes, zx_handle_info_t* handles, uint32_t num_bytes,
                       uint32_t num_handles, uint32_t* actual_bytes,
                       uint32_t* actual_handles) const {
    return zx_channel_read_etc(get(), flags, bytes, handles, num_bytes, num_handles, actual_bytes,
                               actual_handles);
  }

  zx_status_t write(uint32_t flags, const void* bytes, uint32_t num_bytes,
                    const zx_handle_t* handles, uint32_t num_handles) const {
    return zx_channel_write(get(), flags, bytes, num_bytes, handles, num_handles);
  }

  zx_status_t write_etc(uint32_t flags, const void* bytes, uint32_t num_bytes,
                        zx_handle_disposition_t* handles, uint32_t num_handles) {
    return zx_channel_write_etc(get(), flags, bytes, num_bytes, handles, num_handles);
  }

  zx_status_t call(uint32_t flags, zx::time deadline, const zx_channel_call_args_t* args,
                   uint32_t* actual_bytes, uint32_t* actual_handles) const {
    return zx_channel_call(get(), flags, deadline.get(), args, actual_bytes, actual_handles);
  }

  zx_status_t call_etc(uint32_t flags, zx::time deadline, zx_channel_call_etc_args_t* args,
                       uint32_t* actual_bytes, uint32_t* actual_handles) const {
    return zx_channel_call_etc(get(), flags, deadline.get(), args, actual_bytes, actual_handles);
  }
};

using unowned_channel = unowned<channel>;

}  // namespace zx

#endif  // LIB_ZX_CHANNEL_H_
