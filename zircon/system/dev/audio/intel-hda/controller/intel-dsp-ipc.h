// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CONTROLLER_INTEL_DSP_IPC_H_
#define ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CONTROLLER_INTEL_DSP_IPC_H_

#include <lib/fit/function.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/time.h>

#include <functional>
#include <optional>

#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/span.h>
#include <fbl/string.h>
#include <intel-hda/utils/intel-audio-dsp-ipc.h>
#include <intel-hda/utils/intel-hda-registers.h>
#include <intel-hda/utils/status.h>
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
  virtual Status Send(uint32_t primary, uint32_t extension) = 0;
  virtual Status SendWithData(uint32_t primary, uint32_t extension,
                              fbl::Span<const uint8_t> payload, fbl::Span<uint8_t> recv_buffer,
                              size_t* bytes_received) = 0;

  // Return true if at least one operation is pending.
  virtual bool IsOperationPending() const = 0;

  // Default timeout for IPC operations.
  static constexpr auto kDefaultTimeout = zx::msec(1000);
};

// Create a new DspChannel backed by real hardware.
std::unique_ptr<DspChannel> CreateHardwareDspChannel(
    fbl::String log_prefix, adsp_registers_t* regs,
    std::optional<std::function<void(NotificationType)>> notification_callback = std::nullopt,
    zx::duration hardware_timeout = DspChannel::kDefaultTimeout);

}  // namespace intel_hda
}  // namespace audio

#endif  // ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CONTROLLER_INTEL_DSP_IPC_H_
