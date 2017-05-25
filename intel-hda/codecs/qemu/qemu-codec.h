// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <mxtl/ref_ptr.h>

#include "drivers/audio/intel-hda/codecs/utils/codec-driver-base.h"

namespace audio {
namespace intel_hda {
namespace codecs {

class QemuCodec : public IntelHDACodecDriverBase {
public:
    static mxtl::RefPtr<QemuCodec> Create();

    mx_status_t Init(mx_device_t* codec_dev);
    mx_status_t Start() override;

protected:
    void PrintDebugPrefix() const override;

private:
    friend class mxtl::RefPtr<QemuCodec>;
    QemuCodec() { }
    virtual ~QemuCodec() { }
};

}  // namespace codecs
}  // namespace audio
}  // namespace intel_hda
