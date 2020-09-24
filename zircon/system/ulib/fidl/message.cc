// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/trace.h>
#include <string.h>

#ifdef __Fuchsia__
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#endif

namespace fidl {

Message::Message() = default;

Message::Message(BytePart bytes, HandlePart handles)
    : bytes_(static_cast<BytePart&&>(bytes)), handles_(static_cast<HandlePart&&>(handles)) {}

Message::~Message() {
#ifdef __Fuchsia__
  if (handles_.actual() > 0) {
    zx_handle_close_many(handles_.data(), handles_.actual());
  }
#endif
  ClearHandlesUnsafe();
}

Message::Message(Message&& other)
    : bytes_(static_cast<BytePart&&>(other.bytes_)),
      handles_(static_cast<HandlePart&&>(other.handles_)) {}

Message& Message::operator=(Message&& other) {
  bytes_ = static_cast<BytePart&&>(other.bytes_);
  handles_ = static_cast<HandlePart&&>(other.handles_);
  return *this;
}

zx_status_t Message::Encode(const fidl_type_t* type, const char** error_msg_out) {
  uint32_t actual_handles = 0u;
  zx_status_t status = fidl_encode(type, bytes_.data(), bytes_.actual(), handles_.data(),
                                   handles_.capacity(), &actual_handles, error_msg_out);
  if (status == ZX_OK)
    handles_.set_actual(actual_handles);

  return status;
}

zx_status_t Message::Decode(const fidl_type_t* type, const char** error_msg_out) {
  fidl_trace(WillHLCPPDecode, type, bytes_.data(), bytes_.actual(), handles_.actual());
  zx_status_t status = fidl_decode_skip_unknown_union_handles(
      type, bytes_.data(), bytes_.actual(), handles_.data(), handles_.actual(), error_msg_out);
  fidl_trace(DidHLCPPDecode);

  ClearHandlesUnsafe();
  return status;
}

zx_status_t Message::Validate(const fidl_type_t* v1_type, const char** error_msg_out) const {
  fidl_trace(WillHLCPPValidate, v1_type, bytes_.data(), bytes_.actual(), handles_.actual());
  const zx_status_t status =
      fidl_validate(v1_type, bytes_.data(), bytes_.actual(), handles_.actual(), error_msg_out);
  fidl_trace(DidHLCPPValidate);

  return status;
}

#ifdef __Fuchsia__
zx_status_t Message::Read(zx_handle_t channel, uint32_t flags) {
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  fidl_trace(WillHLCPPChannelRead);
  zx_status_t status =
      zx_channel_read(channel, flags, bytes_.data(), handles_.data(), bytes_.capacity(),
                      handles_.capacity(), &actual_bytes, &actual_handles);
  if (status != ZX_OK) {
    return status;
  }
  fidl_trace(DidHLCPPChannelRead, nullptr /* type */, bytes_.data(), actual_bytes, actual_handles);

  // Ensure we received enough bytes for the FIDL header.
  if (actual_bytes < sizeof(fidl_message_header_t)) {
    return ZX_ERR_INVALID_ARGS;
  }

  bytes_.set_actual(actual_bytes);
  handles_.set_actual(actual_handles);
  return ZX_OK;
}

zx_status_t Message::Write(zx_handle_t channel, uint32_t flags) {
  fidl_trace(WillHLCPPChannelWrite, nullptr /* type */, bytes_.data(), bytes_.actual(),
             handles_.actual());
  zx_status_t status = zx_channel_write(channel, flags, bytes_.data(), bytes_.actual(),
                                        handles_.data(), handles_.actual());
  fidl_trace(DidHLCPPChannelWrite);

  // Handles are cleared by the kernel on either success or failure.
  ClearHandlesUnsafe();

  return status;
}

zx_status_t Message::Call(zx_handle_t channel, uint32_t flags, zx_time_t deadline,
                          Message* response) {
  zx_channel_call_args_t args;
  args.wr_bytes = bytes_.data();
  args.wr_handles = handles_.data();
  args.rd_bytes = response->bytes_.data();
  args.rd_handles = response->handles_.data();
  args.wr_num_bytes = bytes_.actual();
  args.wr_num_handles = handles_.actual();
  args.rd_num_bytes = response->bytes_.capacity();
  args.rd_num_handles = response->handles_.capacity();
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  zx_status_t status =
      zx_channel_call(channel, flags, deadline, &args, &actual_bytes, &actual_handles);
  ClearHandlesUnsafe();
  if (status == ZX_OK) {
    response->bytes_.set_actual(actual_bytes);
    response->handles_.set_actual(actual_handles);
  }
  return status;
}
#endif

void Message::ClearHandlesUnsafe() { handles_.set_actual(0u); }

}  // namespace fidl
