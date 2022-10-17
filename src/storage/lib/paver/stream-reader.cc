// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/stream-reader.h"

#include <memory>

#include "src/storage/lib/paver/pave-logging.h"

namespace paver {

zx::result<std::unique_ptr<StreamReader>> StreamReader::Create(zx::channel stream) {
  zx::vmo vmo;
  auto status = zx::make_result(zx::vmo::create(8192, 0, &vmo));
  if (status.is_error()) {
    ERROR("Unable to create vmo.\n");
    return status.take_error();
  }
  zx::vmo dup;
  status = zx::make_result(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
  if (status.is_error()) {
    ERROR("Unable to duplicate vmo.\n");
    return status.take_error();
  }
  auto result = fidl::WireCall<fuchsia_paver::PayloadStream>(zx::unowned(stream))
                    ->RegisterVmo(std::move(dup));
  status = zx::make_result(result.ok() ? result.value().status : result.status());
  if (status.is_error()) {
    ERROR("Unable to register vmo: %d\n", status.error_value());
    return status.take_error();
  }
  return zx::ok(new StreamReader(std::move(stream), std::move(vmo)));
}

zx_status_t StreamReader::Read(void* buf, size_t buf_size, size_t* size_actual) {
  if (size_ == 0) {
    auto call_result = stream_->ReadData();
    if (call_result.status() != ZX_OK) {
      return call_result.status();
    }
    const fuchsia_paver::wire::ReadResult& read_result = call_result.value().result;
    switch (read_result.Which()) {
      case fuchsia_paver::wire::ReadResult::Tag::kErr:
        return read_result.err();
      case fuchsia_paver::wire::ReadResult::Tag::kEof:
        *size_actual = 0;
        return ZX_OK;
      case fuchsia_paver::wire::ReadResult::Tag::kInfo:
        offset_ = read_result.info().offset;
        size_ = read_result.info().size;
        break;
      default:
        return ZX_ERR_INTERNAL;
    }
  }
  const auto size = std::min(size_, buf_size);
  auto status = vmo_.read(buf, offset_, size);
  if (status != ZX_OK) {
    return status;
  }
  offset_ += size;
  size_ -= size;
  *size_actual = size;
  return ZX_OK;
}

}  // namespace paver
