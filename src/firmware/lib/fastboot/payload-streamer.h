// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_LIB_FASTBOOT_PAYLOAD_STREAMER_H_
#define SRC_FIRMWARE_LIB_FASTBOOT_PAYLOAD_STREAMER_H_

#include <fidl/fuchsia.paver/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fzl/vmo-mapper.h>

namespace fastboot {
namespace internal {

// Implement a PayloadStreamer service for streaming FVM partition required by
// DataSink::WriteVolumes(...).
class PayloadStreamer : public fidl::WireServer<fuchsia_paver::PayloadStream> {
 public:
  PayloadStreamer(fidl::ServerEnd<fuchsia_paver::PayloadStream> server_end, const void* data,
                  size_t size)
      : data_(static_cast<const uint8_t*>(data)), size_(size) {
    fidl::BindSingleInFlightOnly(async_get_default_dispatcher(), std::move(server_end), this);
  }

  ~PayloadStreamer() override = default;

  PayloadStreamer(const PayloadStreamer&) = delete;
  PayloadStreamer& operator=(const PayloadStreamer&) = delete;
  PayloadStreamer(PayloadStreamer&&) = delete;
  PayloadStreamer& operator=(PayloadStreamer&&) = delete;

  // Register a vmo for reading data from the payload
  void RegisterVmo(RegisterVmoRequestView request, RegisterVmoCompleter::Sync& completer) override;

  // Read the payload data into the vmo registered via RegisterVmo(...)
  void ReadData(ReadDataCompleter::Sync& completer) override;

 private:
  zx::vmo vmo_;
  fzl::VmoMapper mapper_;
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  size_t read_offset_ = 0;
  bool eof_reached_ = false;
};

}  // namespace internal
}  // namespace fastboot

#endif  // SRC_FIRMWARE_LIB_FASTBOOT_PAYLOAD_STREAMER_H_
