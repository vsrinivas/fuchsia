// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "qemu-codec.h"

#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <dispatcher-pool/dispatcher-thread-pool.h>
#include <fbl/auto_lock.h>

#include "debug-logging.h"
#include "qemu-stream.h"
#include "src/media/audio/drivers/intel-hda/codecs/qemu/qemu_ihda_codec-bind.h"

namespace audio {
namespace intel_hda {
namespace codecs {

class QemuInputStream : public QemuStream {
 public:
  static constexpr uint32_t STREAM_ID = 2;
  static constexpr uint16_t CONVERTER_NID = 4;
  QemuInputStream() : QemuStream(STREAM_ID, true, CONVERTER_NID) {}
};

class QemuOutputStream : public QemuStream {
 public:
  static constexpr uint32_t STREAM_ID = 1;
  static constexpr uint16_t CONVERTER_NID = 2;
  QemuOutputStream() : QemuStream(STREAM_ID, false, CONVERTER_NID) {}
};

void QemuCodec::PrintDebugPrefix() const { printf("QEMUCodec : "); }

zx_status_t QemuCodec::Create(void* ctx, zx_device_t* parent) {
  fbl::RefPtr<QemuCodec> codec = fbl::AdoptRef(new QemuCodec);
  ZX_DEBUG_ASSERT(codec != nullptr);
  return codec->Init(parent);
}

zx_status_t QemuCodec::Init(zx_device_t* codec_dev) {
  zx_status_t res = Bind(codec_dev, "qemu-codec").code();
  if (res != ZX_OK)
    return res;

  res = Start();
  if (res != ZX_OK) {
    Shutdown();
    return res;
  }

  return ZX_OK;
}

zx_status_t QemuCodec::Start() {
  zx_status_t res;

  auto output = fbl::AdoptRef<QemuStream>(new QemuOutputStream());
  res = ActivateStream(output);
  if (res != ZX_OK) {
    LOG("Failed to activate output stream (res %d)!", res);
    return res;
  }

  auto input = fbl::AdoptRef<QemuStream>(new QemuInputStream());
  res = ActivateStream(input);
  if (res != ZX_OK) {
    LOG("Failed to activate input stream (res %d)!", res);
    return res;
  }

  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = QemuCodec::Create;
  return ops;
}();

}  // namespace codecs
}  // namespace intel_hda
}  // namespace audio

ZIRCON_DRIVER(qemu_ihda_codec, audio::intel_hda::codecs::driver_ops, "zircon", "0.1")
