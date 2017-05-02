// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <mxtl/ref_ptr.h>

#include "drivers/audio/intel-hda/codecs/utils/codec-driver-base.h"

#include "utils.h"

namespace audio {
namespace intel_hda {
namespace codecs {

class RealtekCodec : public IntelHDACodecDriverBase {
public:
    static mxtl::RefPtr<RealtekCodec> Create();

    mx_status_t Init(mx_driver_t* driver, mx_device_t* codec_dev);
    mx_status_t Start() override;
    mx_status_t ProcessSolicitedResponse(const CodecResponse& resp) override;

protected:
    void PrintDebugPrefix() const override;

    mx_status_t SetupCommon();
    mx_status_t SetupAcer12();
    mx_status_t SetupIntelNUC();
    mx_status_t RunCommandList(const CommandListEntry* cmds, size_t cmd_count);
    mx_status_t CreateAndStartStreams(const StreamProperties* streams, size_t stream_cnt);

private:
    friend class mxtl::RefPtr<RealtekCodec>;
    RealtekCodec() { }
    virtual ~RealtekCodec() { }

    bool waiting_for_impl_id_ = true;
};

}  // namespace codecs
}  // namespace audio
}  // namespace intel_hda
