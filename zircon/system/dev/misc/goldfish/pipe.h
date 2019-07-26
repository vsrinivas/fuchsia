// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddktl/device.h>
#include <ddktl/protocol/goldfish/pipe.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <fuchsia/hardware/goldfish/pipe/c/fidl.h>
#include <lib/fidl-async-2/fidl_server.h>
#include <lib/fidl-async-2/simple_binding.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/event.h>
#include <lib/zx/pmt.h>
#include <stdarg.h>

namespace goldfish {

void vLog(bool is_error, const char* prefix1, const char* prefix2, const char* format,
          va_list args);

// An instance of this class serves a Pipe connection.
class Pipe : public FidlServer<Pipe,
                               SimpleBinding<Pipe, fuchsia_hardware_goldfish_pipe_Pipe_ops_t,
                                             fuchsia_hardware_goldfish_pipe_Pipe_dispatch>,
                               vLog> {
 public:
  // Public for std::unique_ptr<Pipe>:
  ~Pipe();

  void Init();

 private:
  friend class FidlServer;

  explicit Pipe(zx_device_t* parent);

  zx_status_t SetBufferSize(uint64_t size, fidl_txn_t* txn);
  zx_status_t SetEvent(zx_handle_t event_handle);
  zx_status_t GetBuffer(fidl_txn_t* txn);
  zx_status_t Read(size_t count, zx_off_t offset, fidl_txn_t* txn);
  zx_status_t Write(size_t count, zx_off_t offset, fidl_txn_t* txn);
  zx_status_t Call(size_t count, zx_off_t offset, size_t read_count, zx_off_t read_offset,
                   fidl_txn_t* txn);

  zx_status_t TransferLocked(int32_t cmd, int32_t wake_cmd, zx_signals_t state_clr,
                             zx_paddr_t paddr, size_t count, zx_paddr_t read_paddr,
                             size_t read_count, size_t* actual) TA_REQ(lock_);
  zx_status_t SetBufferSizeLocked(uint64_t size) TA_REQ(lock_);

  static void OnSignal(void* ctx, int32_t flags);

  static const fuchsia_hardware_goldfish_pipe_Pipe_ops_t kOps;

  fbl::Mutex lock_;
  fbl::ConditionVariable signal_cvar_;
  ddk::GoldfishPipeProtocolClient pipe_;
  int32_t id_ TA_GUARDED(lock_) = 0;
  zx::bti bti_;
  ddk::IoBuffer cmd_buffer_;
  struct {
    zx::vmo vmo;
    zx::pmt pmt;
    size_t size;
    zx_paddr_t phys;
  } buffer_ TA_GUARDED(lock_) = {};
  zx::event event_ TA_GUARDED(lock_);
};

}  // namespace goldfish
