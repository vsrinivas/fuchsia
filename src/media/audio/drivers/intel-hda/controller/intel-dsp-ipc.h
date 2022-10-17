// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_IPC_H_
#define SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_IPC_H_

#include <lib/fit/function.h>
#include <lib/stdcompat/span.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/string.h>
#include <intel-hda/utils/intel-audio-dsp-ipc.h>
#include <intel-hda/utils/intel-hda-registers.h>
#include <refcount/blocking_refcount.h>

namespace audio {
namespace intel_hda {

// A DspChannel manages a inter-processor communications channel from the
// Intel HDA driver to the DSP.
class DspChannel {
 public:
  // Implementations will block until all pending operations have been cancelled and callbacks
  // completed.
  virtual ~DspChannel() {}

  // Shutdown the object, cancelling all in-flight transactions.
  //
  // Called implicitly on destruction if not called earlier.
  virtual void Shutdown() = 0;

  // Process an interrupt.
  //
  // Should be called each time the DSP receives an interrupt, allowing this
  // object to process any IPC-related interrupts that may be pending.
  virtual void ProcessIrq() = 0;

  // Send an IPC message and wait for response.
  //
  // The second variant |SendWithData| allows a data payload to be sent and/or
  // received from the DSP. Empty spans are allowed to indicate no data should
  // be sent or received, and both the send and receive spans may point to the
  // same underlying memory if the same buffer should be used for both reading
  // and writing.
  virtual zx::result<> Send(uint32_t primary, uint32_t extension) = 0;
  virtual zx::result<> SendWithData(uint32_t primary, uint32_t extension,
                                    cpp20::span<const uint8_t> payload,
                                    cpp20::span<uint8_t> recv_buffer, size_t* bytes_received) = 0;

  // Return true if at least one operation is pending.
  virtual bool IsOperationPending() const = 0;

  // Default timeout for IPC operations.
  static constexpr auto kDefaultTimeout = zx::msec(1000);
};

// Create a new DspChannel backed by real hardware.
std::unique_ptr<DspChannel> CreateHardwareDspChannel(
    fbl::String log_prefix, MMIO_PTR adsp_registers_t* regs,
    std::optional<std::function<void(NotificationType)>> notification_callback = std::nullopt,
    zx::duration hardware_timeout = DspChannel::kDefaultTimeout);

}  // namespace intel_hda
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_IPC_H_
