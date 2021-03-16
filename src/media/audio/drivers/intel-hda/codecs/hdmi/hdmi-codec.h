// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CODECS_HDMI_HDMI_CODEC_H_
#define SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CODECS_HDMI_HDMI_CODEC_H_

#include <lib/ddk/device.h>

#include <fbl/ref_ptr.h>
#include <intel-hda/codec-utils/codec-driver-base.h>

#include "utils.h"

namespace audio {
namespace intel_hda {
namespace codecs {

class HdmiCodec : public IntelHDACodecDriverBase {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  zx_status_t Init(zx_device_t* codec_dev);
  zx_status_t Start() override;
  zx_status_t ProcessSolicitedResponse(const CodecResponse& resp) override;

 protected:
  zx_status_t Setup();
  zx_status_t RunCommandList(const CommandListEntry* cmds, size_t cmd_count);
  zx_status_t CreateAndStartStreams(const StreamProperties* streams, size_t stream_cnt);

 private:
  friend class fbl::RefPtr<HdmiCodec>;
  HdmiCodec() {}
  virtual ~HdmiCodec() {}

  bool waiting_for_impl_id_ = false;
};

}  // namespace codecs
}  // namespace intel_hda
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CODECS_HDMI_HDMI_CODEC_H_
