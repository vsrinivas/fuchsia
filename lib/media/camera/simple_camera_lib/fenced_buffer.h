// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEDIA_CAMERA_SIMPLE_CAMERA_LIB_FENCED_BUFFER_H_
#define GARNET_LIB_MEDIA_CAMERA_SIMPLE_CAMERA_LIB_FENCED_BUFFER_H_

#include <stdint.h>

#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/fsl/tasks/message_loop.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/log_settings_command_line.h>
#include <lib/fxl/logging.h>
#include <zircon/status.h>
#include <zx/event.h>
#include <zx/vmo.h>

#include <garnet/lib/media/camera/simple_camera_lib/buffer.h>

namespace simple_camera {

class FencedBuffer;
using BufferCallback = fit::function<void(FencedBuffer*)>;

// FencedBuffer includes acquire and release fences
// which allow the locked or available status of the buffer to be
// signaled across processes. The fences follow the usage pattern
// described in image_pipe.fidl:
// The acquire fence signals the consumer that the writing is complete,
// and the release fence is signalled from the consumer that the
// frame is no longer needed for reading. These fences are indended for
// communication between only 2 processes; the fences are both reset upon
// receipt of the release fence signal.
// The state transitions of the buffer are thus:
// acquire fence  | release fence | state
//       0        |       0       |   Buffer is in writer control.
//                |               |   The writer may be writing to the buffer.
//       1        |       0       |   Buffer is done writing, signals the
//                                    consumer.
//       X        |       1       |   Consumer is done.  The buffer calls
//                                    OnReleaseFenceSignalled, and resets the
//                                    fences.
// FencedBuffer also adds an index variable, which is used for external
// record-keeping.  The index variable does not influence the internal
// workings of this class.
class FencedBuffer : public Buffer {
 public:
  ~FencedBuffer();

  void Reset() override;   // clear acquire and release fences
  void Signal() override;  // set acquire fence

  void DuplicateAcquireFence(zx::event* result) {
    // TODO: remove write permissions
    acquire_fence_.duplicate(ZX_RIGHT_SAME_RIGHTS, result);
  }

  void DuplicateReleaseFence(zx::event* result) {
    release_fence_.duplicate(ZX_RIGHT_SAME_RIGHTS, result);
  }

  static std::unique_ptr<FencedBuffer> Create(uint64_t buffer_size,
                                       const zx::vmo& main_buffer,
                                       uint64_t offset,
                                       uint32_t index);

  // This function is called when the release fence is signalled
  void OnReleaseFenceSignalled(async_t* async,
                               async::WaitBase* wait,
                               zx_status_t status,
                               const zx_packet_signal* signal);

  // Set a handler function that will be called whenever the release fence
  // is signalled.
  void SetReleaseFenceHandler(BufferCallback callback);

  uint32_t index() { return index_; }

 private:
  uint32_t index_;
  BufferCallback release_fence_callback_;
  async::WaitMethod<FencedBuffer, &FencedBuffer::OnReleaseFenceSignalled>
      release_fence_waiter_{this};

  FencedBuffer();

  zx::event acquire_fence_;
  zx::event release_fence_;
};

}  // namespace simple_camera

#endif  // GARNET_LIB_MEDIA_CAMERA_SIMPLE_CAMERA_LIB_FENCED_BUFFER_H_
