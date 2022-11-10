// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/internal/transport_channel_host.h>
#include <lib/fidl/internal.h>

namespace fidl::internal {
namespace {

zx_status_t channel_encode_process_handle(HandleAttributes attr, uint32_t metadata_index,
                                          void* out_metadata_array, const char** out_error) {
  reinterpret_cast<fidl_channel_handle_metadata_t*>(out_metadata_array)[metadata_index] = {
      .obj_type = attr.obj_type, .rights = attr.rights};
  return ZX_OK;
}
zx_status_t channel_decode_process_handle(fidl_handle_t* handle, HandleAttributes attr,
                                          uint32_t metadata_index, const void* metadata_array,
                                          const char** error) {
  fidl_channel_handle_metadata_t v =
      reinterpret_cast<const fidl_channel_handle_metadata_t*>(metadata_array)[metadata_index];
  return FidlEnsureHandleRights(handle, v.obj_type, v.rights, attr.obj_type, attr.rights, error);
}

}  // namespace

const TransportVTable ChannelTransport::VTable = {
    .type = FIDL_TRANSPORT_TYPE_CHANNEL,
    .encoding_configuration = &ChannelTransport::EncodingConfiguration,
};

const CodingConfig ChannelTransport::EncodingConfiguration = {
    .handle_metadata_stride = sizeof(fidl_channel_handle_metadata_t),
    .encode_process_handle = channel_encode_process_handle,
    .decode_process_handle = channel_decode_process_handle,
    .close = [](fidl_handle_t) { ZX_PANIC("Handles are not supported on host"); },
    .close_many =
        [](const fidl_handle_t*, size_t num_handles) {
          ZX_ASSERT_MSG(num_handles == 0, "Handles are not supported on host");
        },
};

}  // namespace fidl::internal
