// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_PIPE_H_
#define SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_PIPE_H_

#include <fidl/fuchsia.hardware.goldfish/cpp/wire.h>
#include <fuchsia/hardware/goldfish/pipe/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/io-buffer.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/event.h>
#include <lib/zx/pmt.h>
#include <stdarg.h>

#include <ddktl/device.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>

namespace goldfish {

// An instance of this class serves a Pipe connection.
class Pipe : public fidl::WireServer<fuchsia_hardware_goldfish::Pipe> {
 public:
  using OnBindFn = fit::function<void(Pipe*)>;
  using OnCloseFn = fit::function<void(Pipe*)>;

  Pipe(zx_device_t* parent, async_dispatcher_t* dispatcher, OnBindFn on_bind, OnCloseFn on_close);
  // Public for std::unique_ptr<Pipe>:
  ~Pipe();

  void Init();

  void Bind(fidl::ServerEnd<fuchsia_hardware_goldfish::Pipe> server_request);

 private:
  struct Buffer {
    Buffer& operator=(Buffer&&) noexcept;
    ~Buffer();
    zx::vmo vmo;
    zx::pmt pmt;
    size_t size;
    zx_paddr_t phys;
  };

  // |fidl::WireServer<fuchsia_hardware_goldfish::Pipe>|
  void SetBufferSize(SetBufferSizeRequestView request,
                     SetBufferSizeCompleter::Sync& completer) override;

  // |fidl::WireServer<fuchsia_hardware_goldfish::Pipe>|
  void SetEvent(SetEventRequestView request, SetEventCompleter::Sync& completer) override;

  // |fidl::WireServer<fuchsia_hardware_goldfish::Pipe>|
  void GetBuffer(GetBufferRequestView request, GetBufferCompleter::Sync& completer) override;

  // |fidl::WireServer<fuchsia_hardware_goldfish::Pipe>|
  void Read(ReadRequestView request, ReadCompleter::Sync& completer) override;

  // |fidl::WireServer<fuchsia_hardware_goldfish::Pipe>|
  void Write(WriteRequestView request, WriteCompleter::Sync& completer) override;

  // |fidl::WireServer<fuchsia_hardware_goldfish::Pipe>|
  void DoCall(DoCallRequestView request, DoCallCompleter::Sync& completer) override;

  zx_status_t TransferLocked(int32_t cmd, int32_t wake_cmd, zx_signals_t state_clr,
                             zx_paddr_t paddr, size_t count, zx_paddr_t read_paddr,
                             size_t read_count, size_t* actual) TA_REQ(lock_);
  zx_status_t SetBufferSizeLocked(uint64_t size) TA_REQ(lock_);

  // Close current bounded channel and send an epitaph to the client.
  void FailAsync(zx_status_t epitaph, const char* file, int line, const char* format, ...);

  std::unique_ptr<fidl::ServerBindingRef<fuchsia_hardware_goldfish::Pipe>> binding_ref_;
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
