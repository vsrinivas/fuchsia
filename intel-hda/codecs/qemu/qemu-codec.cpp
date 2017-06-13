// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxtl/auto_lock.h>

#include "drivers/audio/dispatcher-pool/dispatcher-thread.h"

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

mxtl::RefPtr<QemuCodec> QemuCodec::Create() {
    return mxtl::AdoptRef(new QemuCodec);
}

mx_status_t QemuCodec::Init(mx_device_t* codec_dev) {
    mx_status_t res = Bind(codec_dev);
    if (res != MX_OK)
        return res;

    res = Start();
    if (res != MX_OK) {
        Shutdown();
        return res;
    }

    return MX_OK;
}

mx_status_t QemuCodec::Start() {
    mx_status_t res;

    auto output = mxtl::AdoptRef<QemuStream>(new QemuOutputStream());
    res = ActivateStream(output);
    if (res != MX_OK) {
        LOG("Failed to activate output stream (res %d)!", res);
        return res;
    }

    auto input = mxtl::AdoptRef<QemuStream>(new QemuInputStream());
    res = ActivateStream(input);
    if (res != MX_OK) {
        LOG("Failed to activate input stream (res %d)!", res);
        return res;
    }

    return MX_OK;
}

extern "C" mx_status_t qemu_ihda_codec_bind_hook(void* ctx,
                                                 mx_device_t* codec_dev,
                                                 void** cookie) {
    if (cookie == nullptr)
        return MX_ERR_INVALID_ARGS;

    auto codec = QemuCodec::Create();
    MX_DEBUG_ASSERT(codec != nullptr);

    // Init our codec.  If we succeed, transfer our reference to the unmanaged
    // world.  We will re-claim it later when unbind is called.
    mx_status_t res = codec->Init(codec_dev);
    if (res == MX_OK)
        *cookie = codec.leak_ref();

    return res;
}

extern "C" void qemu_ihda_codec_unbind_hook(void* ctx,
                                            mx_device_t* codec_dev,
                                            void* cookie) {
    MX_DEBUG_ASSERT(cookie != nullptr);

    // Reclaim our reference from the cookie.
    auto codec = mxtl::internal::MakeRefPtrNoAdopt(reinterpret_cast<QemuCodec*>(cookie));

    // Shut the codec down.
    codec->Shutdown();

    // Let go of the reference.
    codec.reset();

    // Signal the thread pool so it can completely shut down if we were the last client.
    DispatcherThread::ShutdownThreadPool();
}

}  // namespace codecs
}  // namespace audio
}  // namespace intel_hda

