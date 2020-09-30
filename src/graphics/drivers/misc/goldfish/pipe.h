// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_PIPE_H_
#define SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_PIPE_H_

#include <fuchsia/hardware/goldfish/llcpp/fidl.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/event.h>
#include <lib/zx/pmt.h>
#include <stdarg.h>

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddktl/device.h>
#include <ddktl/protocol/goldfish/pipe.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>

namespace goldfish {

// An instance of this class serves a Pipe connection.
class Pipe : public llcpp::fuchsia::hardware::goldfish::Pipe::Interface {
 public:
  using OnBindFn = fit::function<void(Pipe*)>;
  using OnCloseFn = fit::function<void(Pipe*)>;

  Pipe(zx_device_t* parent, async_dispatcher_t* dispatcher, OnBindFn on_bind, OnCloseFn on_close);
  // Public for std::unique_ptr<Pipe>:
  ~Pipe();

  void Init();

  void Bind(zx::channel server_request);

 private:
  struct Buffer {
    Buffer& operator=(Buffer&&) noexcept;
    ~Buffer();
    zx::vmo vmo;
    zx::pmt pmt;
    size_t size;
    zx_paddr_t phys;
  };

  // |llcpp::fuchsia::hardware::goldfish::Pipe::Interface|
  void SetBufferSize(uint64_t size, SetBufferSizeCompleter::Sync completer) override;

  // |llcpp::fuchsia::hardware::goldfish::Pipe::Interface|
  void SetEvent(::zx::event event, SetEventCompleter::Sync completer) override;

  // |llcpp::fuchsia::hardware::goldfish::Pipe::Interface|
  void GetBuffer(GetBufferCompleter::Sync completer) override;

  // |llcpp::fuchsia::hardware::goldfish::Pipe::Interface|
  void Read(uint64_t count, uint64_t offset, ReadCompleter::Sync completer) override;

  // |llcpp::fuchsia::hardware::goldfish::Pipe::Interface|
  void Write(uint64_t count, uint64_t offset, WriteCompleter::Sync completer) override;

  // |llcpp::fuchsia::hardware::goldfish::Pipe::Interface|
  void DoCall(uint64_t count, uint64_t offset, uint64_t read_count, uint64_t read_offset,
              DoCallCompleter::Sync completer) override;

  zx_status_t TransferLocked(int32_t cmd, int32_t wake_cmd, zx_signals_t state_clr,
                             zx_paddr_t paddr, size_t count, zx_paddr_t read_paddr,
                             size_t read_count, size_t* actual) TA_REQ(lock_);
  zx_status_t SetBufferSizeLocked(uint64_t size) TA_REQ(lock_);

  // Close current bounded channel and send an epitaph to the client.
  void FailAsync(zx_status_t epitaph, const char* format, ...);

  std::unique_ptr<fidl::ServerBindingRef<llcpp::fuchsia::hardware::goldfish::Pipe>> binding_ref_;
  const OnBindFn on_bind_;
  const OnCloseFn on_close_;

  async_dispatcher_t* dispatcher_;

  fbl::Mutex lock_;
  ddk::GoldfishPipeProtocolClient pipe_;
  int32_t id_ TA_GUARDED(lock_) = 0;
  zx::bti bti_;
  ddk::IoBuffer cmd_buffer_;

  Buffer buffer_ TA_GUARDED(lock_) = {};
};

}  // namespace goldfish

#endif  // SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_PIPE_H_
