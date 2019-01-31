// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <fbl/ref_ptr.h>

#include <intel-hda/codec-utils/codec-driver-base.h>

namespace audio {
namespace intel_hda {
namespace codecs {

class QemuCodec : public IntelHDACodecDriverBase {
public:
    static fbl::RefPtr<QemuCodec> Create();

    zx_status_t Init(zx_device_t* codec_dev);
    zx_status_t Start() override;

protected:
    void PrintDebugPrefix() const override;

private:
    friend class fbl::RefPtr<QemuCodec>;
    QemuCodec() { }
    virtual ~QemuCodec() { }
};

}  // namespace codecs
}  // namespace audio
}  // namespace intel_hda
