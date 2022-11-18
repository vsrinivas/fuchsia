// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_LIB_AML_DSP_DSP_H_
#define SRC_MEDIA_AUDIO_DRIVERS_LIB_AML_DSP_DSP_H_

#include <fidl/fuchsia.hardware.dsp/cpp/wire.h>
#include <fidl/fuchsia.hardware.mailbox/cpp/wire.h>

#include "src/devices/mailbox/drivers/aml-mailbox/meson_mhu_common.h"

using MailboxClient = fidl::WireSyncClient<fuchsia_hardware_mailbox::Device>;
using DspClient = fidl::WireSyncClient<fuchsia_hardware_dsp::DspDevice>;

namespace audio::aml_g12 {

class AmlMailboxDevice {
 public:
  explicit AmlMailboxDevice(MailboxClient client);
  // Create data processing tasks in DSP FW.
  zx_status_t DspCreateProcessingTask(AddrInfo* arg, uint8_t size);
  // Enable data processing tasks in DSP FW.
  zx_status_t DspProcessTaskStart();
  // Stop data processing tasks in DSP FW.
  zx_status_t DspProcessTaskStop();
  // Notify the DSP FW of the current position information of the ring buffer.
  zx_status_t DspProcessTaskPosition(uint32_t position);

 private:
  zx_status_t SendDspCommand(uint8_t* data, uint8_t size, uint8_t cmd);
  MailboxClient client_;
};

class AmlDspDevice {
 public:
  explicit AmlDspDevice(DspClient client);
  // Initialize the DSP firmware and start it.
  zx_status_t DspHwInit();

 private:
  DspClient client_;
};

}  // namespace audio::aml_g12

#endif  // SRC_MEDIA_AUDIO_DRIVERS_LIB_AML_DSP_DSP_H_
