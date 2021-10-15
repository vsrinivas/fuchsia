// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/internal.h>
#include <lib/fidl/llcpp/internal/transport_channel.h>

#include <cstring>

namespace fidl {
namespace internal {

namespace {

struct obj_type_and_rights {
  zx_obj_type_t obj_type;
  zx_rights_t rights;
};

zx_status_t channel_write(Handle handle, EncodeFlags encode_flags, const void* data,
                          uint32_t data_count, const Handle* handles, const void* handle_metadata,
                          uint32_t handles_count) {
  zx_handle_disposition_t hds[ZX_CHANNEL_MAX_MSG_HANDLES];
  const obj_type_and_rights* metadata = static_cast<const obj_type_and_rights*>(handle_metadata);
  for (uint32_t i = 0; i < handles_count; i++) {
    hds[i] = zx_handle_disposition_t{
        .operation = ZX_HANDLE_OP_MOVE,
        .handle = handles[i].value(),
        .type = metadata[i].obj_type,
        .rights = metadata[i].rights,
        .result = ZX_OK,
    };
  }
  return zx_channel_write_etc(handle.value(), ZX_CHANNEL_WRITE_USE_IOVEC, data, data_count,
                              reinterpret_cast<zx_handle_disposition_t*>(hds), handles_count);
}

zx_status_t channel_read(Handle handle, void* data, uint32_t data_capacity, Handle* handles,
                         void* handle_metadata, uint32_t handles_capacity,
                         DecodeFlags* out_decode_flags, uint32_t* out_data_actual_count,
                         uint32_t* out_handles_actual_count) {
  *out_decode_flags = {};
  *out_data_actual_count = 0;
  *out_handles_actual_count = 0;
  zx_handle_info_t his[ZX_CHANNEL_MAX_MSG_HANDLES];
  zx_status_t status =
      zx_channel_read_etc(handle.value(), 0, data, his, data_capacity, handles_capacity,
                          out_data_actual_count, out_handles_actual_count);
  obj_type_and_rights* metadata = static_cast<obj_type_and_rights*>(handle_metadata);
  for (uint32_t i = 0; i < *out_handles_actual_count; i++) {
    handles[i] = Handle(his[i].handle);
    metadata[i] = obj_type_and_rights{
        .obj_type = his[i].type,
        .rights = his[i].rights,
    };
  }
  return status;
}

zx_status_t channel_call(Handle handle, EncodeFlags encode_flags, zx_time_t deadline,
                         CallMethodArgs cargs, DecodeFlags* out_decode_flags,
                         uint32_t* out_data_actual_count, uint32_t* out_handles_actual_count) {
  *out_decode_flags = {};
  zx_handle_disposition_t hds[ZX_CHANNEL_MAX_MSG_HANDLES];
  const obj_type_and_rights* wr_metadata =
      static_cast<const obj_type_and_rights*>(cargs.wr_handle_metadata);
  for (uint32_t i = 0; i < cargs.wr_handles_count; i++) {
    hds[i] = zx_handle_disposition_t{
        .operation = ZX_HANDLE_OP_MOVE,
        .handle = cargs.wr_handles[i].value(),
        .type = wr_metadata[i].obj_type,
        .rights = wr_metadata[i].rights,
        .result = ZX_OK,
    };
  }
  zx_handle_info_t his[ZX_CHANNEL_MAX_MSG_HANDLES];
  zx_channel_call_etc_args_t args = {
      .wr_bytes = cargs.wr_data,
      .wr_handles = hds,
      .rd_bytes = cargs.rd_data,
      .rd_handles = his,
      .wr_num_bytes = cargs.wr_data_count,
      .wr_num_handles = cargs.wr_handles_count,
      .rd_num_bytes = cargs.rd_data_capacity,
      .rd_num_handles = cargs.rd_handles_capacity,
  };
  zx_status_t status = zx_channel_call_etc(handle.value(), ZX_CHANNEL_WRITE_USE_IOVEC, deadline,
                                           &args, out_data_actual_count, out_handles_actual_count);
  obj_type_and_rights* rd_metadata = static_cast<obj_type_and_rights*>(cargs.rd_handle_metadata);
  for (uint32_t i = 0; i < *out_handles_actual_count; i++) {
    cargs.rd_handles[i] = Handle(his[i].handle);
    rd_metadata[i] = obj_type_and_rights{
        .obj_type = his[i].type,
        .rights = his[i].rights,
    };
  }
  return status;
}
void channel_close(Handle handle) { zx_handle_close(handle.value()); }

}  // namespace

const TransportVTable ChannelTransport::VTable = {
    .type = TransportType::Channel,
    .encoding_configuration = &ChannelTransport::EncodingConfiguration,
    .write = channel_write,
    .read = channel_read,
    .call = channel_call,
    .close = channel_close,
};

namespace {

zx_status_t channel_encode_process_handle(HandleAttributes attr, uint32_t metadata_index,
                                          void* out_metadata_array, const char** out_error) {
  reinterpret_cast<obj_type_and_rights*>(out_metadata_array)[metadata_index] = {
      .obj_type = attr.obj_type, .rights = attr.rights};
  return ZX_OK;
}
zx_status_t channel_decode_process_handle(Handle* handle, HandleAttributes attr,
                                          uint32_t metadata_index, const void* metadata_array,
                                          const char** error) {
  obj_type_and_rights v =
      reinterpret_cast<const obj_type_and_rights*>(metadata_array)[metadata_index];
  return FidlEnsureHandleRights(&handle->value(), v.obj_type, v.rights, attr.obj_type, attr.rights,
                                error);
}

}  // namespace

const EncodingConfiguration ChannelTransport::EncodingConfiguration = {
    .encode_supports_iovec = true,
    .decode_supports_iovec = false,
    .encode_process_handle = channel_encode_process_handle,
    .decode_process_handle = channel_decode_process_handle,
};

AnyTransport MakeAnyTransport(zx::channel channel) {
  return AnyTransport::Make<ChannelTransport>(Handle(channel.release()));
}
AnyUnownedTransport MakeAnyUnownedTransport(const zx::channel& channel) {
  return MakeAnyUnownedTransport(channel.borrow());
}
AnyUnownedTransport MakeAnyUnownedTransport(const zx::unowned_channel& channel) {
  return AnyUnownedTransport::Make<ChannelTransport>(Handle(channel->get()));
}

}  // namespace internal
}  // namespace fidl
