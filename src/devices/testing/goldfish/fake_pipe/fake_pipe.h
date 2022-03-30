// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTING_GOLDFISH_FAKE_PIPE_FAKE_PIPE_H_
#define SRC_DEVICES_TESTING_GOLDFISH_FAKE_PIPE_FAKE_PIPE_H_

#include <fuchsia/hardware/goldfish/pipe/cpp/banjo.h>
#include <lib/fit/function.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>

#include <queue>
#include <vector>

#include <ddktl/device.h>
#include <fbl/mutex.h>

namespace goldfish::sensor {
namespace testing {

// A fake fuchsia.hardware.goldfish.Pipe banjo protocol implementation where
// users could set up custom callbacks for PIPE_CMD_WRITE commands and queue
// outputs for PIPE_CMD_READ commands.
class FakePipe : public ddk::GoldfishPipeProtocol<FakePipe, ddk::base_protocol> {
 public:
  FakePipe();

  // goldfish Pipe protocol ddk mixins.
  zx_status_t GoldfishPipeCreate(int32_t* out_id, zx::vmo* out_vmo);
  zx_status_t GoldfishPipeSetEvent(int32_t id, zx::event pipe_event);
  void GoldfishPipeDestroy(int32_t id);
  void GoldfishPipeOpen(int32_t id);
  void GoldfishPipeExec(int32_t id);
  zx_status_t GoldfishPipeGetBti(zx::bti* out_bti);
  zx_status_t GoldfishPipeConnectSysmem(zx::channel connection);
  zx_status_t GoldfishPipeRegisterSysmemHeap(uint64_t heap, zx::channel connection);

  // FakePipe stores a queue of byte vectors for PIPE_CMD_READ commands.
  // Every time it receives a PIPE_CMD_READ command, it will pop a byte vector
  // and send the contents to the client.
  void EnqueueBytesToRead(const std::vector<uint8_t>& bytes);

  // Set callback function for PIPE_CMD_WRITE commands.
  void SetOnCmdWriteCallback(fit::function<void(const std::vector<uint8_t>&)> fn);

  zx_status_t SetUpPipeDevice();
  // Map command buffer to a memory address so that tests can access.
  fzl::VmoMapper MapCmdBuffer();
  // Map IO buffer to a memory address so that tests can access. Wsill create a
  // new IO buffer if there is none available.
  fzl::VmoMapper MapIoBuffer();

  const goldfish_pipe_protocol_t* proto() const;
  bool IsPipeReady() const;
  zx::event& pipe_event();
  const std::vector<std::vector<uint8_t>>& io_buffer_contents() const;

 private:
  zx_status_t PrepareIoBuffer() TA_REQ(lock_);

  fbl::Mutex lock_;

  goldfish_pipe_protocol_t proto_;
  zx::unowned_bti bti_ TA_GUARDED(lock_);

  static constexpr int32_t kPipeId = 1;
  zx::vmo pipe_cmd_buffer_ TA_GUARDED(lock_) = zx::vmo();
  zx::vmo pipe_io_buffer_ TA_GUARDED(lock_) = zx::vmo();
  size_t io_buffer_size_;

  zx::event pipe_event_;

  bool pipe_created_ = false;
  bool pipe_opened_ = false;

  fit::function<void(const std::vector<uint8_t>&)> on_cmd_write_;
  std::vector<std::vector<uint8_t>> io_buffer_contents_;
  std::queue<std::vector<uint8_t>> bytes_to_read_ TA_GUARDED(lock_);
};

}  // namespace testing
}  // namespace goldfish::sensor

#endif  // SRC_DEVICES_TESTING_GOLDFISH_FAKE_PIPE_FAKE_PIPE_H_
