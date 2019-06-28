// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CODECS_QEMU_QEMU_CODEC_H_
#define ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CODECS_QEMU_QEMU_CODEC_H_

#include <ddk/device.h>
#include <fbl/ref_ptr.h>
#include <intel-hda/codec-utils/codec-driver-base.h>

namespace audio {
namespace intel_hda {
namespace codecs {

class QemuCodec : public IntelHDACodecDriverBase {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  zx_status_t Init(zx_device_t* codec_dev);
  zx_status_t Start() override;

 protected:
  void PrintDebugPrefix() const override;

 private:
  friend class fbl::RefPtr<QemuCodec>;
  QemuCodec() {}
  virtual ~QemuCodec() {}
};

}  // namespace codecs
}  // namespace intel_hda
}  // namespace audio

#endif  // ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CODECS_QEMU_QEMU_CODEC_H_
