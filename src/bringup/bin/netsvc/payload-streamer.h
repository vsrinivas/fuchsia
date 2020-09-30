// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_NETSVC_PAYLOAD_STREAMER_H_
#define SRC_BRINGUP_BIN_NETSVC_PAYLOAD_STREAMER_H_

#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <fbl/function.h>
#include <fbl/unique_fd.h>

namespace netsvc {

// Reads the data into the vmo at offset, size. Can block.
using ReadCallback = fbl::Function<zx_status_t(void* /*buf*/, size_t /*offset*/, size_t /*size*/,
                                               size_t* /*actual*/)>;

class PayloadStreamer : public ::llcpp::fuchsia::paver::PayloadStream::Interface {
 public:
  PayloadStreamer(zx::channel chan, ReadCallback callback);

  PayloadStreamer(const PayloadStreamer&) = delete;
  PayloadStreamer& operator=(const PayloadStreamer&) = delete;
  PayloadStreamer(PayloadStreamer&&) = delete;
  PayloadStreamer& operator=(PayloadStreamer&&) = delete;

  void RegisterVmo(zx::vmo vmo, RegisterVmoCompleter::Sync& completer);

  void ReadData(ReadDataCompleter::Sync& completer);

 private:
  ReadCallback read_;
  zx::vmo vmo_;
  fzl::VmoMapper mapper_;
  size_t read_offset_ = 0;
  fidl::aligned<bool> eof_reached_ = false;
};

}  // namespace netsvc

#endif  // SRC_BRINGUP_BIN_NETSVC_PAYLOAD_STREAMER_H_
