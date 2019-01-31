// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>

#include <dispatcher-pool/dispatcher-thread-pool.h>

#include "debug-logging.h"
#include "qemu-codec.h"
#include "qemu-stream.h"

namespace audio {
namespace intel_hda {
namespace codecs {

class QemuInputStream : public QemuStream  {
public:
    static constexpr uint32_t STREAM_ID = 2;
    static constexpr uint16_t CONVERTER_NID = 4;
    QemuInputStream() : QemuStream(STREAM_ID, true, CONVERTER_NID) { }
};

class QemuOutputStream : public QemuStream  {
public:
    static constexpr uint32_t STREAM_ID = 1;
    static constexpr uint16_t CONVERTER_NID = 2;
    QemuOutputStream() : QemuStream(STREAM_ID, false, CONVERTER_NID) { }
};

void QemuCodec::PrintDebugPrefix() const {
    printf("QEMUCodec : ");
}

fbl::RefPtr<QemuCodec> QemuCodec::Create() {
    return fbl::AdoptRef(new QemuCodec);
}

zx_status_t QemuCodec::Init(zx_device_t* codec_dev) {
    zx_status_t res = Bind(codec_dev, "qemu-codec");
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

extern "C" zx_status_t qemu_ihda_codec_bind_hook(void* ctx,
                                                 zx_device_t* codec_dev) {
    auto codec = QemuCodec::Create();
    ZX_DEBUG_ASSERT(codec != nullptr);

    // Init our codec.
    return codec->Init(codec_dev);
}

}  // namespace codecs
}  // namespace audio
}  // namespace intel_hda

