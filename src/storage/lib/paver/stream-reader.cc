// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stream-reader.h"

#include <memory>

#include "pave-logging.h"

namespace paver {

zx_status_t StreamReader::Create(zx::channel stream, std::unique_ptr<StreamReader>* reader) {
  zx::vmo vmo;
  auto status = zx::vmo::create(8192, 0, &vmo);
  if (status != ZX_OK) {
    ERROR("Unable to create vmo.\n");
    return status;
  }
  zx::vmo dup;
  status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
  if (status != ZX_OK) {
    ERROR("Unable to duplicate vmo.\n");
    return status;
  }
  auto result = ::llcpp::fuchsia::paver::PayloadStream::Call::RegisterVmo(zx::unowned(stream),
                                                                          std::move(dup));
  status = result.ok() ? result.value().status : result.status();
  if (status != ZX_OK) {
    ERROR("Unable to register vmo: %d\n", status);
    return status;
  }
  reader->reset(new StreamReader(std::move(stream), std::move(vmo)));
  return ZX_OK;
}

zx_status_t StreamReader::Read(void* buf, size_t buf_size, size_t* size_actual) {
  if (size_ == 0) {
    auto call_result = stream_.ReadData();
    if (call_result.status() != ZX_OK) {
      return call_result.status();
    }
    const ::llcpp::fuchsia::paver::ReadResult& read_result = call_result.value().result;
    switch (read_result.which()) {
      case llcpp::fuchsia::paver::ReadResult::Tag::kErr:
        return read_result.err();
      case llcpp::fuchsia::paver::ReadResult::Tag::kEof:
        *size_actual = 0;
        return ZX_OK;
      case llcpp::fuchsia::paver::ReadResult::Tag::kInfo:
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
