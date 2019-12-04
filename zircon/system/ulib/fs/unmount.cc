// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/vfs.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fidl/txn_header.h>
#include <lib/zircon-internal/debug.h>
#include <lib/fdio/vfs.h>

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

namespace fio = ::llcpp::fuchsia::io;

namespace fs {

zx_status_t Vfs::UnmountHandle(zx::channel handle, zx::time deadline) {
  using UnmountRequest = fio::DirectoryAdmin::UnmountRequest;
  using UnmountResponse = fio::DirectoryAdmin::UnmountResponse;

  fidl::Buffer<UnmountRequest> request_buffer;
  fidl::BytePart request_bytes = request_buffer.view();
  memset(request_bytes.data(), 0, request_bytes.capacity());
  request_bytes.set_actual(sizeof(UnmountRequest));
  fidl::DecodedMessage<UnmountRequest> msg(std::move(request_bytes));
  fio::DirectoryAdmin::SetTransactionHeaderFor::UnmountRequest(msg);

  fidl::EncodeResult<UnmountRequest> encode_result = fidl::Encode(std::move(msg));
  if (encode_result.status != ZX_OK) {
    return encode_result.status;
  }

  fidl::Buffer<UnmountResponse> response_buffer;
  auto result = fidl::Call<UnmountRequest, UnmountResponse>(
      handle, std::move(encode_result.message), response_buffer.view(), deadline);
  if (result.status != ZX_OK) {
    return result.status;
  }

  auto decode_result = fidl::Decode(std::move(result.message));
  if (decode_result.status != ZX_OK) {
    return decode_result.status;
  }

  return decode_result.message.message()->s;
}

}  // namespace fs
