// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hdmi-codec.h"

#include <stdio.h>

#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <dispatcher-pool/dispatcher-thread-pool.h>
#include <fbl/auto_lock.h>
#include <intel-hda/codec-utils/stream-base.h>

#include "src/media/audio/drivers/intel-hda/codecs/hdmi/hdmi_ihda_codec-bind.h"

namespace audio {
namespace intel_hda {
namespace codecs {

void HdmiCodec::PrintDebugPrefix() const { printf("HDMICodec : "); }

zx_status_t HdmiCodec::Create(void* ctx, zx_device_t* parent) {
  auto codec = fbl::MakeRefCounted<HdmiCodec>();
  return codec->Init(parent);
}

zx_status_t HdmiCodec::Init(zx_device_t* codec_dev) {
  zx_status_t res = Bind(codec_dev, "hdmi-codec").code();
  if (res != ZX_OK) {
    return res;
  }

  res = Start();
  if (res != ZX_OK) {
    Shutdown();
    return res;
  }

  return ZX_OK;
}

zx_status_t HdmiCodec::Start() { return ZX_OK; }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = HdmiCodec::Create;
  return ops;
}();

}  // namespace codecs
}  // namespace intel_hda
}  // namespace audio

ZIRCON_DRIVER(hdmi_ihda_codec, audio::intel_hda::codecs::driver_ops, "zircon", "0.1")
