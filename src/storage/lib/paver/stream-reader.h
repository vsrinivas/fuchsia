// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_LIB_PAVER_STREAM_READER_H_
#define SRC_STORAGE_LIB_PAVER_STREAM_READER_H_

#include <fidl/fuchsia.paver/cpp/wire.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>

#include <memory>

#include "src/storage/fvm/sparse_reader.h"

namespace paver {

// Implements fvm::ReaderInterface to allow interoperability between paver and
// fvm sparse reader library.
class StreamReader : public fvm::ReaderInterface {
 public:
  static zx::result<std::unique_ptr<StreamReader>> Create(zx::channel stream);

  virtual ~StreamReader() = default;

  virtual zx_status_t Read(void* buf, size_t buf_size, size_t* size_actual) final;

 private:
  StreamReader(zx::channel stream, zx::vmo vmo)
      : stream_(std::move(stream)), vmo_(std::move(vmo)) {}

  StreamReader(const StreamReader&) = delete;
  StreamReader& operator=(const StreamReader&) = delete;
  StreamReader(StreamReader&&) = delete;
  StreamReader& operator=(StreamReader&&) = delete;

  fidl::WireSyncClient<fuchsia_paver::PayloadStream> stream_;
  zx::vmo vmo_;
  zx_off_t offset_ = 0;
  size_t size_ = 0;
};

}  // namespace paver

#endif  // SRC_STORAGE_LIB_PAVER_STREAM_READER_H_
